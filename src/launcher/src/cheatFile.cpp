#include "cheatFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace Cheats {

namespace {

QString Tr(const char* text) {
	return QCoreApplication::translate("Cheats", text);
}

QString ValidateDocument(const QJsonDocument& document) {
	if (!document.isObject()) {
		return Tr("Invalid cheat JSON.");
	}
	const auto mods = document.object().value(QStringLiteral("mods"));
	if (!mods.isArray()) {
		return Tr("Cheat JSON has no \"mods\" array.");
	}
	for (const auto& value: mods.toArray()) {
		const auto mod     = value.toObject();
		const auto name    = mod.value(QStringLiteral("name"));
		const auto enabled = mod.value(QStringLiteral("enabled"));
		if (!value.isObject() || !name.isString() || name.toString().isEmpty() ||
		    (!enabled.isUndefined() && !enabled.isBool())) {
			return Tr("Invalid cheat entry.");
		}
	}
	return {};
}

} // namespace

bool IsSupportedTitleId(const QString& title_id) {
	static const QRegularExpression pattern(QStringLiteral("\\APPSA[0-9]{5}\\z"));
	return pattern.match(title_id.trimmed().toUpper()).hasMatch();
}

QString PlanPath(const QString& title_id) {
	if (!IsSupportedTitleId(title_id)) {
		return {};
	}
	return QDir(QCoreApplication::applicationDirPath())
	    .filePath(QStringLiteral("_Patches/%1.json").arg(title_id.trimmed().toUpper()));
}

QString ParseDocument(const QByteArray& data, QJsonDocument* document) {
	const auto parsed = QJsonDocument::fromJson(data);
	const auto error  = ValidateDocument(parsed);
	*document         = error.isEmpty() ? parsed : QJsonDocument();
	return error;
}

QString ValidateImport(const QJsonDocument& document, const QString& title_id,
                       const QString& version, const QString& process) {
	const auto parse_error = ValidateDocument(document);
	if (!parse_error.isEmpty()) {
		return parse_error;
	}
	const auto root = document.object();
	if (!IsSupportedTitleId(title_id) ||
	    root.value(QStringLiteral("id"))
	            .toString()
	            .compare(title_id.trimmed(), Qt::CaseInsensitive) != 0) {
		return Tr("The cheat file does not match this game's title ID.");
	}
	if (version.isEmpty() || root.value(QStringLiteral("version")).toString() != version) {
		return Tr("The cheat file does not match this game's version (%1).").arg(version);
	}
	if (process.isEmpty() ||
	    root.value(QStringLiteral("process")).toString().compare(process, Qt::CaseInsensitive) !=
	        0) {
		return Tr("The cheat file does not match this game's executable (%1).").arg(process);
	}
	static const QRegularExpression offset_pattern(
	    QStringLiteral("\\A(?:0[xX])?[0-9a-fA-F]{1,16}\\z"));
	static const QRegularExpression bytes_pattern(QStringLiteral("\\A(?:[0-9a-fA-F]{2})+\\z"));
	const auto                      mods = root.value(QStringLiteral("mods")).toArray();
	if (mods.isEmpty()) {
		return Tr("The cheat file contains no cheats.");
	}
	if (root.contains(QStringLiteral("master"))) {
		return Tr("This JSON uses master codes, which are not supported yet.");
	}
	for (const auto& value: mods) {
		const auto mod    = value.toObject();
		const auto memory = mod.value(QStringLiteral("memory"));
		if (mod.contains(QStringLiteral("module_name"))) {
			return Tr("This JSON uses module-specific cheats, which are not supported yet.");
		}
		if (!memory.isArray() || memory.toArray().isEmpty()) {
			return Tr("Unsupported memory entries in cheat \"%1\".")
			    .arg(mod.value(QStringLiteral("name")).toString());
		}
		for (const auto& entry: memory.toArray()) {
			const auto write    = entry.toObject();
			const auto offset   = write.value(QStringLiteral("offset")).toString();
			const auto off      = write.value(QStringLiteral("off")).toString();
			const auto on       = write.value(QStringLiteral("on")).toString();
			const auto absolute = write.value(QStringLiteral("absolute"));
			const auto expected = write.value(QStringLiteral("expected"));
			if ((!absolute.isUndefined() && (!absolute.isBool() || absolute.toBool())) ||
			    (!expected.isUndefined() &&
			     (!expected.isString() ||
			      expected.toString().compare(off, Qt::CaseInsensitive) != 0)) ||
			    write.contains(QStringLiteral("section")) ||
			    write.contains(QStringLiteral("sectionName")) ||
			    write.contains(QStringLiteral("sectionProtection")) ||
			    write.contains(QStringLiteral("logicalOffset")) ||
			    write.contains(QStringLiteral("sectionOffset"))) {
				return Tr("This JSON uses memory addressing features that are not supported yet.");
			}
			if (!entry.isObject() || !offset_pattern.match(offset).hasMatch() ||
			    !bytes_pattern.match(off).hasMatch() || !bytes_pattern.match(on).hasMatch() ||
			    off.size() != on.size()) {
				return Tr("Unsupported memory entries in cheat \"%1\".")
				    .arg(mod.value(QStringLiteral("name")).toString());
			}
		}
	}
	return {};
}

