#include "common/emulatorConfig.h"

#include <cstdio>
#include <cstdlib>

static void Env(const char* name, const char* value) {
#ifdef _WIN32
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

int main() {
	int        failures = 0;
	const auto check    = [&](bool value, const char* text) {
		if (!value) {
			std::fprintf(stderr, "FAILED: %s\n", text);
			++failures;
		}
	};
	Config::Initialize();
	Config::ConfigOptions config;
	Env("KYTY_PRESENT_WHEN_READY", "1");
	Env("KYTY_PRESENT_FPS", "45");
	Config::Load(config);
	check(Config::IndependentPresentationEnabled() && Config::GetPresentationFps() == 45,
	      "unset options preserve the bundled preset");
	config.independent_presentation = false;
	config.presentation_fps         = 30;
	Config::Load(config);
	check(!Config::IndependentPresentationEnabled() && Config::GetPresentationFps() == 30,
	      "explicit CLI or launcher values override the preset");
	config.independent_presentation = true;
	config.presentation_fps         = 0;
	Env("KYTY_PRESENT_WHEN_READY", "0");
	Config::Load(config);
	check(Config::IndependentPresentationEnabled() && Config::GetPresentationFps() == 0,
	      "explicit independent delivery and uncapped setting remain distinct");
	check(Config::GetVblankFrequency() == 60, "presentation changes never lower guest vblank");
	Config::Shutdown();
	return failures == 0 ? 0 : 1;
}
