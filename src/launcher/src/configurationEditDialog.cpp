#include "configurationEditDialog.h"

#include "cheatFile.h"
#include "common/emulatorConfig.h"
#include "configuration.h"
#include "mandatoryLineEdit.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtAlgorithms>

#include <SDL3/SDL.h>
#include <algorithm>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#include <QVulkanWindow>
#endif

#include "ui_configuration_edit_dialog.h"

constexpr char SETTINGS_CFG_DIALOG[]               = "ConfigurationEditDialog";
constexpr char SETTINGS_CFG_LAST_GEOMETRY[]        = "geometry";
constexpr int  GLOBAL_SETTINGS_GAME_DIRS_MIN_WIDTH = 400;

static void UpdateControllerColorButton(QPushButton* button, const QString& hex) {
	const QColor color(hex);
	if (!color.isValid()) {
		button->setProperty("controllerColor", QString {});
		button->setText(QObject::tr("Custom"));
		button->setStyleSheet({});
		return;
	}
	const auto normalized = color.name(QColor::HexRgb);
	button->setProperty("controllerColor", normalized);
	button->setText(normalized.toUpper());
	button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
	                          .arg(normalized, color.lightness() < 128 ? QStringLiteral("white")
	                                                                   : QStringLiteral("black")));
}

const QStringList CONSOLE_LANGUAGE_NAMES = {
    "Japanese",
    "English (United States)",
    "French (France)",
    "Spanish (Spain)",
    "German",
    "Italian",
    "Dutch",
    "Portuguese (Portugal)",
    "Russian",
    "Korean",
    "Chinese (Traditional)",
    "Chinese (Simplified)",
    "Finnish",
    "Swedish",
    "Danish",
    "Norwegian",
    "Polish",
    "Portuguese (Brazil)",
    "English (United Kingdom)",
    "Turkish",
    "Spanish (Latin America)",
    "Arabic",
    "French (Canada)",
    "Czech",
    "Hungarian",
    "Greek",
    "Romanian",
    "Thai",
    "Vietnamese",
    "Indonesian",
};

static QString NormalizeGameDirectory(const QString& dir) {
	const auto trimmed = dir.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}

	return QDir::cleanPath(QDir(trimmed).absolutePath());
}

static QString GameDirectoryKey(const QString& dir) {
	const auto normalized = NormalizeGameDirectory(dir);
	if (normalized.isEmpty()) {
		return {};
	}

	auto canonical = QFileInfo(normalized).canonicalFilePath();
	if (canonical.isEmpty()) {
		canonical = normalized;
	}
	canonical = QDir::cleanPath(canonical);

#ifdef __linux__
	return canonical;
#else
	return canonical.toCaseFolded();
#endif
}

// The audio mix sliders and the 3D objects switch (rows 1-6 of the Audio group) are global settings like the controller
// group: shown only in the global settings dialog.
struct AudioMixSlider {
	QSlider* slider;
	QLabel*  value;
	int AudioMixSettings::*field;
};

static std::array<AudioMixSlider, 5> AudioMixSliders(Ui::ConfigurationEditDialog& ui) {
	return {{{ui.slider_audio_master, ui.label_audio_master_value, &AudioMixSettings::master},
	         {ui.slider_audio_main, ui.label_audio_main_value, &AudioMixSettings::main},
	         {ui.slider_audio_music, ui.label_audio_music_value, &AudioMixSettings::music},
	         {ui.slider_audio_pad_speaker, ui.label_audio_pad_speaker_value,
	          &AudioMixSettings::pad_speaker},
	         {ui.slider_audio_objects, ui.label_audio_objects_value, &AudioMixSettings::objects}}};
}

static void SetAudioMixVisible(Ui::ConfigurationEditDialog& ui, bool visible) {
	for (int row = 1; row <= 6; row++) {
		ui.audioLayout->setRowVisible(row, visible);
	}
	// The occlusion mode is global too (graphics row 9).
	ui.graphicsLayout->setRowVisible(ui.checkBox_gpu_occlusion, visible);
}

