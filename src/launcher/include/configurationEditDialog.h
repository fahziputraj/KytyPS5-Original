#ifndef CONFIGURATION_EDIT_DIALOG_H
#define CONFIGURATION_EDIT_DIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QJsonDocument>
#include <QString>
#include <QStringList>
class QGroupBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QListWidget;
class QMoveEvent;
class QResizeEvent;
class QToolButton;
class QSettings;
class QWidget;

class Configuration;
namespace Ui {
class ConfigurationEditDialog;
} // namespace Ui

class ConfigurationEditDialog: public QDialog {
	Q_OBJECT

public:
	explicit ConfigurationEditDialog(Configuration& info, QWidget* parent = nullptr);
	~ConfigurationEditDialog() override;

	static void WriteSettings(QSettings& s);
	static void ReadSettings(QSettings& s);

	void                      SetGlobalSettings(const QStringList& dirs);
	[[nodiscard]] QStringList GetGameDirectories() const;

signals:
	void PreviewControllerColor(const QString& color);
	void ImportGameSettings();
	void ExportGameSettings();

private:
	Ui::ConfigurationEditDialog* m_ui = nullptr;
	Configuration&               m_info;
	QGroupBox*                   m_game_dirs_group        = nullptr;
	QListWidget*                 m_game_dirs_list         = nullptr;
	QToolButton*                 m_remove_game_dir_button = nullptr;
	bool                         m_global_settings        = false;
	QComboBox*                   m_clock_profile          = nullptr;
	QLabel*                      m_clock_status           = nullptr;
	QComboBox*                   m_fsr_upscale            = nullptr;
	QCheckBox*                   m_fsr_generation         = nullptr;
	QList<QJsonDocument>         m_clock_profiles;
	QJsonDocument                m_clock_document;
	QByteArray                   m_clock_data;
	bool                         m_clock_available = false;

protected:
	void Init(const Configuration& info);
	void InitGameDirectories();
	void DetectMotionControllers();
	void InitPacingControls();
	void UpdateClockControls();
	void RunFsrGpuTest();
	void AddGameDirectoryItem(const QString& dir);

	void moveEvent(QMoveEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

	static QByteArray g_last_geometry;

	/*slots:*/

	void save();
	void clear();
	void add_game_directory();
	void remove_selected_game_directories();
	void update_game_directory_buttons();
};

#endif // CONFIGURATION_EDIT_DIALOG_H
