#include "cheatFile.h"
#include "configuration.h"
#include "temporalStatus.h"

#include <QCoreApplication>
#include <QDir>
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
	config.fsr_upscale              = QStringLiteral("quality");
	config.fsr_frame_generation     = true;
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
	check(restored.fsr_upscale == "quality" && restored.fsr_frame_generation,
	      "FSR selections survive an INI round trip");
	check(restored.controller.motion_source == config.controller.motion_source &&
	          restored.controller.motion_rate == 125,
	      "global motion selection survives an INI round trip");
	Configuration game;
	game.CopyEmulatorSettingsFrom(restored);
	check(game.fsr_upscale == "quality" && game.fsr_frame_generation,
	      "per-game settings inherit global FSR selections");
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
	check(legacy.fsr_upscale == "off" && !legacy.fsr_frame_generation,
	      "FSR remains opt-in for existing settings");
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

	// FSR/DLSS status: package, title eligibility and launch options. Never a GPU claim.
	using TemporalStatus::FsrState;
	const QString astro = QStringLiteral("PPSA21564");
	const QString v107  = QStringLiteral("01.007.000");
	const QString eboot = QStringLiteral("eboot.bin");
	check(TemporalStatus::CertifiedFsrTitle(astro, v107, eboot) &&
	          TemporalStatus::CertifiedFsrTitle(astro, v107, QStringLiteral("EBOOT.BIN")),
	      "the certified FSR title is recognised");
	check(!TemporalStatus::CertifiedFsrTitle(QStringLiteral("PPSA21567"), "01.019.000", eboot) &&
	          !TemporalStatus::CertifiedFsrTitle(astro, "01.019.000", eboot) &&
	          !TemporalStatus::CertifiedFsrTitle(astro, v107, "other.elf") &&
	          !TemporalStatus::CertifiedFsrTitle({}, {}, eboot),
	      "other titles, versions and executables are not certified");
	TemporalStatus::FsrPackage  fsr_package;
	TemporalStatus::DlssPackage dlss_package;
	TemporalStatus::ParseBuildInfo(
	    R"({"fsr3":{"bundled":true,"sdk":"1.1.4","gpu_probe_status":"not_run","installed_state":"updated"},
	        "dlss":{"prototype_bundled":true,"sdk":"310.9.16","gpu_execution":"not_run"}})",
	    &fsr_package, &dlss_package);
	check(fsr_package.sdk == "1.1.4" && fsr_package.gpu_probe == "not_run" &&
	          fsr_package.installed_state == "updated" && dlss_package.sdk == "310.9.16" &&
	          dlss_package.gpu_execution == "not_run",
	      "BUILD-INFO.json package fields are read");
	TemporalStatus::FsrPackage unparsed;
	TemporalStatus::ParseBuildInfo("not json", &unparsed, nullptr);
	check(unparsed.sdk.isEmpty() && unparsed.gpu_probe.isEmpty(),
	      "invalid BUILD-INFO.json is ignored");
	auto status = TemporalStatus::EvaluateFsr(fsr_package, false, astro, v107, eboot, false);
	check(status.state == FsrState::PackageMissing && !status.controls_enabled,
	      "without the library FSR controls are disabled");
	status = TemporalStatus::EvaluateFsr(fsr_package, false, astro, v107, eboot, true);
	check(status.state == FsrState::PackageMissing && status.controls_enabled,
	      "saved FSR options stay editable so they can be cleared");
	fsr_package.library = true;
	status = TemporalStatus::EvaluateFsr(fsr_package, false, astro, v107, eboot, false);
	check(status.state == FsrState::Experimental && status.controls_enabled &&
	          status.text.contains("not run") && status.text.contains("original image") &&
	          !status.text.contains("passed"),
	      "the certified title is experimental, with an honest probe state and fallback");
	fsr_package.gpu_probe = QStringLiteral("passed");
	status = TemporalStatus::EvaluateFsr(fsr_package, false, astro, v107, eboot, false);
	check(status.text.contains("not gameplay"),
	      "a passed offscreen probe is not called gameplay proof");
	status = TemporalStatus::EvaluateFsr(fsr_package, false, QStringLiteral("PPSA21567"),
	                                     "01.019.000", eboot, false);
	check(status.state == FsrState::UnsupportedTitle && !status.controls_enabled &&
	          status.text.contains("PPSA21567 01.019.000"),
	      "an uncertified title reports the actual title/version and keeps FSR off");
	status = TemporalStatus::EvaluateFsr(fsr_package, true, {}, {}, eboot, false);
	check(status.state == FsrState::GlobalSettings && status.controls_enabled &&
	          status.text.contains("PPSA21564 01.007.000"),
	      "global FSR defaults name the only certified title");
	auto launch = TemporalStatus::EffectiveFsrLaunch(fsr_package, astro, v107, eboot,
	                                                 QStringLiteral("quality"), true);
	check(launch.upscale == "quality" && launch.frame_generation && launch.reason.isEmpty(),
	      "the certified title receives the requested FSR options");
	launch =
	    TemporalStatus::EffectiveFsrLaunch(fsr_package, QStringLiteral("PPSA21567"), "01.019.000",
	                                       eboot, QStringLiteral("quality"), true);
	check(launch.upscale == "off" && !launch.frame_generation &&
	          launch.reason.contains("PPSA21567"),
	      "an uncertified title starts with FSR off and a reason");
	launch = TemporalStatus::EffectiveFsrLaunch(fsr_package, QStringLiteral("PPSA21567"),
	                                            "01.019.000", eboot, QStringLiteral("off"), false);
	check(launch.upscale == "off" && !launch.frame_generation && launch.reason.isEmpty(),
	      "FSR that is not requested needs no reason");
	fsr_package.library = false;
	launch = TemporalStatus::EffectiveFsrLaunch(fsr_package, astro, v107, eboot, {}, true);
	check(!launch.frame_generation && launch.reason.contains("not installed"),
	      "a missing library starts with FSR off");
	check(TemporalStatus::DlssStatusText(dlss_package).startsWith("DLSS: not available in games."),
	      "no DLSS package is never available in games");
	dlss_package.checks  = true;
	const auto dlss_text = TemporalStatus::DlssStatusText(dlss_package);
	check(dlss_text.startsWith("DLSS: not available in games.") && dlss_text.contains("310.9.16") &&
	          dlss_text.contains("no DLSS GPU work has run") && !dlss_text.contains("active"),
	      "installed DLSS link checks are never reported as an active game feature");
	dlss_package.gpu_execution = QStringLiteral("offscreen_probe_passed");
	const auto probed          = TemporalStatus::DlssStatusText(dlss_package);
	check(probed.startsWith("DLSS: not available in games.") && probed.contains("offscreen") &&
	          probed.contains("not implemented"),
	      "a passed DLSS offscreen probe is still not reported as a game feature");
	QTemporaryDir runtime;
	check(runtime.isValid() && QDir(runtime.path()).mkpath("Experiments/FSR3"),
	      "create an isolated runtime folder");
	QFile(runtime.filePath("Experiments/FSR3/amd_fidelityfx_vk.dll")).open(QIODevice::WriteOnly);
	const auto read = TemporalStatus::ReadFsrPackage(runtime.path());
	check(read.library && !read.probe && read.sdk.isEmpty(),
	      "the runtime package files are detected without a manifest");
	check(!TemporalStatus::ReadDlssPackage(runtime.path()).checks,
	      "an absent DLSS package is detected");
	return failures == 0 ? 0 : 1;
}