ConfigurationEditDialog::ConfigurationEditDialog(Configuration& info, QWidget* parent)
    : QDialog(parent, Qt::WindowCloseButtonHint), m_ui(new Ui::ConfigurationEditDialog),
      m_info(info) {
	m_ui->setupUi(this);
	SetAudioMixVisible(*m_ui, false);
	for (const auto& mix: AudioMixSliders(*m_ui)) {
		mix.slider->setMaximum(static_cast<int>(Config::MAX_AUDIO_VOLUME));
		connect(mix.slider, &QSlider::valueChanged, this,
		        [label = mix.value](int value) {
			label->setText(ConfigurationEditDialog::tr("%1%").arg(value));
		});
	}
	connect(m_ui->checkBox_audio_objects, &QCheckBox::toggled, this, [this](bool on) {
		m_ui->slider_audio_objects->setEnabled(on);
		m_ui->label_audio_objects_value->setEnabled(on);
	});
	setMinimumWidth(width());
	InitGameDirectories();
	m_ui->controller_group->setVisible(false);
	connect(m_ui->button_controller_motion_refresh, &QPushButton::clicked, this,
	        &ConfigurationEditDialog::DetectMotionControllers);
	m_ui->settings_button->setVisible(false);

	connect(m_ui->ok_button, &QPushButton::clicked, this, &ConfigurationEditDialog::save);
	connect(m_ui->cancel_button, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_ui->clear_button, &QPushButton::clicked, this, &ConfigurationEditDialog::clear);
	connect(m_ui->button_controller_color, &QPushButton::clicked, this, [this]() {
		const auto current = m_ui->button_controller_color->property("controllerColor").toString();
		const auto initial =
		    current.isEmpty() ? QColor(QStringLiteral("#0070d1")) : QColor(current);
		const auto color = QColorDialog::getColor(initial, this, tr("DualSense lightbar color"));
		if (color.isValid()) {
			const auto hex = color.name(QColor::HexRgb);
			UpdateControllerColorButton(m_ui->button_controller_color, hex);
			emit PreviewControllerColor(hex);
		}
	});
	connect(m_ui->button_controller_color_reset, &QPushButton::clicked, this, [this]() {
		UpdateControllerColorButton(m_ui->button_controller_color, {});
		emit PreviewControllerColor({});
	});
	connect(m_ui->slider_controller_vibration, &QSlider::valueChanged, this, [this](int value) {
		m_ui->label_controller_vibration_value->setText(tr("%1%").arg(value));
	});
	connect(m_ui->slider_controller_volume, &QSlider::valueChanged, this, [this](int value) {
		m_ui->label_controller_volume_value->setText(tr("%1%").arg(value));
	});
	connect(m_ui->comboBox_shader_log_direction, &QComboBox::currentTextChanged, this,
	        [this](const QString& text) {
		        auto log = TextToEnum<Configuration::LogDirection>(text);
		        m_ui->lineEdit_shader_log_folder->setEnabled(
		            log == Configuration::LogDirection::File);
	        });
	connect(m_ui->checkBox_cmd_dump, &QCheckBox::toggled, this,
	        [this](bool flag) { m_ui->lineEdit_cmd_dump_folder->setEnabled(flag); });
	connect(m_ui->comboBox_printf_direction, &QComboBox::currentTextChanged, this,
	        [this](const QString& text) {
		        auto log = TextToEnum<Configuration::LogDirection>(text);
		        m_ui->lineEdit_printf_file->setEnabled(log == Configuration::LogDirection::File);
	        });

	setSizeGripEnabled(true);

	restoreGeometry(g_last_geometry);

	Init(info);
	InitPacingControls();
}

QByteArray ConfigurationEditDialog::g_last_geometry;

ConfigurationEditDialog::~ConfigurationEditDialog() {
	delete m_ui;
}

void ConfigurationEditDialog::WriteSettings(QSettings& s) {
	s.beginGroup(SETTINGS_CFG_DIALOG);

	if (!g_last_geometry.isEmpty()) {
		s.setValue(SETTINGS_CFG_LAST_GEOMETRY, g_last_geometry);
	}

	s.endGroup();
}

void ConfigurationEditDialog::ReadSettings(QSettings& s) {
	s.beginGroup(SETTINGS_CFG_DIALOG);

	g_last_geometry = s.value(SETTINGS_CFG_LAST_GEOMETRY, g_last_geometry).toByteArray();

	s.endGroup();
}

template <class T>
static void ListInit(QComboBox* combo, T value) {
	combo->clear();
	combo->addItems(EnumToList<T>());
	combo->setCurrentText(EnumToText(value));
}

