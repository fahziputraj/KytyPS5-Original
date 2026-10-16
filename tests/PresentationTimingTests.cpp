#include "graphics/presentation/presentationClock.h"

#include <cstdio>

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
	return failures == 0 ? 0 : 1;
}
