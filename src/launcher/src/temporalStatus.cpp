#include "temporalStatus.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace TemporalStatus {

static QString Tr(const char* text) {
	return QCoreApplication::translate("TemporalStatus", text);
}

bool CertifiedFsrTitle(const QString& title_id, const QString& version, const QString& elf) {
	return title_id == QStringLiteral("PPSA21564") && version == QStringLiteral("01.007.000") &&
	       elf.compare(QStringLiteral("eboot.bin"), Qt::CaseInsensitive) == 0;
}

void ParseBuildInfo(const QByteArray& json, FsrPackage* fsr, DlssPackage* dlss) {
	QJsonParseError error {};
	const auto      document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return;
	}
	const auto root = document.object();
	if (fsr != nullptr) {
		const auto section   = root.value(QStringLiteral("fsr3")).toObject();
		fsr->sdk             = section.value(QStringLiteral("sdk")).toString();
		fsr->gpu_probe       = section.value(QStringLiteral("gpu_probe_status")).toString();
		fsr->installed_state = section.value(QStringLiteral("installed_state")).toString();
	}
	if (dlss != nullptr) {
		const auto section    = root.value(QStringLiteral("dlss")).toObject();
		dlss->sdk             = section.value(QStringLiteral("sdk")).toString();
		dlss->gpu_execution   = section.value(QStringLiteral("gpu_execution")).toString();
		dlss->installed_state = section.value(QStringLiteral("installed_state")).toString();
	}
}

static QByteArray ReadBuildInfo(const QString& runtime_dir) {
	QFile file(QDir(runtime_dir).filePath(QStringLiteral("BUILD-INFO.json")));
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
}

FsrPackage ReadFsrPackage(const QString& runtime_dir) {
	FsrPackage package;
	const QDir dir(runtime_dir);
	package.library =
	    QFileInfo::exists(dir.filePath(QStringLiteral("Experiments/FSR3/amd_fidelityfx_vk.dll")));
	package.probe =
	    QFileInfo::exists(dir.filePath(QStringLiteral("Experiments/FSR3/fsr3_vulkan_probe.exe")));
	ParseBuildInfo(ReadBuildInfo(runtime_dir), &package, nullptr);
	return package;
}

DlssPackage ReadDlssPackage(const QString& runtime_dir) {
	DlssPackage package;
	const QDir  dir(runtime_dir);
	package.checks =
	    QFileInfo::exists(dir.filePath(QStringLiteral("Experiments/DLSS/dlss_backend_tests.exe")));
	ParseBuildInfo(ReadBuildInfo(runtime_dir), nullptr, &package);
	return package;
}

static QString ProbeText(const FsrPackage& package) {
	if (package.gpu_probe == QStringLiteral("passed")) {
		return Tr("This build's offscreen GPU probe passed (synthetic frames, not gameplay).");
	}
	if (package.installed_state == QStringLiteral("retained")) {
		return Tr("The FSR package was retained from an earlier build; rerun its GPU test.");
	}
	return Tr("The offscreen GPU probe was not run for this build.");
}

FsrStatus EvaluateFsr(const FsrPackage& package, bool global_settings, const QString& title_id,
                      const QString& version, const QString& elf, bool selected) {
	FsrStatus  status;
	const auto fallback = Tr("FSR stays off and the game's original presentation is used.");
	if (!package.library) {
		status.state = FsrState::PackageMissing;
		// Saved selections can still be cleared.
		status.controls_enabled = selected;
		status.text             = Tr("FSR package not installed (Experiments/FSR3). ") + fallback;
		return status;
	}
	const auto limits = Tr("Compatibility mode: retains the game's TAA, post-processing and HUD; "
	                       "UI/transparent effects may show artifacts; it does not reduce guest "
	                       "rendering cost. ");
	if (global_settings) {
		status.state            = FsrState::GlobalSettings;
		status.controls_enabled = true;
		status.text = Tr("Applies only to titles with a certified temporal adapter (ASTRO BOT "
		                 "PPSA21564 01.007.000). Other games keep their original presentation. ") +
		              limits + ProbeText(package);
		return status;
	}
	if (!CertifiedFsrTitle(title_id, version, elf)) {
		status.state            = FsrState::UnsupportedTitle;
		status.controls_enabled = selected;
		const auto name =
		    title_id.isEmpty() ? Tr("this game") : title_id + QLatin1Char(' ') + version;
		status.text =
		    Tr("No certified FSR adapter for %1. ").arg(name) + fallback +
		    (selected ? Tr(" Saved FSR options are not passed to the emulator.") : QString {});
		return status;
	}
	status.state            = FsrState::Experimental;
	status.controls_enabled = true;
	status.text = Tr("Experimental ASTRO BOT 01.007 adapter. The emulator verifies GPU features, "
	                 "scene guides and each frame at run time; on any failure it suspends FSR and "
	                 "shows the original image. ") +
	              limits + ProbeText(package) + Tr(" Changes apply on next launch.");
	return status;
}

FsrLaunch EffectiveFsrLaunch(const FsrPackage& package, const QString& title_id,
                             const QString& version, const QString& elf, const QString& upscale,
                             bool frame_generation) {
	FsrLaunch  launch;
	const bool requested =
	    frame_generation || (!upscale.isEmpty() && upscale != QStringLiteral("off"));
	if (!requested) {
		return launch;
	}
	if (!package.library) {
		launch.reason = Tr("FSR options ignored: the FSR package is not installed.");
		return launch;
	}
	if (!CertifiedFsrTitle(title_id, version, elf)) {
		launch.reason =
		    Tr("FSR options ignored: no certified adapter for %1 %2.").arg(title_id, version);
		return launch;
	}
	launch.upscale          = upscale.isEmpty() ? QStringLiteral("off") : upscale;
	launch.frame_generation = frame_generation;
	return launch;
}

QString DlssStatusText(const DlssPackage& package) {
	if (!package.checks) {
		return Tr("DLSS: not available in games. No DLSS development package is installed.");
	}
	auto text = Tr("DLSS: not available in games. Experiments/DLSS holds CPU, stub and link "
	               "development checks only; no DLSS GPU work has run");
	if (package.gpu_execution == QStringLiteral("passed")) {
		text = Tr("DLSS: not available in games. Its offscreen development probe passed on this "
		          "build's test GPU; game integration is not implemented");
	}
	if (!package.sdk.isEmpty()) {
		text += Tr(" (SDK %1)").arg(package.sdk);
	}
	return text + QLatin1Char('.');
}

} // namespace TemporalStatus