void ConfigurationEditDialog::Init(const Configuration& info) {
	if (m_fsr_upscale)
		m_fsr_upscale->setCurrentIndex(std::max(0, m_fsr_upscale->findData(info.fsr_upscale)));
	if (m_fsr_generation) m_fsr_generation->setChecked(info.fsr_frame_generation);
	m_ui->lineEdit_user_name->setMaxLength(static_cast<int>(Config::MAX_USER_NAME_LENGTH));
	m_ui->lineEdit_user_name->setText(info.user_name);
	m_ui->spinBox_user_id->setValue(info.user_id);
	UpdateControllerColorButton(m_ui->button_controller_color, info.controller.color);
	m_ui->slider_controller_vibration->setValue(info.controller.vibration_intensity);
	m_ui->slider_controller_volume->setValue(info.controller.speaker_volume);
	m_ui->comboBox_controller_motion->clear();
	m_ui->comboBox_controller_motion->addItem(tr("Active gamepad (native SDL)"),
	                                          QStringLiteral("active"));
	m_ui->comboBox_controller_motion->addItem(tr("Off (compare CPU usage)"), QStringLiteral("off"));
	if (m_ui->comboBox_controller_motion->findData(info.controller.motion_source) < 0) {
		m_ui->comboBox_controller_motion->addItem(
		    tr("Saved device: %1").arg(info.controller.motion_source),
		    info.controller.motion_source);
	}
	m_ui->comboBox_controller_motion->setCurrentIndex(
	    m_ui->comboBox_controller_motion->findData(info.controller.motion_source));
	m_ui->comboBox_controller_motion_rate->clear();
	for (const auto rate: {125u, 250u, 500u, 1000u, 0u}) {
		m_ui->comboBox_controller_motion_rate->addItem(
		    rate == 0 ? tr("Raw sensor reports") : tr("%1 Hz").arg(rate), rate);
	}
	if (m_ui->comboBox_controller_motion_rate->findData(info.controller.motion_rate) < 0) {
		m_ui->comboBox_controller_motion_rate->addItem(tr("%1 Hz").arg(info.controller.motion_rate),
		                                               info.controller.motion_rate);
	}
	m_ui->comboBox_controller_motion_rate->setCurrentIndex(
	    m_ui->comboBox_controller_motion_rate->findData(info.controller.motion_rate));
	for (const auto& mix: AudioMixSliders(*m_ui)) {
		mix.slider->setValue(info.audio_mix.*mix.field);
	}
	m_ui->checkBox_audio_objects->setChecked(info.audio_mix.objects_enabled);
	m_ui->slider_audio_objects->setEnabled(info.audio_mix.objects_enabled);
	m_ui->label_audio_objects_value->setEnabled(info.audio_mix.objects_enabled);
	m_ui->checkBox_gpu_occlusion->setChecked(info.gpu_occlusion_accurate);
	auto* microphone = m_ui->comboBox_audio_input_device;
	microphone->clear();
	microphone->addItem(tr("None"), QString {});
	microphone->setToolTip(tr("Microphone used by games. None supplies silence."));
	if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		int                device_count = 0;
		SDL_AudioDeviceID* devices      = SDL_GetAudioRecordingDevices(&device_count);
		for (int i = 0; i < device_count; i++) {
			if (const auto* device = SDL_GetAudioDeviceName(devices[i]); device != nullptr) {
				const auto name = QString::fromUtf8(device);
				if (microphone->findData(name) < 0) {
					microphone->addItem(name, name);
				}
			}
		}
		SDL_free(devices);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	} else {
		microphone->setToolTip(tr("Microphones could not be listed: %1")
		                           .arg(QString::fromUtf8(SDL_GetError())));
	}
	if (microphone->findData(info.audio_input_device) < 0) {
		microphone->addItem(tr("%1 (unavailable)").arg(info.audio_input_device),
		                    info.audio_input_device);
	}
	microphone->setCurrentIndex(microphone->findData(info.audio_input_device));
	ListInit(m_ui->comboBox_screen_resolution, info.screen_resolution);
	ListInit(m_ui->comboBox_present_mode, info.present_mode);
	const auto add_mode = [](QComboBox* box, const QString& preset, const QString& independent,
	                         const QString& fixed, const QString& selected) {
		box->clear();
		box->addItem(preset, QStringLiteral("preset"));
		box->addItem(independent, QStringLiteral("independent"));
		box->addItem(fixed, QStringLiteral("fixed"));
		box->setCurrentIndex(std::max(0, box->findData(selected)));
	};
	add_mode(m_ui->comboBox_presentation_clock, tr("Preset"), tr("Independent (variable FPS)"),
	         tr("Fixed (legacy)"), info.presentation_clock);
	m_ui->spinBox_presentation_fps->setValue(info.presentation_fps);
	auto* cpu_mode = m_ui->comboBox_cpu_hybrid;
	cpu_mode->clear();
	cpu_mode->addItem(tr("Preset"), QStringLiteral("preset"));
	cpu_mode->addItem(tr("P-cores for graphics, E-cores for background"), QStringLiteral("on"));
	cpu_mode->addItem(tr("E-cores for background only"), QStringLiteral("background"));
	cpu_mode->addItem(tr("System default"), QStringLiteral("off"));
	cpu_mode->setCurrentIndex(std::max(0, cpu_mode->findData(info.cpu_hybrid)));
	m_ui->comboBox_gpu->clear();
	m_ui->comboBox_gpu->addItem(tr("Auto"));
	// Keep Auto when Qt is built without Vulkan support.
#if QT_CONFIG(vulkan)
#if defined(__APPLE__)
	if (!qEnvironmentVariableIsSet("QT_VULKAN_LIB")) {
		const auto base    = QCoreApplication::applicationDirPath();
		auto       library = base + "/libMoltenVK.dylib";
		if (!QFileInfo::exists(library)) {
			library = base + "/../Frameworks/libMoltenVK.dylib";
		}
		if (QFileInfo::exists(library)) {
			qputenv("QT_VULKAN_LIB", library.toUtf8());
		}
	}
#endif
	QVulkanInstance instance;
	instance.setApiVersion(QVersionNumber(1, 3, 0));
#if !defined(__APPLE__) && QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
	instance.setFlags(QVulkanInstance::NoPortabilityDrivers);
#endif
	if (instance.create()) {
		QVulkanWindow window;
		window.setVulkanInstance(&instance);
		for (const auto& device: window.availablePhysicalDevices()) {
			m_ui->comboBox_gpu->addItem(QString::fromUtf8(device.deviceName));
		}
	}
