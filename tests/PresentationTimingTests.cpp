#include "graphics/presentation/presentationClock.h"
#include "loader/simulationClock.h"

#include <cmath>
#include <cstdio>
#include <limits>

int main() {
	using namespace Libs::VideoOut;
	int        failures = 0;
	const auto check    = [&](bool value, const char* text) {
		if (!value) {
			std::fprintf(stderr, "FAILED: %s\n", text);
			failures++;
		}
	};
	VblankClock        clock;
	constexpr uint64_t frequency = 10'000'000;
	clock.Reset(1, frequency, 60);
	uint64_t ticks = 0;
	// A display that only accepts thirty frames per second must still expose
	// sixty guest vblanks per second. This also exercises fractional QPC periods.
	for (uint64_t frame = 1; frame <= 1800; frame++) {
		ticks += clock.Advance(1 + frame * frequency / 30);
	}
	check(ticks == 3600, "30 host frames/s preserve 60 guest ticks/s for a minute");
	check(clock.Deadline() == 1 + 60 * frequency + 166'667,
	      "60 Hz deadlines retain the fractional period without drift");
	check(clock.Advance(1 + 60 * frequency) == 0, "no duplicate tick at the same instant");
	check(clock.Advance(1 + 61 * frequency) == 60, "one-second stall coalesces sixty ticks");
	check(clock.Advance(1) == 0, "a backward sample cannot rewind the clock");
	clock.Reset(100 * frequency, frequency, 60);
	check(clock.Advance(100 * frequency) == 0, "resume resets the epoch without a pause backlog");
	clock.Reset(0, frequency, 120);
	check(clock.Advance(frequency) == 120, "120 Hz remains a separate guest clock setting");
	check(PresentationDelay(200'001, 1, frequency, 60, 1) == 0,
	      "a frame ready at 20 ms need not wait for the 33 ms vblank");
	check(PresentationDelay(100'001, 1, frequency, 60, 1) == 66'667,
	      "on-time frames respect the sixty-frame presentation limit");
	check(PresentationDelay(200'001, 1, frequency, 60, 2) == 133'334,
	      "a title's own two-vblank flip interval remains respected");
	check(PresentationDelay(1, 0, frequency, 60, 1) == 0, "first frame is immediately eligible");
	check(PresentationDelay(200'001, 1, frequency, 60, 1, 30) == 133'334,
	      "an optional thirty-frame limit changes presentation spacing only");
	uint64_t origin = 1;
	for (uint64_t frame = 1; frame <= 1800; frame++) {
		const auto deadline = origin + PresentationSpacing(frequency, 60, 1, 30);
		origin              = PresentationOrigin(deadline + 10'000, origin, frequency, 60, 1, 30);
	}
	check(origin == 1 + 1800 * 333'334,
	      "one millisecond wake-up jitter does not accumulate across a minute");
	check(PresentationOrigin(400'001, 1, frequency, 60, 1, 30) == 400'001,
	      "a slow frame rebases pacing without a catch-up burst");
	check(PresentationDelay(400'001, 400'001, frequency, 60, 1, 30) == 333'334,
	      "the frame after a stall still gets a full presentation period");
	check(PresentationOrigin(0, 0, frequency, 60, 1, 30) == 0,
	      "first presentation has no artificial deadline");
	Loader::GamePatch::SimulationClock simulation;
	check(simulation.Advance(0) == simulation.Nominal, "first simulation step is sixty Hz");
	double simulated = 0.0;
	double elapsed   = 0.0;
	double step      = simulation.Nominal;
	for (unsigned frame = 0; frame < 1500; frame++) {
		const double duration = frame < 300 ? 1.0 / 60.0 : frame < 900 ? 1.0 / 30.0 : 1.0 / 45.0;
		simulated += step;
		elapsed += duration;
		step = simulation.Advance(duration);
		check(step >= simulation.Nominal && step <= simulation.Longest,
		      "adaptive steps stay inside the configured timestep range");
	}
	check(std::abs(simulated - elapsed) < 0.1,
	      "60-to-30-to-45 FPS requested game time follows wall time");
	simulation.Reset();
	simulated = elapsed  = 0.0;
	step                 = simulation.Nominal;
	double largest_error = 0.0;
	for (unsigned frame = 0; frame < 3000; frame++) {
		// Uneven CPU frame times and occasional 90 ms stalls must not make the
		// game clock jump between its endpoints or accumulate ongoing drift.
		const double duration = frame % 120 == 0 ? 0.09 : 1.0 / 45.0;
		simulated += step;
		elapsed += duration;
		const double next_step = simulation.Advance(duration);
		check(std::abs(next_step - step) <= simulation.MaxStepChange + 1e-12,
		      "isolated stalls and recovery cannot abruptly change the requested step");
		step          = next_step;
		largest_error = std::max(largest_error, std::abs(simulated - elapsed));
	}
	check(largest_error < 0.12 && std::abs(simulated - elapsed) < 0.02,
	      "smoothing noisy presentation retains bounded time error without cumulative drift");
	check(simulation.Advance(2.0) == simulation.Nominal,
	      "a loading stall resets timing without catch-up debt");
	check(simulation.Advance(std::numeric_limits<double>::quiet_NaN()) == simulation.Nominal,
	      "invalid duration cannot poison the game clock");
	for (unsigned frame = 0; frame < 300; frame++) {
		step = simulation.Advance(1.0 / 18.0);
	}
	check(step == simulation.Longest,
	      "sub-thirty performance cannot inject an unsafe unbounded physics step");
	return failures == 0 ? 0 : 1;
}
