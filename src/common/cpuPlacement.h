#ifndef EMULATOR_SRC_COMMON_CPUPLACEMENT_H_
#define EMULATOR_SRC_COMMON_CPUPLACEMENT_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Where the emulator's threads run (KYTY_CPU_RESERVE).
//
// The command processor (CP) is the frame's critical path. In U54 its SMT sibling was busy 21.7% of
// the time, mostly with guest job threads, and a woken CP often waited for a CPU.
//
// KYTY_CPU_RESERVE=off (default): nothing changes.
// cp: the CP gets one physical core to itself, both logical processors, through Windows CPU sets
// (SetThreadSelectedCpuSets). Every other thread of the process keeps off that core through the
// process default CPU sets: guest threads, draw-prep workers, service and completion threads, the
// CP recorder, and driver threads alike.
// cp+recorder: the CP recorder thread (KYTY_CP_RECORDER=1) gets a second core the same way.
//
// Details:
// - The allowed processors are read at runtime: the process affinity mask (Process Lasso pins
//   Astro Bot to 0-15, the V-cache CCD) and any process default CPU sets.
// - Only whole cores, every logical processor allowed, are reserved. They are chosen in the last
//   level cache that holds most allowed processors, by SchedulingClass (the CPPC preference),
//   avoiding the core of logical processor 0 (interrupts). KYTY_CPU_RESERVE_CORE=<logical
//   processor> picks the CP's core instead.
// - Reservation is skipped (and logged) when fewer than 6 logical processors would remain.
// - A monitor thread re-applies the layout when the process affinity changes (Process Lasso sets it
//   after the process has started), checking once a second.
// - CPU sets never override a hard thread affinity (SetThreadAffinityMask). Kyty sets none: guest
//   pthread affinities are only stored. The monitor scans the process's threads every 5 s for one
//   anyway (drivers, overlays): a hard affinity that meets the general processors is already
//   narrowed to them by the CPU sets; one confined to the reserved cores runs there regardless.
//   KYTY_CPU_RESERVE_REPIN=1 moves such threads to the general processors (default: counted and
//   logged only).
//
// KYTY_CPU_SETS=<logical processors of group 0> (default unset: no restriction), e.g. 16-31 or
// 0-7,16-23: every thread of the process runs on these processors (within the affinity mask),
// through the process default CPU sets, a soft affinity. With KYTY_CPU_RESERVE the CP's core is
// chosen among them and the other threads get the rest of them. Unlike a process affinity mask,
// Process Lasso's affinity rule does not reset it.
// KYTY_CPU_RESERVE_REASSERT=1 (default off): the monitor also checks once a second whether another
// process replaced the process default CPU sets this module applied, and applies its layout again.
// Process Lasso's CPU-set rule (SetProcessDefaultCpuSets, about 0.6 s after the process starts)
// otherwise silently undoes KYTY_CPU_RESERVE and KYTY_CPU_SETS for every thread but the CP and the
// recorder: they keep their thread-selected sets, so the CP stays confined to its core while other
// threads run there again. Without KYTY_CPU_SETS the other process's sets become the constraint the
// layout is computed in (Process Lasso's (0-31) leaves it unchanged).
//
// Hybrid CPUs (performance and efficiency cores, e.g. Intel 12th-14th gen: GetSystemCpuSetInformation
// reports a higher EfficiencyClass for the P-cores):
// - The CP's and the recorder's cores are always chosen among the fastest class (before, only the
//   index order kept them off the E-cores).
// - KYTY_CPU_HYBRID=off (default): nothing else changes.
//   on (1): the helper threads the CP waits for (Pipeline: the sequencer it waits for at every
//   ordering point, the draw-prep workers whose claimed head slot it spins on, the queue
//   submission, upload DMA and texture staging threads) run on the other P-cores, and the
//   background compiles (Background: pipeline optimizer, journal replay, compute prewarm,
//   ray-tracing pipelines, shader precompile, cache load/save) on the E-cores, so they never take a
//   P-core from the frame. Guest threads keep the process default (P- and E-cores).
//   background (2): only the Background part.
//   The Pipeline part needs at least kMinPipelineProcessors P-core processors outside the reserved
//   cores; without them those threads keep the process default. On a CPU whose cores are all of
//   one class it does nothing (logged).
//
// Placement samples, with the profiler's aggregates, the hang trace or
// KYTY_CPU_PLACEMENT_SAMPLES=1: the CP, the recorder, guest threads, draw-prep workers and service
// threads note where they run.
// - FrameEvents: CpuPlacement{Cp,Guest,Host}Samples; CpuPlacementCpOffCore and
//   CpuPlacementRecorderOffCore (outside their reserved cores); CpuPlacement{Guest,Host}OnCpCore
//   (on the physical core of the CP's latest sample: SMT sharing); CpuPlacementHardAffinity
//   (threads found with a hard affinity other than the process's), CpuPlacementHardAffinityReserved
//   (of those, confined to reserved cores), CpuPlacementRepinned.
// - Hang trace: placement.csv, samples per role and logical processor each second.
namespace Common {

enum class ThreadRole : uint8_t { Cp = 0, Recorder = 1, Guest = 2, Host = 3, Count = 4 };
enum class CpuReserveMode : uint8_t { Off, Cp, CpRecorder };
enum class HybridMode : uint8_t { Off, On, Background };
// Host helper threads by how close they are to the frame's critical path (KYTY_CPU_HYBRID).
enum class HelperClass : uint8_t { Pipeline, Background };

// One logical processor as GetSystemCpuSetInformation reports it.
struct CpuSetInfo {
	uint32_t id               = 0;
	uint16_t group            = 0;
	uint8_t  logical          = 0; // index within the group
	uint8_t  core             = 0; // CoreIndex: shared by SMT siblings
	uint8_t  cache            = 0; // LastLevelCacheIndex
	uint8_t  scheduling_class = 0; // higher = preferred (CPPC)
	uint8_t  efficiency_class = 0; // EfficiencyClass: higher = faster core (hybrid: the P-cores)
	bool     allowed          = false;
};

struct CpuLayout {
	bool                  reserved      = false; // a CP core is reserved
	int                   cp_core       = -1;
	int                   recorder_core = -1;
	std::vector<uint32_t> cp_ids;            // CPU set ids of the CP's core
	std::vector<uint32_t> recorder_ids;      // of the recorder's core (cp+recorder)
	std::vector<uint32_t> general_ids;       // every other allowed processor
	uint64_t              cp_mask       = 0; // group 0 logical processors of the above
	uint64_t              recorder_mask = 0;
	uint64_t              general_mask  = 0;
	std::string           description; // the choice, or why nothing was reserved
	// SplitByCoreClass: the processors the other threads use (the general ones of a reservation,
	// otherwise every allowed one) by core class, when they differ in EfficiencyClass.
	bool                  hybrid           = false;
	std::vector<uint32_t> performance_ids;  // of the fastest class
	std::vector<uint32_t> efficiency_ids;   // of the slower classes
	uint64_t              performance_mask = 0;
	uint64_t              efficiency_mask  = 0;
};

inline constexpr uint32_t kMinGeneralProcessors  = 6;
inline constexpr uint32_t kMinPipelineProcessors = 4;

// Pure (tested): the reserved cores for `sets` (only group 0 is considered).
[[nodiscard]] CpuLayout ComputeCpuLayout(const std::vector<CpuSetInfo>& sets, CpuReserveMode mode,
                                         int      forced_cp_logical,
                                         uint32_t min_general = kMinGeneralProcessors);
// Pure (tested): fills the hybrid fields of a layout ComputeCpuLayout made from the same `sets`.
void SplitByCoreClass(const std::vector<CpuSetInfo>& sets, CpuLayout& layout);
// Pure (tested): the CPU set ids a helper thread gets under `mode` (empty: the process default).
[[nodiscard]] std::vector<uint32_t> HelperCpuSets(const CpuLayout& layout, HybridMode mode,
                                                  HelperClass helper);
// Pure (tested): the hard affinity a thread with `affinity` should get so that it stays off the
// reserved cores: unchanged when nothing is reserved or it already meets the general processors
// (the CPU sets narrow it), otherwise every general processor.
[[nodiscard]] uint64_t GeneralPoolAffinity(uint64_t affinity, const CpuLayout& layout);

// KYTY_CPU_RESERVE=off|cp|cp+recorder (also 0|1|2); nullptr is off. `valid` false: unknown text.
[[nodiscard]] CpuReserveMode ParseCpuReserveMode(const char* text, bool* valid = nullptr);
[[nodiscard]] CpuReserveMode GetCpuReserveMode(); // KYTY_CPU_RESERVE, evaluated once
// KYTY_CPU_HYBRID=off|on|background (also 0|1|2); nullptr is off. `valid` false: unknown text.
[[nodiscard]] HybridMode ParseHybridMode(const char* text, bool* valid = nullptr);
[[nodiscard]] HybridMode GetHybridMode(); // KYTY_CPU_HYBRID, evaluated once
// Pure (tested): "0-15", "16-31", "0-7,16-23", "5" as a mask of logical processors 0..63; 0 with
// `valid` false for empty or malformed text, a descending range or a processor above 63.
[[nodiscard]] uint64_t ParseLogicalMask(const char* text, bool* valid = nullptr);
[[nodiscard]] uint64_t CpuSetsMask();     // KYTY_CPU_SETS, evaluated once; 0: unset (no restriction)
[[nodiscard]] bool     ReassertEnabled(); // KYTY_CPU_RESERVE_REASSERT, evaluated once
// How often the monitor found the process default CPU sets replaced and applied its layout again.
[[nodiscard]] uint32_t PlacementReasserts();

// Startup (Common::InitializeThreads, or the first PlaceCurrentThread): computes and applies the
// layout, and starts the monitor thread when a reservation or placement samples are on. Idempotent.
void InitCpuPlacement();
// Re-applies the layout if the process affinity mask changed since it was computed (the monitor
// calls it once a second).
void MaintainCpuPlacement();
// The calling thread's CPU sets for its role. The CP gets its reserved core. The recorder gets its
// core in cp+recorder mode, otherwise the general processors. Other roles keep the process
// default. No-op when nothing is reserved.
void PlaceCurrentThread(ThreadRole role);
// KYTY_CPU_HYBRID: the calling helper thread's CPU sets for its class (HelperCpuSets), kept when the
// layout is applied again. No-op when the mode leaves the class alone.
void PlaceHelperThread(HelperClass helper);
// The current layout (a copy; tests and logs).
[[nodiscard]] CpuLayout CurrentCpuLayout();

// One pass over the process's threads (the monitor runs it every 5 s): threads whose hard affinity
// differs from the process mask, those confined to reserved cores, and those moved to the general
// processors (`repin`, KYTY_CPU_RESERVE_REPIN). FrameEvents count each thread once per process.
struct AffinityScan {
	uint32_t threads       = 0;
	uint32_t hard          = 0;
	uint32_t reserved_only = 0;
	uint32_t repinned      = 0;
};
AffinityScan       ScanThreadAffinities(bool repin);
[[nodiscard]] bool RepinEnabled(); // KYTY_CPU_RESERVE_REPIN

// Placement samples (see above). Cheap: one processor-number read and a few relaxed increments.
[[nodiscard]] bool PlacementSamplingEnabled();
void               SamplePlacement(ThreadRole role);
// group * 64 + number of the processor the calling thread runs on now.
[[nodiscard]] uint32_t CurrentLogicalProcessor();
// The physical core (group * 256 + CoreIndex) of a logical processor, UINT32_MAX if unknown.
[[nodiscard]] uint32_t CoreOfLogicalProcessor(uint32_t logical);

// Samples per role and logical processor (up to 64) since the previous call (hang trace).
struct PlacementHistogram {
	static constexpr uint32_t Processors = 64;
	std::array<std::array<uint32_t, Processors>, static_cast<size_t>(ThreadRole::Count)> samples {};
	std::array<uint32_t, static_cast<size_t>(ThreadRole::Count)> on_cp_core {};
	uint32_t cp_core = UINT32_MAX; // the CP's last sampled core
};
void TakePlacementHistogram(PlacementHistogram& out);

} // namespace Common

#endif // EMULATOR_SRC_COMMON_CPUPLACEMENT_H_