#endif
	m_ui->comboBox_gpu->setCurrentIndex(
	    info.gpu_index >= 0 && info.gpu_index < m_ui->comboBox_gpu->count() - 1 ? info.gpu_index + 1
	                                                                            : 0);
	m_ui->checkBox_fullscreen->setChecked(info.fullscreen_enabled);
	m_ui->checkBox_hide_cursor->setChecked(info.hide_cursor_enabled);
	m_ui->checkBox_readback->setChecked(info.readback_linear_images);
	m_ui->checkBox_sync_raw_image_buffers->setChecked(info.sync_raw_image_buffers);
	m_ui->checkBox_tessellation->setChecked(info.tessellation_enabled);
	m_ui->checkBox_skip_notice_screen->setChecked(info.skip_notice_screen);
	m_ui->checkBox_trophy_notifications->setChecked(info.trophy_enabled);
	m_ui->spinBox_vblank_frequency->setValue(info.vblank_frequency);
	m_ui->comboBox_console_language->clear();
	m_ui->comboBox_console_language->addItems(CONSOLE_LANGUAGE_NAMES);
	m_ui->comboBox_console_language->setCurrentIndex(
	    info.console_language >= 0 && info.console_language < CONSOLE_LANGUAGE_NAMES.size()
	        ? info.console_language
	        : Configuration::DEFAULT_CONSOLE_LANGUAGE);
	m_ui->checkBox_shader_validation->setChecked(info.shader_validation_enabled);
	m_ui->checkBox_vulkan_validation->setChecked(info.vulkan_validation_enabled);
	m_ui->checkBox_renderdoc_capture->setChecked(info.renderdoc_enabled);
	// Partly checked: automatic (on unless the CPU is an AMD).
	m_ui->checkBox_amd_cpu->setTristate(true);
	m_ui->checkBox_amd_cpu->setCheckState(info.amd_cpu_mode == QStringLiteral("on")    ? Qt::Checked
	                                      : info.amd_cpu_mode == QStringLiteral("off") ? Qt::Unchecked
	                                                                                   : Qt::PartiallyChecked);
#if defined(__APPLE__)
	m_ui->checkBox_amd_cpu->setVisible(false);
#endif
#if defined(_WIN32)
	m_ui->checkBox_red_zone_protection->setChecked(info.red_zone_protection_enabled);
#else
	m_ui->checkBox_red_zone_protection->setVisible(false);
#endif
	ListInit(m_ui->comboBox_shader_optimization_type, info.shader_optimization_type);
	ListInit(m_ui->comboBox_shader_log_direction, info.shader_log_direction);
	m_ui->lineEdit_shader_log_folder->setText(info.shader_log_folder);
	m_ui->lineEdit_shader_log_folder->setEnabled(info.shader_log_direction ==
	                                             Configuration::LogDirection::File);
	m_ui->checkBox_cmd_dump->setChecked(info.command_buffer_dump_enabled);
	m_ui->lineEdit_cmd_dump_folder->setText(info.command_buffer_dump_folder);
	m_ui->lineEdit_cmd_dump_folder->setEnabled(info.command_buffer_dump_enabled);
	ListInit(m_ui->comboBox_printf_direction, info.printf_direction);
	m_ui->lineEdit_printf_file->setText(info.printf_output_file);
	m_ui->lineEdit_printf_file->setEnabled(info.printf_direction ==
	                                       Configuration::LogDirection::File);
	m_ui->checkBox_profiler->setChecked(info.profiler_enabled);
}

void ConfigurationEditDialog::InitGameDirectories() {
	m_game_dirs_group = new QGroupBox(tr("Game folders"), this);

	auto* group_layout = new QVBoxLayout(m_game_dirs_group);
	group_layout->setContentsMargins(8, 8, 8, 8);
	group_layout->setSpacing(6);

	m_game_dirs_list = new QListWidget(m_game_dirs_group);
	m_game_dirs_list->setMinimumHeight(120);
	m_game_dirs_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	group_layout->addWidget(m_game_dirs_list);

	auto* button_layout = new QHBoxLayout;
	button_layout->setContentsMargins(0, 0, 0, 0);
	button_layout->setSpacing(4);

	auto* add_button = new QToolButton(m_game_dirs_group);
	add_button->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
	add_button->setToolTip(tr("Add game folder"));
	add_button->setAutoRaise(true);
	button_layout->addWidget(add_button);

	m_remove_game_dir_button = new QToolButton(m_game_dirs_group);
	m_remove_game_dir_button->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
	m_remove_game_dir_button->setToolTip(tr("Remove selected game folders"));
	m_remove_game_dir_button->setAutoRaise(true);
	button_layout->addWidget(m_remove_game_dir_button);

	button_layout->addStretch(1);
	group_layout->addLayout(button_layout);

	m_ui->gridLayout->addWidget(m_game_dirs_group, 1, 0, 1, 1);
	m_game_dirs_group->setVisible(false);

	connect(add_button, &QToolButton::clicked, this, &ConfigurationEditDialog::add_game_directory);
	connect(m_remove_game_dir_button, &QToolButton::clicked, this,
	        &ConfigurationEditDialog::remove_selected_game_directories);
	connect(m_game_dirs_list, &QListWidget::itemSelectionChanged, this,
	        &ConfigurationEditDialog::update_game_directory_buttons);

	update_game_directory_buttons();
}

void ConfigurationEditDialog::SetGlobalSettings(const QStringList& dirs) {
	m_global_settings = true;
	m_clock_profile->setEnabled(false);
	m_clock_status->setText(
	    tr("Choose a validated simulation clock in each game's settings. "
	       "Variable frame delivery alone does not correct every game's pace."));
	auto* menu        = new QMenu(m_ui->settings_button);
	menu->addAction(tr("Import..."), this, &ConfigurationEditDialog::ImportGameSettings);
	menu->addAction(tr("Export..."), this, &ConfigurationEditDialog::ExportGameSettings);
	m_ui->settings_button->setMenu(menu);
	m_ui->settings_button->setVisible(true);
	m_game_dirs_list->clear();
	m_game_dirs_group->setMinimumWidth(GLOBAL_SETTINGS_GAME_DIRS_MIN_WIDTH);

	for (const auto& dir: dirs) {
		AddGameDirectoryItem(dir);
	}

	m_game_dirs_group->setVisible(true);
	m_ui->controller_group->setVisible(true);
	SetAudioMixVisible(*m_ui, true);
	update_game_directory_buttons();
	layout()->activate();
	resize(size().expandedTo(minimumSizeHint()));
}

