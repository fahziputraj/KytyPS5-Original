#include "cheatFile.h"
#include "configuration.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
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
	config.screen_resolution        = Configuration::Resolution::R2560X1440;
	config.controller.motion_source = QStringLiteral("01000000000000000000000000000000");
	config.controller.motion_rate   = 125;
	QTemporaryDir directory;
	check(directory.isValid(), "create isolated settings directory");
	const auto path = directory.filePath("Kyty.ini");
	{
		QSettings settings(path, QSettings::IniFormat);
		config.WriteSettings(&settings);
		config.controller.WriteSettings(&settings);
		settings.sync();
		check(settings.status() == QSettings::NoError, "save options");
	}
	Configuration restored;
	{
		QSettings settings(path, QSettings::IniFormat);
		restored.ReadSettings(&settings);
		restored.controller.ReadSettings(&settings);
	}
	check(restored.presentation_clock == "independent" && restored.presentation_fps == 45 &&
	          restored.cpu_hybrid == "on",
	      "all new options survive an INI round trip");
	check(restored.screen_resolution == Configuration::Resolution::R2560X1440,
	      "unrelated settings survive");
	check(restored.controller.motion_source == config.controller.motion_source &&
	          restored.controller.motion_rate == 125,
	      "global motion selection survives an INI round trip");
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
	legacy.controller.ReadSettings(&empty);
	check(legacy.controller.motion_source == "active" && legacy.controller.motion_rate == 250,
	      "old settings retain native active-pad motion with bounded history");
	empty.setValue("controller_motion_source", "invalid");
	empty.setValue("controller_motion_rate", -1);
	legacy.controller.ReadSettings(&empty);
	check(legacy.controller.motion_source == "active" && legacy.controller.motion_rate == 250,
	      "invalid motion configuration falls back safely");
	check(legacy.presentation_clock == "preset" && legacy.presentation_fps == -1 &&
	          legacy.cpu_hybrid == "preset",
	      "older INI files retain preset inheritance");
	QList<QJsonDocument> profiles;
	for (const auto* file_name:
	     {"PPSA21564-01.007-clock30.json", "PPSA21564-01.007-clock-adaptive.json"}) {
		QFile file(QStringLiteral(KYTY_CLOCK_PATCH_DIR "/") + QLatin1String(file_name));
		check(file.open(QIODevice::ReadOnly), "open bundled clock profile");
		QJsonDocument profile;
		check(Cheats::ParseDocument(file.readAll(), &profile).isEmpty(),
		      "parse bundled clock profile");
		profiles.append(profile);
	}
	auto current = profiles.first().object();
	auto mods    = current.value("mods").toArray();
	auto other   = mods.first().toObject();
	other.insert("id", "unrelated");
	other.insert("name", "Unrelated visual cheat");
	other.remove("presentation_fps");
	mods.append(other);
	current.insert("mods", mods);
	QJsonDocument selected;
	check(Cheats::SelectClockProfile(QJsonDocument(current), profiles, "codex.astro.clock-adaptive",
	                                 &selected)
	          .isEmpty(),
	      "launcher switches from fixed to adaptive");
	const auto saved_mods = selected.object().value("mods").toArray();
	check(saved_mods.size() == 3 && saved_mods.first().toObject() == other,
	      "clock selection preserves unrelated cheats");
	check(!saved_mods[1].toObject().value("enabled").toBool() &&
	          saved_mods[2].toObject().value("enabled").toBool() &&
	          saved_mods[2].toObject().value("presentation_fps").toInt() == 60,
	      "exactly the adaptive clock and paired 60 FPS ceiling are selected");
	check(saved_mods[2].toObject().value("memory") ==
	          profiles[1].object().value("mods").toArray().first().toObject().value("memory"),
	      "bundled original-byte guards survive GUI selection");
	check(Cheats::SelectClockProfile(selected, profiles, {}, &selected).isEmpty(),
	      "disable bundled clock patches");
	const auto original_mods = selected.object().value("mods").toArray();
	check(!original_mods[1].toObject().value("enabled").toBool() &&
	          !original_mods[2].toObject().value("enabled").toBool(),
	      "original clock disables both bundled modes");
	auto wrong = current;
	wrong.insert("version", "01.008.000");
	check(!Cheats::SelectClockProfile(QJsonDocument(wrong), profiles, "codex.astro.clock30",
	                                  &selected)
	           .isEmpty(),
	      "GUI clock selection rejects another game version");
	wrong = current;
	wrong.insert("source_sha256", QString(64, QLatin1Char('0')));
	check(!Cheats::SelectClockProfile(QJsonDocument(wrong), profiles, "codex.astro.clock30",
	                                  &selected)
	           .isEmpty(),
	      "GUI clock selection rejects an executable fingerprint mismatch");
	other.insert("presentation_fps", 45);
	mods.append(other);
	current.insert("mods", mods);
	check(!Cheats::SelectClockProfile(QJsonDocument(current), profiles, "codex.astro.clock30",
	                                  &selected)
	           .isEmpty(),
	      "other active timing mods cannot silently conflict");
	check(!Cheats::SelectClockProfile({}, profiles, "guess", &selected).isEmpty(),
	      "unknown clock selection fails closed");
	return failures == 0 ? 0 : 1;
}
