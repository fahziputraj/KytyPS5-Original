#pragma once

#include <QString>

// What the launcher can establish about FSR and DLSS before a launch. It distinguishes an
// installed package from an eligible title; GPU features, scene inputs, dispatch and generated
// presentation are decided by the emulator at run time and are never claimed here. DLSS is never
// reported as available in games: the bundled files are CPU/stub/link development checks only.
namespace TemporalStatus {

// The one title/version/executable whose temporal inputs are certified
// (renderDraw.cpp CertifiedTemporalDraw); APP_VER is appVersion, else contentVersion.
bool CertifiedFsrTitle(const QString& title_id, const QString& version, const QString& elf);

struct FsrPackage {
	bool    library = false; // Experiments/FSR3/amd_fidelityfx_vk.dll, the path the emulator loads
	bool    probe   = false; // Experiments/FSR3/fsr3_vulkan_probe.exe
	QString sdk;             // BUILD-INFO.json fsr3.sdk
	QString gpu_probe;       // BUILD-INFO.json fsr3.gpu_probe_status: passed | not_run | ""
	QString installed_state; // BUILD-INFO.json fsr3.installed_state: updated | retained | ...
};

struct DlssPackage {
	bool    checks = false;  // Experiments/DLSS development checks are installed
	QString sdk;             // BUILD-INFO.json dlss.sdk
	QString gpu_execution;   // BUILD-INFO.json dlss.gpu_execution
	QString installed_state; // BUILD-INFO.json dlss.installed_state
};

// Reads the package files and BUILD-INFO.json of the runtime folder.
FsrPackage  ReadFsrPackage(const QString& runtime_dir);
DlssPackage ReadDlssPackage(const QString& runtime_dir);
// The same, from BUILD-INFO.json bytes (empty when missing or invalid).
void ParseBuildInfo(const QByteArray& json, FsrPackage* fsr, DlssPackage* dlss);

enum class FsrState {
	PackageMissing,   // no AMD library: FSR is off, original presentation
	GlobalSettings,   // global defaults: apply only to a certified title
	UnsupportedTitle, // no certified adapter for this title/version: original presentation
	Experimental,     // certified title and library: run-time checks decide, with fallback
};

struct FsrStatus {
	FsrState state            = FsrState::PackageMissing;
	bool     controls_enabled = false;
	QString  text;
};

// `selected` is whether FSR options are currently set, so they can still be turned off.
FsrStatus EvaluateFsr(const FsrPackage& package, bool global_settings, const QString& title_id,
                      const QString& version, const QString& elf, bool selected);

struct FsrLaunch {
	QString upscale          = QStringLiteral("off");
	bool    frame_generation = false;
	QString reason; // why requested options were not passed; empty when they were
};

// The FSR options passed to the emulator. Options for an uncertified title or a missing library
// are not passed, so the emulator keeps its native swapchain instead of an idle FSR one.
FsrLaunch EffectiveFsrLaunch(const FsrPackage& package, const QString& title_id,
                             const QString& version, const QString& elf, const QString& upscale,
                             bool frame_generation);

QString DlssStatusText(const DlssPackage& package);

} // namespace TemporalStatus
