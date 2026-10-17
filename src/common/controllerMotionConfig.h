#pragma once

#include <cstdint>
#include <string_view>

namespace Config {
constexpr uint32_t DEFAULT_CONTROLLER_MOTION_RATE = 250;

constexpr bool IsControllerMotionSourceValid(std::string_view source) {
	if (source == "active" || source == "off") {
		return true;
	}
	if (source.size() != 32) {
		return false;
	}
	for (const char c: source) {
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return false;
		}
	}
	return true;
}

constexpr bool IsControllerMotionRateValid(uint32_t rate) {
	return rate == 0 || (rate >= 60 && rate <= 1000);
}
} // namespace Config