QJsonDocument WithSelection(const QJsonDocument& document, const QList<bool>& enabled) {
	auto root = document.object();
	auto mods = root.value(QStringLiteral("mods")).toArray();
	if (mods.size() != enabled.size()) {
		return {};
	}
	for (qsizetype index = 0; index < mods.size(); ++index) {
		auto mod = mods[index].toObject();
		mod.insert(QStringLiteral("enabled"), enabled[index]);
		mods[index] = mod;
	}
	root.insert(QStringLiteral("mods"), mods);
	return QJsonDocument(root);
}

QString SelectClockProfile(const QJsonDocument& current, const QList<QJsonDocument>& profiles,
                           const QString& selected_id, QJsonDocument* result) {
	if (profiles.isEmpty() || result == nullptr) {
		return Tr("No compatible clock profiles are bundled.");
	}
	const auto reference = profiles.first().object();
	const auto title     = reference.value("id").toString();
	const auto version   = reference.value("version").toString();
	const auto process   = reference.value("process").toString();
	const auto hash      = reference.value("source_sha256").toString();
	if (hash.size() != 64) {
		return Tr("Clock profile has no executable fingerprint.");
	}
	QSet<QString> ids;
	QJsonArray    clocks;
	for (const auto& profile: profiles) {
		const auto error = ValidateImport(profile, title, version, process);
		if (!error.isEmpty()) {
			return error;
		}
		const auto root = profile.object();
		if (root.value("source_sha256").toString() != hash ||
		    root.value("mods").toArray().size() != 1) {
			return Tr("Bundled clock profiles do not match one executable.");
		}
		auto       mod = root.value("mods").toArray().first().toObject();
		const auto id  = mod.value("id").toString();
		if (id.isEmpty() || ids.contains(id)) {
			return Tr("Clock profile has an invalid or duplicate identifier.");
		}
		ids.insert(id);
		mod.insert("enabled", id == selected_id);
		clocks.append(mod);
	}
	if (!selected_id.isEmpty() && !ids.contains(selected_id)) {
		return Tr("Unknown clock profile.");
	}
	auto root = current.isNull() ? reference : current.object();
	if (!current.isNull()) {
		const auto error = ValidateImport(current, title, version, process);
		if (!error.isEmpty()) {
			return error;
		}
		if (root.contains("source_sha256") && root.value("source_sha256").toString() != hash) {
			return Tr("The local cheats target a different executable.");
		}
	}
	QJsonArray mods;
	for (const auto& value: root.value("mods").toArray()) {
		const auto mod = value.toObject();
		if (ids.contains(mod.value("id").toString())) {
			continue;
		}
		if (!selected_id.isEmpty() && mod.value("enabled").toBool(true) &&
		    (mod.contains("presentation_fps") || mod.contains("simulation_clock"))) {
			return Tr("Disable the other timing cheat before selecting a bundled clock profile.");
		}
		mods.append(mod);
	}
	for (const auto& mod: clocks) {
		mods.append(mod);
	}
	root.insert("source_sha256", hash);
	root.insert("mods", mods);
	*result = QJsonDocument(root);
	return {};
}

QString SaveDocument(const QString& path, const QJsonDocument& document,
                     const QByteArray& expected_data) {
	if (path.isEmpty() || !document.isObject()) {
		return Tr("Invalid cheat file.");
	}
	QFile input(path);
	if (input.exists()) {
		if (!input.open(QIODevice::ReadOnly)) {
			return Tr("Could not read cheat file: %1").arg(input.errorString());
		}
		const auto current = input.readAll();
		if (input.error() != QFile::NoError) {
			return Tr("Could not read cheat file: %1").arg(input.errorString());
		}
		if (current != expected_data) {
			return Tr("Cheat file changed; reload the dialog.");
		}
	} else if (!expected_data.isEmpty()) {
		return Tr("Cheat file changed; reload the dialog.");
	}
	input.close();
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		return Tr("Could not create the local cheats directory.");
	}
	QSaveFile  output(path);
	const auto data = document.toJson();
	if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() ||
	    !output.commit()) {
		return Tr("Could not save cheat file: %1").arg(output.errorString());
	}
	return {};
}

} // namespace Cheats
