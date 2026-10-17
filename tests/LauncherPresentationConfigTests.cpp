#include "configuration.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	int              failures = 0;
	const auto       check    = [&](bool value, const char* message) {
		if (!value) {
			std::fprintf(stderr, "FAILED: %s\n", message);
			++failures;
		}
	};
	Configuration config;
	QString       error;
	check(config.SetGameSettings({{"presentation_clock", "independent"},
	                              {"presentation_fps", "45"},
	                              {"cpu_hybrid", "on"}},
	                             error),
	      "import global/per-game options");
	config.screen_resolution = Configuration::Resolution::R2560X1440;
	QTemporaryDir directory;
	check(directory.isValid(), "create isolated settings directory");
	const auto path = directory.filePath("Kyty.ini");
	{
		QSettings settings(path, QSettings::IniFormat);
		config.WriteSettings(&settings);
		settings.sync();
		check(settings.status() == QSettings::NoError, "save options");
	}
	Configuration restored;
	{
		QSettings settings(path, QSettings::IniFormat);
		restored.ReadSettings(&settings);
	}
	check(restored.presentation_clock == "independent" && restored.presentation_fps == 45 &&
	          restored.cpu_hybrid == "on",
	      "all new options survive an INI round trip");
	check(restored.screen_resolution == Configuration::Resolution::R2560X1440,
	      "unrelated settings survive");
	Configuration game;
	game.CopyEmulatorSettingsFrom(restored);
	check(game.presentation_clock == "independent" && game.presentation_fps == 45 &&
	          game.cpu_hybrid == "on",
	      "game configuration inherits global timing and CPU options");
	for (const QJsonObject& bad:
	     {QJsonObject {{"presentation_clock", "guess"}}, QJsonObject {{"presentation_fps", "-2"}},
	      QJsonObject {{"presentation_fps", "241"}}, QJsonObject {{"presentation_fps", "4.5"}},
	      QJsonObject {{"cpu_hybrid", "guess"}}}) {
		Configuration candidate;
		check(!candidate.SetGameSettings(bad, error), "invalid settings imports fail closed");
	}
	Configuration legacy;
	QSettings     empty(directory.filePath("old.ini"), QSettings::IniFormat);
	legacy.ReadSettings(&empty);
	check(legacy.presentation_clock == "preset" && legacy.presentation_fps == -1 &&
	          legacy.cpu_hybrid == "preset",
	      "older INI files retain preset inheritance");
	return failures == 0 ? 0 : 1;
}