QStringList ConfigurationEditDialog::GetGameDirectories() const {
	QStringList dirs;
	if (!m_global_settings) {
		return dirs;
	}

	for (int index = 0; index < m_game_dirs_list->count(); index++) {
		auto* item = m_game_dirs_list->item(index);
		dirs.append(item->text());
	}

	return dirs;
}

void ConfigurationEditDialog::InitPacingControls() {
	auto* group = new QGroupBox(tr("Pacing && frame generation"), this);
	group->setObjectName(QStringLiteral("pacing_group"));
	auto* form = new QFormLayout(group);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_clock_profile = new QComboBox(group);
	m_clock_profile->setObjectName(QStringLiteral("comboBox_simulation_clock"));
	m_clock_profile->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_clock_profile->setMinimumContentsLength(18);
	m_clock_profile->addItem(tr("Keep local cheat selection"), QStringLiteral("keep"));
	form->addRow(tr("Simulation pace:"), m_clock_profile);
	m_clock_status = new QLabel(group);
	m_clock_status->setWordWrap(true);
	m_clock_status->setText(tr("No validated simulation-clock profile for this title/version. "
	                           "Frame delivery and CPU scheduling are available above."));
	form->addRow(m_clock_status);
	m_fsr_upscale = new QComboBox(group);
	m_fsr_upscale->setObjectName(QStringLiteral("comboBox_fsr_upscale"));
	m_fsr_upscale->addItem(tr("Off"), QStringLiteral("off"));
	m_fsr_upscale->addItem(tr("Native AA (1x)"), QStringLiteral("native"));
	m_fsr_upscale->addItem(tr("Quality (1.5x)"), QStringLiteral("quality"));
	m_fsr_upscale->addItem(tr("Balanced (1.7x)"), QStringLiteral("balanced"));
	m_fsr_upscale->addItem(tr("Performance (2x)"), QStringLiteral("performance"));
	m_fsr_upscale->setCurrentIndex(std::max(0, m_fsr_upscale->findData(m_info.fsr_upscale)));
	form->addRow(tr("FSR upscaling:"), m_fsr_upscale);
	m_fsr_generation = new QCheckBox(tr("FSR frame generation (experimental)"), group);
	m_fsr_generation->setObjectName(QStringLiteral("checkBox_fsr_frame_generation"));
	m_fsr_generation->setChecked(m_info.fsr_frame_generation);
	form->addRow(m_fsr_generation);
	auto* fg_status =
	    new QLabel(tr("Astro Bot 01.007 temporal adapter. Requires the bundled AMD Vulkan library. "
	                  "Missing guides suspend FSR automatically. This compatibility mode retains "
	                  "the game's rendering, "
	                  "TAA and HUD; UI/transparent effects may show artifacts. It does not reduce "
	                  "guest rendering cost. "
	                  "Changes apply on next launch."),
	               group);
	fg_status->setObjectName(QStringLiteral("label_fsr_status"));
	fg_status->setWordWrap(true);
	form->addRow(fg_status);
	auto* probe = new QPushButton(tr("Run FSR3 GPU test"), group);
	probe->setObjectName(QStringLiteral("button_fsr_gpu_test"));
	const auto base = QCoreApplication::applicationDirPath();
	const bool fsr_installed = QFileInfo::exists(base + "/Experiments/FSR3/amd_fidelityfx_vk.dll");
	m_fsr_upscale->setEnabled(fsr_installed);
	m_fsr_generation->setEnabled(fsr_installed);
	probe->setEnabled(QFileInfo::exists(base + "/Experiments/FSR3/fsr3_vulkan_probe.exe") &&
	                  QFileInfo::exists(base + "/Experiments/FSR3/amd_fidelityfx_vk.dll"));
	if (!probe->isEnabled()) {
		probe->setToolTip(tr("The optional FSR3 test package is not installed."));
	}
	connect(probe, &QPushButton::clicked, this, &ConfigurationEditDialog::RunFsrGpuTest);
	form->addRow(probe);
	m_ui->sectionsLayout->addWidget(group, 3, 1);
	m_clock_profile->setEnabled(false);
	if (m_info.title_id != "PPSA21564" || m_info.gameVersion != "01.007.000" ||
	    m_info.elf != "eboot.bin") {
		return;
	}
	QString error;
	for (const auto& name:
	     {"PPSA21564-01.007-clock30.json", "PPSA21564-01.007-clock-adaptive.json"}) {
		QFile         file(QDir(base).filePath(QStringLiteral("patches/") + QLatin1String(name)));
		QJsonDocument profile;
		if (!file.open(QIODevice::ReadOnly)) {
			error = tr("Bundled clock profiles are missing. Reinstall the complete build.");
			break;
		}
		error = Cheats::ParseDocument(file.readAll(), &profile);
		if (!error.isEmpty()) {
			break;
		}
		error = Cheats::ValidateImport(profile, m_info.title_id, m_info.gameVersion, m_info.elf);
		if (!error.isEmpty()) {
			break;
		}
		m_clock_profiles.append(profile);
	}
	QFile local(Cheats::PlanPath(m_info.title_id));
	if (error.isEmpty() && local.exists()) {
		if (!local.open(QIODevice::ReadOnly)) {
			error = tr("Could not read local cheats.");
		} else {
			m_clock_data = local.readAll();
			error        = Cheats::ParseDocument(m_clock_data, &m_clock_document);
		}
	}
	QJsonDocument validation;
	if (error.isEmpty()) {
		error = Cheats::SelectClockProfile(m_clock_document, m_clock_profiles, {}, &validation);
	}
	if (!error.isEmpty()) {
		m_clock_status->setText(error);
		return;
	}
	m_clock_profile->addItem(tr("Fixed 30 FPS (experimental)"),
	                         QStringLiteral("codex.astro.clock30"));
	m_clock_profile->addItem(tr("Adaptive 30-60 FPS (experimental)"),
	                         QStringLiteral("codex.astro.clock-adaptive"));
	m_clock_profile->addItem(tr("Original clock (bundled patches off)"), QString {});
	QString selected;
	int     enabled = 0;
	for (const auto& value: m_clock_document.object().value("mods").toArray()) {
		const auto mod = value.toObject();
		if (mod.value("enabled").toBool(true) &&
		    m_clock_profile->findData(mod.value("id").toString()) > 0) {
			selected = mod.value("id").toString();
			++enabled;
		}
	}
	m_clock_profile->setCurrentIndex(enabled > 1 ? 0 : m_clock_profile->findData(selected));
	m_clock_available = true;
	m_clock_profile->setEnabled(true);
	connect(m_clock_profile, &QComboBox::currentIndexChanged, this,
	        &ConfigurationEditDialog::UpdateClockControls);
	UpdateClockControls();
}

