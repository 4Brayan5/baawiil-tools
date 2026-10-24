#pragma once
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
namespace features {
// Deliberately excludes process names, file paths, addresses and arbitrary error strings.
inline void logEvent(const QString &module, const QString &status) {
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(directory))
        return;
    auto path = directory + "/events.jsonl";
    if (QFileInfo(path).size() > 1024 * 1024) {
        QFile::remove(path + ".1");
        if (!QFile::rename(path, path + ".1"))
            return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write(QJsonDocument(QJsonObject{{"utc", QDateTime::currentDateTimeUtc().toString(
                                                         Qt::ISODateWithMs)},
                                             {"module", module.left(80)},
                                             {"status", status.left(80)}})
                       .toJson(QJsonDocument::Compact) +
                   "\n");
}
} // namespace features
