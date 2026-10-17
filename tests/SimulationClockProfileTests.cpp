#include "loader/simulationClock.h"
#include "loader/simulationClockProfile.h"

#include <array>
#include <cstdio>
#include <limits>

int main() {
	using namespace Loader::GamePatch;
	int        failures = 0;
	const auto check    = [&](bool condition, const char* message) {
		if (!condition) {
			std::fprintf(stderr, "FAILED: %s\n", message);
			++failures;
		}
	};
	const auto example = nlohmann::json::parse(R"({"nominal_fps":120,"minimum_fps":60,"fields":[
	    {"offset":"1000","value":"seconds_f64"},
	    {"offset":"2000","value":"rate_f32"}]})");
	SimulationClockProfile profile;
	check(ParseSimulationClockProfile(example, profile),
	      "a second engine can use separate clock fields and rates");
	check(profile.fields.size() == 2 && profile.nominal_fps == 120.0,
	      "profile retains its own layout");
	SimulationClock clock;
	check(clock.Configure(profile.nominal_fps, profile.minimum_fps),
	      "configure another engine's bounds");
	double step = clock.MinimumStep(), simulated = 0.0, elapsed = 0.0;
	for (unsigned frame = 0; frame < 2400; ++frame) {
		const double duration = frame < 600 ? 1.0 / 120.0 : frame < 1500 ? 1.0 / 60.0 : 1.0 / 90.0;
		simulated += step;
		elapsed += duration;
		step = clock.Advance(duration);
		check(step >= 1.0 / 120.0 && step <= 1.0 / 60.0,
		      "a different profile never reuses Astro's bounds");
	}
	check(std::abs(simulated - elapsed) < 0.02, "a second rate range follows elapsed time");
	check(clock.Advance(2.0) == 1.0 / 120.0, "pause reset preserves the configured engine rate");
	check(!clock.Configure(60, 120) && !clock.Configure(0, 0) &&
	          !clock.Configure(std::numeric_limits<double>::infinity(), 30),
	      "invalid bounds fail closed");
	for (const auto* bad: {"", "xyz", "1000junk", "-1000", "fffffffffffffffff"}) {
		check(!ParseSimulationClockProfile(bad, profile), "reject malformed clock addresses");
	}
	check(ParseSimulationClockProfile("08FD4040", profile) && profile.fields.size() == 3,
	      "legacy Astro patches remain readable");
	for (const auto* key: {"offset", "value"}) {
		auto bad = example;
		bad["fields"][0].erase(key);
		check(!ParseSimulationClockProfile(bad, profile), "incomplete fields fail closed");
	}
	auto bad           = example;
	bad["minimum_fps"] = 121;
	check(!ParseSimulationClockProfile(bad, profile), "reject reversed bounds");
	bad                        = example;
	bad["fields"][1]["offset"] = "1004";
	check(!ParseSimulationClockProfile(bad, profile), "reject overlapping fields");
	bad                       = example;
	bad["fields"][0]["value"] = "guessed";
	check(!ParseSimulationClockProfile(bad, profile), "reject unknown encodings");
	bad           = example;
	bad["fields"] = nlohmann::json::array();
	check(!ParseSimulationClockProfile(bad, profile), "reject empty profiles");
	std::array<uint8_t, 24> guard {};
	guard[0] = 1;
	check(IsClockFieldGuarded({0x1010, ClockValue::SecondsF64}, 0x1000, guard),
	      "the full field is guarded");
	check(!IsClockFieldGuarded({0x1014, ClockValue::SecondsF64}, 0x1000, guard),
	      "a partial field is not guarded");
	check(!IsClockFieldGuarded({0xff8, ClockValue::SecondsF64}, 0x1000, guard),
	      "a field before the guard is rejected");
	guard[0] = 0;
	check(!IsClockFieldGuarded({0x1000, ClockValue::SecondsF64}, 0x1000, guard),
	      "an all-zero wildcard is not a byte guard");
	float    f32    = 0.0f;
	double   f64    = 0.0;
	uint64_t micros = 0;
	StoreClockStep({reinterpret_cast<uint64_t>(&f32), ClockValue::SecondsF32}, 1.0 / 40.0);
	StoreClockStep({reinterpret_cast<uint64_t>(&f64), ClockValue::RateF64}, 1.0 / 40.0);
	StoreClockStep({reinterpret_cast<uint64_t>(&micros), ClockValue::MicrosecondsU64}, 1.0 / 40.0);
	check(std::abs(f32 - 0.025f) < 1e-8f && f64 == 40.0 && micros == 25000,
	      "clock bindings write the declared units without changing global clocks");
	return failures == 0 ? 0 : 1;
}
