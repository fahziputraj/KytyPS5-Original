# Astro Bot: 30 FPS timing and accurate graphics

## Current selection (2026-10-17)

The user requested returning to the earlier fixed 30 FPS profile after rejecting
the accurate graphics / uncapped comparison: 1920 x 1080 output, tessellation on,
accurate GPU occlusion off, linear-image readback off and raw-image synchronization
off. Keep the guarded 30 FPS engine patch and variable presentation at 60 Hz guest
vblank. `Start-AstroBot.ps1` defaults to this performance profile.

`KYTY_CPU_HYBRID=on` puts critical graphics helper threads on the available P-cores;
CP reservation keeps its own P-core, and background compiles use E-cores. This is
not a process-wide exclusion of E-cores. `KYTY_WRITE_RANGE_STATS=0` removes the
extra image-overlap query used only to count images spared by a proven write range;
it does not change the write range or synchronization itself. A guaranteed FPS
improvement from CPU placement has not been demonstrated in a controlled scene.

The user subsequently requested adaptive FPS with maintained game pace. The
experiment remains on `codex/astro-adaptive-clock-experiment`, with a separate
`Start-AstroBot-Adaptive.cmd` entry point. The normal entry point retains the
fixed 30 FPS profile. Both modes retain the same graphics and P-core settings.

The adaptive patch is specific to the verified Astro 01.007 executable. Before
main-output flip completion releases the guest, it updates the same guarded
rate, seconds-per-step and microseconds-per-step values from monotonic elapsed
frame time. An averaging controller repays small timing discrepancies gradually.
The step remains between 1/60 and 1/30 second, with a 60 FPS presentation ceiling;
loading gaps above 250 ms reset the controller rather than building catch-up debt.
Below 30 FPS it can still slow down. Unit tests confirm the controller's time
accounting, not correct physics or animation in the game. Treat maintained pace
as unverified until walking, jumps and moving platforms are compared in gameplay.
This is not hardware VRR configuration or a generic fix for other games.

Adaptive launch uses its bundled patch independently of the installed patch list.
The normal launcher and `Start-AstroBot.cmd` retain the installed 30 FPS patch.
The notes below retain the preceding investigation and rejected settings.

The base is Jetsku's exact `u59-windows-20261013-int19.1` tag, commit
`28bc20f146a276479acf42d2e405fb46a4491b8e`. Local delivery tools were rebased onto
it. The earlier official-base crash/preload experiments are preserved on
`codex/backup-pre-jetsku-20261017` (`a4bc67a1`); their replacement shader cache and
resource patches were not replayed over Jetsku's existing implementations.

The screenshots show an approximately 60-to-33 FPS camera-dependent drop in Sky
Garden. The lower GPU utilization and higher CPU utilization suggest a CPU limit;
they do not identify an engine render-distance setting. Shader preloading reduces
first-use compilation stalls, not the ongoing cost of a larger visible scene.

## Timing

`KYTY_PRESENT_WHEN_READY=1` uses two service threads. One advances guest vblank
events from elapsed monotonic host time at the configured frequency (60 Hz for
Astro). The other presents completed flip groups as soon as their frame interval
allows. It sleeps on readiness/vblank notifications rather than polling in a
spin loop. Vulkan acquire, presentation, and GPU waits never run on the guest
vblank thread. Port locks are released across host presentation, with generation
checks before delivering flip completion after a close.

Missed clock ticks are reflected in the next vblank count and coalesced event;
they do not create a burst of render work. Pause/resume rebases the clock without
a catch-up backlog. Guest clock APIs and the TSC frequency remain unchanged.
The title's own flip interval still applies. `KYTY_PRESENT_FPS=30` optionally
limits presentation while retaining a 60 Hz guest display clock; `0` leaves the
limit at the title's configured refresh/flip interval.

Hardware VRR/G-SYNC is a separate display/driver facility. This change permits
variable frame delivery; it does not enable a monitor's VRR or claim to convert
fixed-step game logic into a variable-step simulation. A game that advances by
one fixed simulation step per rendered frame can still need a title-specific
patch. Gameplay-speed validation must use elapsed time or a repeatable animation,
not only an FPS counter or the number of vblank events.

`KYTY_PRESENT_TIMING_LOG=1` prints five-second samples of guest vblank rate and
completed guest flips. These count emulator timing and presentation, not the
game's internal simulation steps or generated frames. `KYTY_PRESENT_WHEN_READY=0`
keeps the original fixed presentation path for A/B testing.

Presentation deadlines retain their nominal origin after scheduler overshoots of
up to 2 ms. Longer CPU/GPU stalls rebase the origin, so they do not produce a
catch-up burst. This avoids adding each timer wake-up delay to every frame period.

## Astro 01.007 clock patch

Independent vblank alone was tested in Sky Garden and the user still observed
slow motion. An initial data-only 30 FPS patch was also insufficient: Astro's
frame-rate setter restored its 60 FPS values during startup.

`patches/PPSA21564-01.007-clock30.json` changes the exact executable's adjacent
double frame rate, float seconds per step and integer microseconds per step to
30, 1/30 and 33333. It prepends a load of that retained rate to the setter at RVA
`0x34c0`, relocating its original RIP-relative operands into the existing padding.
All original arithmetic and register results are retained; the getter at RVA
`0x3520` is untouched. The patch checks both the original 24 data bytes and 96
code bytes before writing. The executable file itself is never modified.

