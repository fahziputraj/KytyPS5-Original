#ifndef COMMON_PAD_INPUT_DEFAULTS_H_
#define COMMON_PAD_INPUT_DEFAULTS_H_

#include <array>
#include <string_view>

namespace Common {

// Default controller bindings, shared by the emulator's input map and the launcher's mapping
// dialog: `control` is the guest control name, `source` the SDL gamepad source name.
// GyroToggle has no default. On a pad without a touchpad, an unbound Back is the centred
// touchpad click instead (hostInput.cpp, BackClicksTouchPad).
struct PadDefaultBinding {
	std::string_view control;
	std::string_view source;
};

inline constexpr std::array PAD_DEFAULT_BINDINGS = {
    PadDefaultBinding {"Cross", "South"},      PadDefaultBinding {"Circle", "East"},
    PadDefaultBinding {"Square", "West"},      PadDefaultBinding {"Triangle", "North"},
    PadDefaultBinding {"Options", "Start"},    PadDefaultBinding {"L3", "LeftStick"},
    PadDefaultBinding {"R3", "RightStick"},    PadDefaultBinding {"L1", "LeftShoulder"},
    PadDefaultBinding {"R1", "RightShoulder"}, PadDefaultBinding {"L2", "LeftTrigger"},
    PadDefaultBinding {"R2", "RightTrigger"},  PadDefaultBinding {"Up", "DpadUp"},
    PadDefaultBinding {"Down", "DpadDown"},    PadDefaultBinding {"Left", "DpadLeft"},
    PadDefaultBinding {"Right", "DpadRight"},  PadDefaultBinding {"TouchPad", "Touchpad"},
};

// The default source of a control, or empty when it has none.
constexpr std::string_view DefaultPadSource(std::string_view control) {
	for (const auto& binding: PAD_DEFAULT_BINDINGS) {
		if (binding.control == control) {
			return binding.source;
		}
	}
	return {};
}

} // namespace Common

#endif /* COMMON_PAD_INPUT_DEFAULTS_H_ */
