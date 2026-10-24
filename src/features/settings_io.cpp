#include "settings_io.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QMap>
namespace features {
namespace {
const QStringList names{"settings.json", "features.json", "updates.json"};
QString atomicWrite(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    return {};
}
bool supported(const QString &name, const QJsonObject &obj) {
    if (name == "settings.json") {
        int ms = obj["intervalMs"].toInt(-1);
        return obj["version"].toInt() == 1 && obj["dark"].isBool() &&
               QList<int>{250, 500, 1000, 2000, 5000}.contains(ms) &&
               obj["processHeader"].toString().size() < 16384;
    }
    return obj["schema"].toInt() == 1 &&
           (name != "features.json" || obj["rules"].toArray().size() <= 64);
}
} // namespace
QString exportSettings(const QString &directory, const QString &destination) {
    QJsonObject files;
    for (const auto &name : names) {
        QFile file(QDir(directory).filePath(name));
        if (!file.exists())
            continue;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
            return "Fichier de préférences inaccessible ou trop volumineux.";
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            return "Une configuration est invalide ; son contenu d'origine n'a pas été modifié.";
        files[name] = doc.object();
    }
    for (const auto &name : names)
        if (QFileInfo(destination)
                    .absoluteFilePath()
                    .compare(QDir(directory).absoluteFilePath(name), Qt::CaseInsensitive) == 0 ||
            (!QFileInfo(destination).canonicalFilePath().isEmpty() &&
             QFileInfo(destination)
                     .canonicalFilePath()
                     .compare(QFileInfo(QDir(directory).filePath(name)).canonicalFilePath(),
                              Qt::CaseInsensitive) == 0))
            return "Choisir une destination distincte des fichiers de configuration actifs.";
    return atomicWrite(destination,
                       QJsonDocument(QJsonObject{{"schema", 1}, {"files", files}}).toJson());
}
QString importSettings(const QString &directory, const QString &source) {
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 256 * 1024)
        return "Sauvegarde inaccessible ou trop volumineuse.";
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(input.readAll(), &error);
    auto root = doc.object();
    if (error.error != QJsonParseError::NoError || root["schema"].toInt() != 1 ||
        !root["files"].isObject())
        return "Format de sauvegarde invalide.";
    auto files = root["files"].toObject();
    if (files.isEmpty())
        return "La sauvegarde ne contient aucun paramètre.";
    QMap<QString, QByteArray> originals, replacements;
    QJsonObject backup;
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (!names.contains(it.key()) || !it.value().isObject() ||
            !supported(it.key(), it.value().toObject()))
            return "Nom, version ou valeurs de configuration non supportés.";
        auto object = it.value().toObject();
        if (it.key() == "features.json") {
            object["autoRecord"] = false;
            object["alertsEnabled"] = false;
        }
        if (it.key() == "updates.json")
            object["automatic"] = false;
        auto path = QDir(directory).filePath(it.key());
        QFile file(path);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
                return "Impossible de préserver les préférences existantes.";
            auto bytes = file.readAll();
            auto old = QJsonDocument::fromJson(bytes).object();
            if (old.value(it.key() == "settings.json" ? "version" : "schema").toInt() > 1)
                return "Une configuration de version future a été conservée ; restauration "
                       "refusée.";
            originals[it.key()] = bytes;
            backup[it.key()] = QString::fromLatin1(bytes.toBase64());
        } else
            backup[it.key()] = QJsonValue();
        replacements[it.key()] = QJsonDocument(object).toJson();
    }
    if (!QDir().mkpath(directory))
        return "Dossier de configuration inaccessible.";
    auto backupPath = QDir(directory).filePath(
        "settings-before-import-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json");
    auto saved = atomicWrite(backupPath, QJsonDocument(backup).toJson());
    if (!saved.isEmpty())
        return "Sauvegarde préalable impossible : " + saved;
    QStringList written;
    for (auto it = replacements.begin(); it != replacements.end(); ++it) {
        auto result = atomicWrite(QDir(directory).filePath(it.key()), it.value());
        if (!result.isEmpty()) {
            bool restored = true;
            for (const auto &name : written) {
                auto path = QDir(directory).filePath(name);
                if (originals.contains(name))
                    restored = atomicWrite(path, originals[name]).isEmpty() && restored;
                else
                    restored = QFile::remove(path) && restored;
            }
            return result + (restored
                                 ? " Préférences précédentes restaurées."
                                 : " Restauration partielle : utiliser la sauvegarde préalable " +
                                       backupPath);
        }
        written.append(it.key());
    }
    return {};
}
} // namespace features
