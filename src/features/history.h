#pragma once
#include <QSqlDatabase>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <optional>
#include "backend.h"

namespace features {
class HistoryStore {
    QString connection_;
    QSqlDatabase db_;
    qint64 active_ = 0;
    qint64 maxBytes_ = 200ll * 1024 * 1024;
    int days_ = 7;
    QString error_;
    int pending_ = 0;
    bool initialized_ = false;
    qint64 sessionStartMonotonic_ = 0;

  public:
    explicit HistoryStore(QString path);
    ~HistoryStore();
    bool ready() const {
        return db_.isOpen() && initialized_;
    }
    QString error() const {
        return error_;
    }
    bool recording() const {
        return active_ != 0;
    }
    bool start(const QString &name, int days, qint64 maxBytes);
    bool stop(bool incomplete = false);
    bool append(qint64 utcMs, std::optional<double> cpu, std::optional<double> ram,
                QJsonObject extra = {}, std::optional<qint64> sourceMonotonic = {});
    bool marker(const QString &text);
    bool saveReport(const QString &type, const QJsonArray &data);
    QJsonArray sessions();
    QJsonArray samples(qint64 session);
    QJsonArray reports(const QString &type);
    QJsonArray markers(qint64 session);
    bool deleteSession(qint64 session);
    bool protectSession(qint64 session, bool enabled);
    QString path() const {
        return db_.databaseName();
    }
    QJsonArray statistics(qint64 session);
    bool flushPending() {
        return flush();
    }

  private:
    bool execute(const QString &sql);
    bool prune();
    bool flush();
};
Result exportSession(const QString &database, qint64 session, const QString &destination,
                     const std::shared_ptr<std::atomic_bool> &cancel);
} // namespace features