void ConfigurationEditDialog::UpdateClockControls() {
	const auto id     = m_clock_profile->currentData().toString();
	const bool paired = m_clock_available && !m_global_settings && id != "keep";
	m_ui->comboBox_presentation_clock->setEnabled(!paired);
	m_ui->spinBox_presentation_fps->setEnabled(!paired);
	m_ui->spinBox_vblank_frequency->setEnabled(!paired);
	if (paired) {
		m_ui->comboBox_presentation_clock->setCurrentIndex(
		    m_ui->comboBox_presentation_clock->findData("independent"));
		m_ui->spinBox_presentation_fps->setValue(id == "codex.astro.clock30" ? 30
		                                         : id.isEmpty()              ? 0
		                                                                     : 60);
		m_ui->spinBox_vblank_frequency->setValue(60);
	}
	if (m_clock_available && !m_global_settings) {
		m_clock_status->setText(tr(
		    "ASTRO BOT 01.007 only. Saving pairs the clock with its FPS ceiling and 60 Hz vblank. "
		    "Adaptive correction covers 30-60 FPS; below 30, slowdown can remain. "
		    "Changes apply on the next launch; other cheats are preserved."));
	}
}

void ConfigurationEditDialog::RunFsrGpuTest() {
	QTemporaryDir output;
	if (!output.isValid()) {
		QMessageBox::critical(this, tr("FSR3 GPU test"),
		                      tr("Could not create a temporary output directory."));
		return;
	}
	QDialog dialog(this);
	dialog.setWindowTitle(tr("FSR3 offscreen GPU test — no in-game frame generation"));
	dialog.resize(720, 440);
	auto* layout = new QVBoxLayout(&dialog);
	auto* status = new QLabel(
	    tr("Running six GPU interpolation dispatches. This does not enable game frame generation."),
	    &dialog);
	status->setWordWrap(true);
	layout->addWidget(status);
	auto* log = new QPlainTextEdit(&dialog);
	log->setReadOnly(true);
	log->setMaximumBlockCount(600);
	layout->addWidget(log);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	const auto base = QCoreApplication::applicationDirPath() + "/Experiments/FSR3/";
	process.setWorkingDirectory(base);
	connect(&process, &QProcess::readyReadStandardOutput, &dialog,
	        [&] { log->appendPlainText(QString::fromLocal8Bit(process.readAllStandardOutput())); });
	connect(&process, &QProcess::errorOccurred, &dialog, [&](QProcess::ProcessError) {
		status->setText(tr("GPU test could not complete: %1").arg(process.errorString()));
	});
	connect(&process, &QProcess::finished, &dialog, [&](int code, QProcess::ExitStatus exit) {
		status->setText(code == 0 && exit == QProcess::NormalExit
		                    ? tr("GPU test passed. Select the in-game FSR options in settings for "
		                         "supported games.")
		                    : tr("GPU test failed. See the output below."));
	});
	process.start(base + "fsr3_vulkan_probe.exe",
	              {base + "amd_fidelityfx_vk.dll", output.filePath("generated.ppm")});
	dialog.exec();
	if (process.state() != QProcess::NotRunning) {
		process.kill();
		process.waitForFinished(1000);
	}
}

