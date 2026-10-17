// Inspect the features exposed by the current controller mode without a game.
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ctime>
#endif

namespace {
double CpuSeconds() {
#ifdef _WIN32
	FILETIME created {}, exited {}, kernel {}, user {};
	if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
		return 0.0;
	}
	const auto value = [](FILETIME time) {
		return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
	};
	return static_cast<double>(value(kernel) + value(user)) * 1e-7;
#else
	return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
}
} // namespace

int main(int argc, char** argv) {
	bool motion  = false;
	int  seconds = 0;
	if (argc == 3 && std::string_view(argv[1]) == "--sample-seconds") {
		seconds = std::atoi(argv[2]);
	} else if (argc == 4 && std::string_view(argv[1]) == "--sample-seconds" &&
	           std::string_view(argv[3]) == "--motion") {
		seconds = std::atoi(argv[2]);
		motion  = true;
	} else if (argc != 1) {
		std::fprintf(stderr, "Usage: controller_probe [--sample-seconds 1..30 [--motion]]\n");
		return 2;
	}
	if ((argc > 1 && seconds < 1) || seconds > 30) {
		return 2;
	}
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (!SDL_Init(SDL_INIT_GAMEPAD)) {
		std::fprintf(stderr, "SDL controller initialization failed: %s\n", SDL_GetError());
		return 1;
	}
	nlohmann::json result = {{"sdl", SDL_GetRevision()}, {"devices", nlohmann::json::array()}};
	std::vector<SDL_Gamepad*> opened;
	int                       count = 0;
	auto*                     ids   = SDL_GetGamepads(&count);
	for (int i = 0; i < count; ++i) {
		auto* pad = SDL_OpenGamepad(ids[i]);
		if (!pad) {
			result["devices"].push_back({{"id", ids[i]}, {"open_error", SDL_GetError()}});
			continue;
		}
		opened.push_back(pad);
		const bool     gyro   = SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO);
		const bool     accel  = SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL);
		const auto*    name   = SDL_GetGamepadName(pad);
		const auto*    type   = SDL_GetGamepadStringForType(SDL_GetGamepadType(pad));
		nlohmann::json device = {
		    {"id", ids[i]},
		    {"name", name != nullptr ? name : "Unknown controller"},
		    {"type", type != nullptr ? type : "unknown"},
		    {"vendor", SDL_GetGamepadVendor(pad)},
		    {"product", SDL_GetGamepadProduct(pad)},
		    {"gyro", gyro},
		    {"accelerometer", accel},
		    {"gyro_rate_hz", SDL_GetGamepadSensorDataRate(pad, SDL_SENSOR_GYRO)}};
		if (motion) {
			device["gyro_enabled"] =
			    gyro && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
			device["accelerometer_enabled"] =
			    accel && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true);
		}
		result["devices"].push_back(device);
	}
	SDL_free(ids);
	if (seconds > 0) {
		const auto start  = std::chrono::steady_clock::now();
		const auto cpu    = CpuSeconds();
		uint64_t   events = 0, sensors = 0;
		while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
			SDL_Event event;
			if (SDL_WaitEventTimeout(&event, 8)) {
				++events;
				if (event.type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE) {
					++sensors;
				}
			}
		}
		const double wall =
		    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		result["sample"] = {
		    {"wall_seconds", wall},       {"process_cpu_seconds", CpuSeconds() - cpu},
		    {"events", events},           {"sensor_events", sensors},
		    {"motion_requested", motion}, {"scope", "SDL input only; no game or renderer"}};
	}
	for (auto* pad: opened) {
		SDL_CloseGamepad(pad);
	}
	SDL_Quit();
	std::puts(result.dump(2).c_str());
	return 0;
}