The launch wrapper additionally verifies this source executable SHA-256:
`3b5100797fe83663e18a650f82f901d066acf1029ae80cfb1be638fe0839debd`.
The loader verifies title ID, version, process name and original bytes. Other
versions are not supported. A selected mod's `presentation_fps: 30` pairs the
host cap with the simulation patch only after its writes succeed. Disabled mods
do not request a cap. Timing mods require variable presentation to be enabled.

In the isolated test, runtime reads confirmed 30, 0.033333335 seconds and 33333
microseconds after startup and gameplay. The user first reported continued slow
motion, then reported that it "kinda works now" after further gameplay. Treat this
as an experimental fixed 30 FPS mode, not a proven variable timestep solution.
Below 30 FPS the fixed step can still fall behind real time. Full physics,
cutscene and level coverage has not been validated.

The related older [Astro Rescue Mission engine work](https://github.com/bigmak94/AstroQuest/blob/main/shadps4-arm64-main/src/core/known_title.cpp)
suggested the three-value clock layout. The addresses and setter instructions
above were found and validated in this user's Astro Bot 01.007 executable;
addresses from that older game were not reused.

## Previous accuracy comparison

The previous accurate profile selected 1920 x 1080 output, accurate GPU occlusion
queries, linear-image readback, raw-image buffer synchronization and tessellation.
The shader compiler keeps its performance optimization setting; validation and
profiling remain disabled because they are debugging facilities. Output size is
not a claim that Astro's internal render targets have been patched to native 1440p.
The user tested 2560 x 1440, reported extra lag without improved detail, and asked
to revert. The earlier game buffer log registers 3840 x 2160; the host screen-size
option changes the output swapchain. No game resolution table or render distance
was patched. This profile can cost more CPU/GPU time and does not resolve all existing rendering
glitches. Stable int19.1's shader precompile and pipeline journal remain enabled.

Related primary sources reviewed:

- [Sky Garden camera-direction report](https://github.com/Jetsku/KytyPS5/issues/28).
- [chenxiao07's separation of vblank and host presentation](https://github.com/chenxiao07/KytyPS5/commit/eadf554fbd49a2cc4119b374a926ad3c01128aa7).
- [Earlier ready-frame presentation experiment](https://github.com/RainKikyou/KytyPS5/commit/aac0f9f5e7c5cd31ce5e0aeddd428147584017a6).
- [Vulkan presentation modes](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html).

The implementation retains int19.1's multi-port flip groups. It does not copy
the other fork's single-port, frame-generation or display-clock dependencies.

## CPU placement and delivery

The existing local hybrid CPU change (`63a7cd62`, replayed as `9f8e2fa8`) chooses
reserved CP cores from the fastest core class. With `KYTY_CPU_HYBRID=on`, critical
CP helper threads use the remaining P-cores and background compilation/cache
workers use E-cores. Guest workers retain the general CPU pool. Homogeneous CPUs
keep their existing placement. This is a scheduling change, not a render-distance
hack or a claim of guaranteed FPS improvement.

`tools/build-windows-local.ps1` uses LLVM 22.1.3, release IPO and the bundled
int19 Sky Garden PGO profile. Anonymous function names require the profile's
exact `C:\kyty-src19` path. The build temporarily redirects that junction to this
checkout and restores its previous target in `finally`; it refuses to do so if
another native build is running. Neither source checkout is moved or deleted.
The build output is `_Build/windows-int19`.

`tools/update-build.ps1` runs timing/CPU placement checks, stages the runtime,
backs up files before overwriting `F:\KytyPS5-Build`, and preserves the installed
INI, caches and saves. `-GraphicsProfile Accurate1080p` explicitly changes the
installed global graphics settings; subsequent updates default to preserving them.
The Astro patch is merged into `_Patches/PPSA21564.json` for GUI launch, preserving
other mods and backing up the previous document. Existing disabled clock mods
remain disabled. Hybrid placement is available but stays off by default until a
same-scene performance gain is demonstrated.

`Start-AstroBot.cmd` uses the guarded clock patch, variable presentation, 30 FPS
and the accurate 1080p profile. For a lower-cost comparison, run
`Start-AstroBot.ps1 -Graphics Performance -Width 1920 -Height 1080`.
For original game timing, use `-Clock Original -Fps 0`; `-Timing Fixed` is available
with that original clock. Normal play uses silent guest logging.

## Validation

- Jetsku-base source build completed with release IPO and int19 PGO.
- All five timing/CPU placement checks passed after the pacing and patch-cap
  changes (13.99 seconds), including hybrid and recorder modes. Clang-format,
  PowerShell parsing, Python compilation and git diff whitespace checks passed.
- Uncapped variable presentation kept the guest clock at approximately 60 Hz
  while Sky Garden completed approximately 34.5 flips/s; user observed slow motion.
- The guarded setter test reached Sky Garden with its 30 FPS clock retained.
- The accurate 2560 x 1440 test reached gameplay with all four requested accuracy
  features enabled. Some scenes fell to approximately 18-26 FPS; source compilation
  overlapped part of that test, so this is not a controlled resolution benchmark.
  The user rejected this profile and the delivery defaults restore 1920 x 1080.