void ConfigurationEditDialog::DetectMotionControllers() {
	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		m_ui->label_controller_motion_status->setText(
		    tr("Controller detection failed: %1").arg(QString::fromUtf8(SDL_GetError())));
		return;
	}
	const auto saved = m_ui->comboBox_controller_motion->currentData().toString();
	m_ui->comboBox_controller_motion->clear();
	m_ui->comboBox_controller_motion->addItem(tr("Active gamepad (native SDL)"),
	                                          QStringLiteral("active"));
	m_ui->comboBox_controller_motion->addItem(tr("Off (compare CPU usage)"), QStringLiteral("off"));
	QStringList status;
	int         count = 0;
	auto*       ids   = SDL_GetGamepads(&count);
	for (int i = 0; i < count; ++i) {
		auto* pad = SDL_OpenGamepad(ids[i]);
		if (!pad) {
			continue;
		}
		const auto name  = QString::fromUtf8(SDL_GetGamepadName(pad));
		const bool gyro  = SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO);
		const bool accel = SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL);
		status << tr("%1: gyro %2, accelerometer %3")
		              .arg(name, gyro ? tr("yes") : tr("no"), accel ? tr("yes") : tr("no"));
		if (gyro || accel) {
			char guid[33] {};
			SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]), guid, sizeof(guid));
			m_ui->comboBox_controller_motion->addItem(name, QString::fromLatin1(guid));
		}
		SDL_CloseGamepad(pad);
	}
	SDL_free(ids);
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	if (m_ui->comboBox_controller_motion->findData(saved) < 0) {
		m_ui->comboBox_controller_motion->addItem(tr("Saved device (not detected)"), saved);
	}
	m_ui->comboBox_controller_motion->setCurrentIndex(
	    m_ui->comboBox_controller_motion->findData(saved));
	if (status.empty()) {
		status << tr("No supported gamepad detected.");
	}
	status << tr("Xbox/XInput mode needs a separately exposed motion device. A mode setting cannot "
	             "create missing gyro data.");
	m_ui->label_controller_motion_status->setText(status.join(QLatin1Char('\n')));
}

void ConfigurationEditDialog::AddGameDirectoryItem(const QString& dir) {
	const auto normalized = NormalizeGameDirectory(dir);
	const auto key        = GameDirectoryKey(normalized);
	if (normalized.isEmpty() || key.isEmpty()) {
		return;
	}

	for (int index = 0; index < m_game_dirs_list->count(); index++) {
		auto* item = m_game_dirs_list->item(index);
		if (GameDirectoryKey(item->text()) == key) {
			return;
		}
	}

	auto* item = new QListWidgetItem(normalized, m_game_dirs_list);
	item->setToolTip(normalized);
}

void ConfigurationEditDialog::moveEvent(QMoveEvent* event) {
	QDialog::moveEvent(event);
	g_last_geometry = saveGeometry();
}

void ConfigurationEditDialog::resizeEvent(QResizeEvent* event) {
	QDialog::resizeEvent(event);
	g_last_geometry = saveGeometry();
}

static void UpdateInfo(Configuration& info, Ui::ConfigurationEditDialog& ui, bool global_settings) {
	info.user_name = ui.lineEdit_user_name->text().trimmed();
	info.user_id   = ui.spinBox_user_id->value();
	info.audio_input_device = ui.comboBox_audio_input_device->currentData().toString();
	if (global_settings) {
		info.controller.color = ui.button_controller_color->property("controllerColor").toString();
		info.controller.vibration_intensity = ui.slider_controller_vibration->value();
		info.controller.speaker_volume      = ui.slider_controller_volume->value();
		info.controller.motion_source = ui.comboBox_controller_motion->currentData().toString();
		info.controller.motion_rate   = ui.comboBox_controller_motion_rate->currentData().toUInt();
		for (const auto& mix: AudioMixSliders(ui)) {
			info.audio_mix.*mix.field = mix.slider->value();
		}
		info.audio_mix.objects_enabled = ui.checkBox_audio_objects->isChecked();
		info.gpu_occlusion_accurate = ui.checkBox_gpu_occlusion->isChecked();
	}
	info.screen_resolution =
	    TextToEnum<Configuration::Resolution>(ui.comboBox_screen_resolution->currentText());
	info.present_mode =
	    TextToEnum<Configuration::PresentMode>(ui.comboBox_present_mode->currentText());
	info.presentation_clock        = ui.comboBox_presentation_clock->currentData().toString();
	info.presentation_fps          = ui.spinBox_presentation_fps->value();
	info.cpu_hybrid                = ui.comboBox_cpu_hybrid->currentData().toString();
	info.gpu_index                 = ui.comboBox_gpu->currentIndex() - 1;
	info.fullscreen_enabled        = ui.checkBox_fullscreen->isChecked();
	info.hide_cursor_enabled       = ui.checkBox_hide_cursor->isChecked();
	info.readback_linear_images    = ui.checkBox_readback->isChecked();
	info.sync_raw_image_buffers    = ui.checkBox_sync_raw_image_buffers->isChecked();
	info.tessellation_enabled      = ui.checkBox_tessellation->isChecked();
	info.skip_notice_screen        = ui.checkBox_skip_notice_screen->isChecked();
	info.trophy_enabled            = ui.checkBox_trophy_notifications->isChecked();
	info.vblank_frequency          = ui.spinBox_vblank_frequency->value();
	info.console_language          = ui.comboBox_console_language->currentIndex();
	info.vulkan_validation_enabled = ui.checkBox_vulkan_validation->isChecked();
	info.shader_validation_enabled = ui.checkBox_shader_validation->isChecked();
	info.renderdoc_enabled         = ui.checkBox_renderdoc_capture->isChecked();
	const auto amd_cpu             = ui.checkBox_amd_cpu->checkState();
	info.amd_cpu_mode              = amd_cpu == Qt::Checked     ? QStringLiteral("on")
	                                 : amd_cpu == Qt::Unchecked ? QStringLiteral("off")
	                                                            : QStringLiteral("auto");
#if defined(_WIN32)
	info.red_zone_protection_enabled = ui.checkBox_red_zone_protection->isChecked();
#endif
	info.shader_optimization_type = TextToEnum<Configuration::ShaderOptimizationType>(
	    ui.comboBox_shader_optimization_type->currentText());
	info.shader_log_direction = TextToEnum<Configuration::LogDirection>(
	    ui.comboBox_shader_log_direction->currentText());
	info.shader_log_folder           = ui.lineEdit_shader_log_folder->text();
	info.command_buffer_dump_enabled = ui.checkBox_cmd_dump->isChecked();
	info.command_buffer_dump_folder  = ui.lineEdit_cmd_dump_folder->text();
	info.printf_direction =
	    TextToEnum<Configuration::LogDirection>(ui.comboBox_printf_direction->currentText());
	info.printf_output_file = ui.lineEdit_printf_file->text();
	info.profiler_enabled = ui.checkBox_profiler->isChecked();
}

