#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

namespace features {
struct Result {
    QJsonArray rows;
    QString error;
};
Result inventory(const QString &category);
Result directoryUsage(const QString &root, const std::shared_ptr<std::atomic_bool> &cancel);
Result benchmark(const QString &kind, int seconds, const QString &directory,
                 const std::shared_ptr<std::atomic_bool> &cancel);
QString processAction(quint32 pid, quint64 creation, const QString &action, quint64 value = 0);
QString serviceAction(const QString &name, const QString &action);
QString startupToggle(const QString &name, bool enable);
QString startupFileToggle(const QString &path, bool enable);
Result networkTest(const QString &host, bool dns);
Result processDetails(quint32 pid, quint64 creation, bool modules);
Result processTree(quint32 pid, quint64 creation);
Result processHandles(quint32 pid, quint64 creation);
QString elevatedAction(const QStringList &arguments);
QString verifyInstaller(const QString &path, const QString &signerSha256);
QString launchInstaller(const QString &path, const QString &signerSha256);
bool autoStartEnabled();
QString setAutoStart(bool enabled);
void shutdownWorkers();
Result isolatedTask(const QString &task, const QJsonObject &parameters,
                    const std::shared_ptr<std::atomic_bool> &cancel);
} // namespace features