void ConfigurationEditDialog::save() {
	if (MandatoryLineEdit::FindEmpty(this)) {
		QMessageBox::critical(this, tr("Save failed"), tr("Please fill all mandatory fields"));
		return;
	}

	const auto user_name = m_ui->lineEdit_user_name->text().trimmed();
	if (user_name.isEmpty() || user_name.toUtf8().size() > Config::MAX_USER_NAME_LENGTH) {
		QMessageBox::critical(this, tr("Save failed"),
		                      tr("User name must contain 1-16 UTF-8 bytes"));
		return;
	}
	m_ui->lineEdit_user_name->setText(user_name);
	if (!Config::IsConfiguredUserIdValid(m_ui->spinBox_user_id->value())) {
		QMessageBox::critical(this, tr("Save failed"),
		                      tr("User ID cannot be 254 (everyone) or 255 (system)"));
		return;
	}

	UpdateClockControls();
	if (!m_global_settings && m_clock_available &&
	    m_clock_profile->currentData().toString() != "keep") {
		QJsonDocument selected;
		auto          error =
		    Cheats::SelectClockProfile(m_clock_document, m_clock_profiles,
		                               m_clock_profile->currentData().toString(), &selected);
		if (error.isEmpty()) {
			error = Cheats::SaveDocument(Cheats::PlanPath(m_info.title_id), selected, m_clock_data);
		}
		if (!error.isEmpty()) {
			QMessageBox::critical(this, tr("Save failed"), error);
			return;
		}
	}
	UpdateInfo(m_info, *m_ui, m_global_settings);
	m_info.fsr_upscale          = m_fsr_upscale->currentData().toString();
	m_info.fsr_frame_generation = m_fsr_generation->isChecked();

	emit accept();
}

void ConfigurationEditDialog::clear() {
	Configuration default_info;
	Init(default_info);

	if (m_global_settings) {
		emit PreviewControllerColor({});
		m_game_dirs_list->clear();
		update_game_directory_buttons();
	}
}

void ConfigurationEditDialog::add_game_directory() {
	QString start_dir = QDir::homePath();
	if (m_game_dirs_list->count() > 0) {
		start_dir = m_game_dirs_list->item(m_game_dirs_list->count() - 1)->text();
	}

#if defined(__APPLE__)
	// Use the native macOS picker to browse mounted volumes.
	const auto dir = QFileDialog::getExistingDirectory(this, tr("Select game folder"), start_dir);
	if (dir.isEmpty()) {
		return;
	}
	AddGameDirectoryItem(dir);
#else
	QFileDialog dialog(this, tr("Select game folders"), start_dir);
	dialog.setFileMode(QFileDialog::Directory);
	dialog.setOption(QFileDialog::ShowDirsOnly, true);
	dialog.setOption(QFileDialog::DontUseNativeDialog, true);
	dialog.setLabelText(QFileDialog::Accept, tr("Add"));

	auto* list_view = dialog.findChild<QListView*>(QStringLiteral("listView"));
	if (list_view != nullptr) {
		list_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
	}
	auto* tree_view = dialog.findChild<QTreeView*>(QStringLiteral("treeView"));
	if (tree_view != nullptr) {
		tree_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
	}

	if (dialog.exec() != QDialog::Accepted) {
		return;
	}

	for (const auto& dir: dialog.selectedFiles()) {
		AddGameDirectoryItem(dir);
	}
#endif

	update_game_directory_buttons();
}

void ConfigurationEditDialog::remove_selected_game_directories() {
	qDeleteAll(m_game_dirs_list->selectedItems());
	update_game_directory_buttons();
}

void ConfigurationEditDialog::update_game_directory_buttons() {
	m_remove_game_dir_button->setEnabled(!m_game_dirs_list->selectedItems().isEmpty());
}
