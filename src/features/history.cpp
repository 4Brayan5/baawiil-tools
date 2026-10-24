#include "history.h"
#include "../core.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QTimeZone>
#include <cmath>
#include <algorithm>
#include <array>
#include <QSaveFile>
#include <vector>

namespace features {
HistoryStore::HistoryStore(QString path) : connection_(QUuid::createUuid().toString()) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    db_ = QSqlDatabase::addDatabase("QSQLITE", connection_);
    db_.setDatabaseName(path);
    if (!db_.open()) {
        error_ = db_.lastError().text();
        return;
    }
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA busy_timeout=3000");
    QSqlQuery version(db_);
    version.exec("PRAGMA user_version");
    if (!version.next() || version.value(0).toInt() > 5) {
        error_ = "Version de base non prise en charge.";
        db_.close();
        return;
    }
    const int dbVersion = version.value(0).toInt();
    version.finish();
    if (!execute("PRAGMA journal_mode=WAL"))
        return;
    if (dbVersion > 0 && dbVersion < 5) {
        QSqlQuery backup(db_);
        backup.prepare("VACUUM INTO ?");
        backup.addBindValue(path + ".before-v5-" +
                            QString::number(QDateTime::currentMSecsSinceEpoch()) + ".sqlite");
        if (!backup.exec()) {
            error_ = "Sauvegarde avant migration impossible : " + backup.lastError().text();
            return;
        }
    }
    if (!db_.transaction()) {
        error_ = db_.lastError().text();
        return;
    }
    bool ok =
        execute("CREATE TABLE IF NOT EXISTS sessions(id INTEGER PRIMARY KEY,name TEXT NOT "
                "NULL,start INTEGER NOT NULL,end INTEGER,status TEXT NOT NULL)") &&
        execute(
            "CREATE TABLE IF NOT EXISTS samples(session INTEGER NOT NULL REFERENCES sessions(id) "
            "ON DELETE CASCADE,time INTEGER NOT NULL,cpu REAL,ram REAL,extra TEXT,elapsed "
            "INTEGER)") &&
        execute("CREATE INDEX IF NOT EXISTS sample_session_time ON samples(session,time)") &&
        execute("CREATE TABLE IF NOT EXISTS markers(session INTEGER NOT NULL REFERENCES "
                "sessions(id) ON DELETE CASCADE,time INTEGER NOT NULL,text TEXT NOT NULL,elapsed "
                "INTEGER)") &&
        execute("CREATE TABLE IF NOT EXISTS protected_sessions(session INTEGER PRIMARY KEY "
                "REFERENCES sessions(id) ON DELETE CASCADE)") &&
        execute("CREATE TABLE IF NOT EXISTS reports(id INTEGER PRIMARY KEY,time INTEGER NOT "
                "NULL,type TEXT NOT NULL,data TEXT NOT NULL)") &&
        execute(
            "CREATE TABLE IF NOT EXISTS settings(name TEXT PRIMARY KEY,value INTEGER NOT NULL)") &&
        execute("UPDATE sessions SET status='interrompue' WHERE status='active'");
    if (ok) {
        bool extra = false, elapsed = false;
        {
            QSqlQuery columns("PRAGMA table_info(samples)", db_);
            while (columns.next())
                if (columns.value(1).toString() == "extra")
                    extra = true;
                else if (columns.value(1).toString() == "elapsed")
                    elapsed = true;
        }
        if (!extra)
            ok = execute("ALTER TABLE samples ADD COLUMN extra TEXT");
        if (ok && !elapsed)
            ok = execute("ALTER TABLE samples ADD COLUMN elapsed INTEGER");
    }
    if (ok) {
        bool elapsed = false;
        {
            QSqlQuery columns("PRAGMA table_info(markers)", db_);
            while (columns.next())
                if (columns.value(1).toString() == "elapsed")
                    elapsed = true;
        }
        if (!elapsed)
            ok = execute("ALTER TABLE markers ADD COLUMN elapsed INTEGER");
    }
    if (ok)
        ok = execute("PRAGMA user_version=5");
    if (ok) {
        if (!db_.commit())
            error_ = db_.lastError().text();
        else
            initialized_ = true;
    } else
        db_.rollback();
    QSqlQuery settings("SELECT name,value FROM settings", db_);
    while (settings.next()) {
        if (settings.value(0).toString() == "days")
            days_ = std::clamp(settings.value(1).toInt(), 1, 3650);
        if (settings.value(0).toString() == "maxBytes")
            maxBytes_ = std::clamp<qint64>(settings.value(1).toLongLong(), 10ll * 1024 * 1024,
                                           10240ll * 1024 * 1024);
    }
}
HistoryStore::~HistoryStore() {
    stop();
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_);
}
bool HistoryStore::execute(const QString &sql) {
    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        error_ = query.lastError().text();
        return false;
    }
    return true;
}
bool HistoryStore::start(const QString &name, int days, qint64 maxBytes) {
    if (!ready() || active_ || name.trimmed().isEmpty() || name.size() > 200 || days < 1 ||
        days > 3650 || maxBytes < 10ll * 1024 * 1024)
        return false;
    days_ = days;
    maxBytes_ = maxBytes;
    error_.clear();
    QSqlQuery settings(db_);
    settings.prepare("INSERT OR REPLACE INTO settings(name,value) VALUES(?,?)");
    settings.addBindValue("days");
    settings.addBindValue(days_);
    if (!settings.exec()) {
        error_ = settings.lastError().text();
        return false;
    }
    settings.bindValue(0, "maxBytes");
    settings.bindValue(1, maxBytes_);
    if (!settings.exec()) {
        error_ = settings.lastError().text();
        return false;
    }
    if (!prune())
        return false;
    QSqlQuery q(db_);
    q.prepare("INSERT INTO sessions(name,start,status) VALUES(?,?,'active')");
    q.addBindValue(name);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        error_ = q.lastError().text();
        return false;
    }
    active_ = q.lastInsertId().toLongLong();
    sessionStartMonotonic_ = monitor::monotonicMs();
    return true;
}
bool HistoryStore::stop(bool incomplete) {
    if (!active_)
        return true;
    if (!flush()) {
        active_ = 0;
        return false;
    }
    QSqlQuery q(db_);
    q.prepare("UPDATE sessions SET end=?,status=? WHERE id=?");
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(incomplete ? QString("interrompue") : QString("terminée"));
    q.addBindValue(active_);
    if (!q.exec()) {
        error_ = q.lastError().text();
        active_ = 0;
        return false;
    }
    active_ = 0;
    return true;
}
bool HistoryStore::prune() {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM sessions WHERE status!='active' AND start<? AND id NOT IN (SELECT "
              "session FROM protected_sessions)");
    q.addBindValue(QDateTime::currentMSecsSinceEpoch() - qint64(days_) * 86400000);
    if (!q.exec()) {
        error_ = q.lastError().text();
        return false;
    }
    // Bound reports too; no external file or arbitrary path is ever purged.
    q.prepare("DELETE FROM reports WHERE time<?");
    q.addBindValue(QDateTime::currentMSecsSinceEpoch() - qint64(days_) * 86400000);
    if (!q.exec()) {
        error_ = q.lastError().text();
        return false;
    }
    // File size includes free SQLite pages. Use live page count; existing free pages can be reused.
    QSqlQuery pages(db_), free(db_), size(db_);
    pages.exec("PRAGMA page_count");
    free.exec("PRAGMA freelist_count");
    size.exec("PRAGMA page_size");
    if (!pages.next() || !free.next() || !size.next()) {
        error_ = "Impossible de vérifier le quota.";
        return false;
    }
    if ((pages.value(0).toLongLong() - free.value(0).toLongLong()) * size.value(0).toLongLong() >=
        maxBytes_) {
        error_ = "Quota de données atteint. Supprimer des sessions ou augmenter la limite.";
        return false;
    }
    return true;
}
bool HistoryStore::append(qint64 time, std::optional<double> cpu, std::optional<double> ram,
                          QJsonObject extra, std::optional<qint64> sourceMonotonic) {
    if (!active_)
        return false;
    const auto elapsed = sourceMonotonic.value_or(monitor::monotonicMs()) - sessionStartMonotonic_;
    if (elapsed < 0)
        return true; // Do not record a queued measurement captured before recording began.
    if (cpu && (!std::isfinite(*cpu) || *cpu < 0 || *cpu > 100))
        cpu.reset();
    if (ram && (!std::isfinite(*ram) || *ram < 0 || *ram > 100))
        ram.reset();
    if (pending_ == 0) {
        if (!prune()) {
            stop(true);
            return false;
        }
        if (!db_.transaction()) {
            error_ = db_.lastError().text();
            stop(true);
            return false;
        }
    }
    QSqlQuery q(db_);
    q.prepare("INSERT INTO samples(session,time,cpu,ram,extra,elapsed) VALUES(?,?,?,?,?,?)");
    q.addBindValue(active_);
    q.addBindValue(time);
    q.addBindValue(cpu ? QVariant(*cpu) : QVariant());
    q.addBindValue(ram ? QVariant(*ram) : QVariant());
    q.addBindValue(QString::fromUtf8(QJsonDocument(extra).toJson(QJsonDocument::Compact)));
    q.addBindValue(elapsed);
    if (!q.exec()) {
        error_ = q.lastError().text();
        db_.rollback();
        pending_ = 0;
        stop(true);
        return false;
    }
    ++pending_;
    return pending_ < 10 || flush();
}
bool HistoryStore::flush() {
    if (!pending_)
        return true;
    if (!db_.commit()) {
        error_ = db_.lastError().text();
        db_.rollback();
        pending_ = 0;
        active_ = 0;
        return false;
    }
    pending_ = 0;
    return execute("PRAGMA wal_checkpoint(TRUNCATE)");
}
bool HistoryStore::marker(const QString &text) {
    if (!active_ || text.isEmpty() || text.size() > 1000)
        return false;
    QSqlQuery q(db_);
    q.prepare("INSERT INTO markers(session,time,text,elapsed) VALUES(?,?,?,?)");
    q.addBindValue(active_);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(text);
    q.addBindValue(std::max<qint64>(0, monitor::monotonicMs() - sessionStartMonotonic_));
    if (!q.exec()) {
        error_ = q.lastError().text();
        return false;
    }
    return true;
}
bool HistoryStore::saveReport(const QString &type, const QJsonArray &data) {
    if (!flush())
        return false;
    auto json = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (json.size() > 1024 * 1024 || !ready() || !prune())
        return false;
    QSqlQuery q(db_);
    q.prepare("INSERT INTO reports(time,type,data) VALUES(?,?,?)");
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(type);
    q.addBindValue(QString::fromUtf8(json));
    if (!q.exec()) {
        error_ = q.lastError().text();
        return false;
    }
    return true;
}
QJsonArray HistoryStore::sessions() {
    QJsonArray rows;
    QSqlQuery q("SELECT id,name,start,end,status,EXISTS(SELECT 1 FROM protected_sessions p WHERE "
                "p.session=sessions.id) FROM sessions ORDER BY start DESC LIMIT 1000",
                db_);
    while (q.next())
        rows.append(QJsonObject{
            {"ID", double(q.value(0).toLongLong())},
            {"Nom", q.value(1).toString()},
            {"Début UTC", QDateTime::fromMSecsSinceEpoch(q.value(2).toLongLong(), QTimeZone::UTC)
                              .toString(Qt::ISODateWithMs)},
            {"Fin UTC", q.value(3).isNull() ? QString()
                                            : QDateTime::fromMSecsSinceEpoch(
                                                  q.value(3).toLongLong(), QTimeZone::UTC)
                                                  .toString(Qt::ISODateWithMs)},
            {"État", q.value(4).toString()},
            {"Conservée", q.value(5).toBool()}});
    return rows;
}
QJsonArray HistoryStore::samples(qint64 session) {
    QJsonArray rows;
    QSqlQuery bounds(db_);
    bounds.prepare("SELECT "
                   "COUNT(*),MIN(COALESCE(s.elapsed,s.time-u.start)),MAX(COALESCE(s.elapsed,s.time-"
                   "u.start)) FROM samples s JOIN sessions u ON s.session=u.id WHERE s.session=?");
    bounds.addBindValue(session);
    if (!bounds.exec() || !bounds.next()) {
        error_ = bounds.lastError().text();
        return rows;
    }
    const auto count = bounds.value(0).toLongLong(), start = bounds.value(1).toLongLong(),
               end = bounds.value(2).toLongLong();
    bounds.finish();
    const qint64 width = std::max<qint64>(1, (end - start + 9999) / 10000);
    struct Raw {
        qint64 time;
        std::optional<double> cpu, ram;
        QString extra;
        qint64 axis;
        std::optional<qint64> elapsed;
    };
    auto append = [&](const Raw &raw) {
        rows.append(QJsonObject{
            {"UTC",
             QDateTime::fromMSecsSinceEpoch(raw.time, QTimeZone::UTC).toString(Qt::ISODateWithMs)},
            {"CPU %", raw.cpu ? QJsonValue(*raw.cpu) : QJsonValue()},
            {"RAM %", raw.ram ? QJsonValue(*raw.ram) : QJsonValue()},
            {"Capteurs supplémentaires", QJsonDocument::fromJson(raw.extra.toUtf8()).object()},
            {"Décimation", count > 100000},
            {"Temps écoulé ms", raw.elapsed ? QJsonValue(double(*raw.elapsed)) : QJsonValue()},
            {"Chronologie", raw.elapsed ? "Monotone" : "UTC héritée"}});
    };
    using Candidate = std::pair<qint64, Raw>;
    std::array<std::optional<Candidate>, 8> candidates{};
    qint64 bucket = -1, index = 0;
    auto flush = [&] {
        std::vector<Candidate> selected;
        for (const auto &candidate : candidates)
            if (candidate)
                selected.push_back(*candidate);
        std::sort(selected.begin(), selected.end(),
                  [](const Candidate &a, const Candidate &b) { return a.first < b.first; });
        qint64 last = -1;
        for (const auto &candidate : selected)
            if (candidate.first != last) {
                append(candidate.second);
                last = candidate.first;
            }
        candidates = {};
    };
    QSqlQuery q(db_);
    q.setForwardOnly(true);
    q.prepare("SELECT s.time,s.cpu,s.ram,s.extra,s.elapsed,COALESCE(s.elapsed,s.time-u.start) FROM "
              "samples s JOIN sessions u ON s.session=u.id WHERE s.session=? ORDER BY s.rowid");
    q.addBindValue(session);
    q.exec();
    while (q.next()) {
        Raw raw{q.value(0).toLongLong(),
                q.value(1).isNull() ? std::nullopt : std::optional<double>(q.value(1).toDouble()),
                q.value(2).isNull() ? std::nullopt : std::optional<double>(q.value(2).toDouble()),
                q.value(3).toString(),
                q.value(5).toLongLong(),
                q.value(4).isNull() ? std::nullopt
                                    : std::optional<qint64>(q.value(4).toLongLong())};
        if (count <= 100000) {
            append(raw);
            continue;
        }
        auto next = (raw.axis - start) / width;
        if (next != bucket) {
            flush();
            bucket = next;
        }
        Candidate current{index++, raw};
        if (!candidates[0])
            candidates[0] = current;
        candidates[1] = current;
        if (raw.cpu) {
            if (!candidates[2] || *raw.cpu < *candidates[2]->second.cpu)
                candidates[2] = current;
            if (!candidates[3] || *raw.cpu > *candidates[3]->second.cpu)
                candidates[3] = current;
        } else if (!candidates[6])
            candidates[6] = current;
        if (raw.ram) {
            if (!candidates[4] || *raw.ram < *candidates[4]->second.ram)
                candidates[4] = current;
            if (!candidates[5] || *raw.ram > *candidates[5]->second.ram)
                candidates[5] = current;
        } else if (!candidates[7])
            candidates[7] = current;
    }
    flush();
    return rows;
}
QJsonArray HistoryStore::statistics(qint64 session) {
    QSqlQuery q(db_);
    q.prepare("SELECT COUNT(cpu),MIN(cpu),AVG(cpu),MAX(cpu),COUNT(ram),MIN(ram),AVG(ram),MAX(ram) "
              "FROM samples WHERE session=?");
    q.addBindValue(session);
    QJsonArray rows;
    if (q.exec() && q.next())
        for (int offset : {0, 4})
            rows.append(QJsonObject{
                {"Mesure", offset ? "RAM %" : "CPU %"},
                {"Relevés valides", double(q.value(offset).toLongLong())},
                {"Minimum", q.value(offset + 1).isNull()
                                ? QJsonValue()
                                : QJsonValue(q.value(offset + 1).toDouble())},
                {"Moyenne arithmétique", q.value(offset + 2).isNull()
                                             ? QJsonValue()
                                             : QJsonValue(q.value(offset + 2).toDouble())},
                {"Maximum", q.value(offset + 3).isNull()
                                ? QJsonValue()
                                : QJsonValue(q.value(offset + 3).toDouble())}});
    return rows;
}
Result exportSession(const QString &database, qint64 session, const QString &destination,
                     const std::shared_ptr<std::atomic_bool> &cancel) {
    const auto source = QFileInfo(database).canonicalFilePath(),
               target = QFileInfo(destination).canonicalFilePath();
    const auto absolute = QDir::cleanPath(QFileInfo(destination).absoluteFilePath()),
               dbAbsolute = QDir::cleanPath(QFileInfo(database).absoluteFilePath());
    if ((!target.isEmpty() && target.compare(source, Qt::CaseInsensitive) == 0) ||
        absolute.compare(dbAbsolute, Qt::CaseInsensitive) == 0 ||
        absolute.compare(dbAbsolute + "-wal", Qt::CaseInsensitive) == 0 ||
        absolute.compare(dbAbsolute + "-shm", Qt::CaseInsensitive) == 0)
        return {
            {},
            "La destination ne peut pas remplacer la base de données ou ses fichiers de travail."};
    const auto connection = QUuid::createUuid().toString();
    QString error;
    qint64 count = 0;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        db.setDatabaseName(database);
        if (!db.open())
            error = db.lastError().text();
        else {
            QSaveFile output(destination);
            if (!output.open(QIODevice::WriteOnly))
                error = output.errorString();
            else {
                bool csv = destination.endsWith(".csv", Qt::CaseInsensitive), first = true;
                auto write = [&](const QByteArray &bytes) {
                    if (output.write(bytes) != bytes.size())
                        error = output.errorString();
                };
                write(csv ? QByteArray("utc,elapsed_ms,cpu_percent,ram_percent,auxiliary_json\r\n")
                          : QByteArray("{\"schema\":1,\"samples\":["));
                QSqlQuery q(db);
                q.setForwardOnly(true);
                q.prepare("SELECT time,cpu,ram,extra,elapsed FROM samples WHERE session=? ORDER BY "
                          "rowid");
                q.addBindValue(session);
                if (!q.exec())
                    error = q.lastError().text();
                while (error.isEmpty() && !cancel->load() && q.next()) {
                    auto utc =
                        QDateTime::fromMSecsSinceEpoch(q.value(0).toLongLong(), QTimeZone::UTC)
                            .toString(Qt::ISODateWithMs);
                    auto cpu = q.value(1).isNull()
                                   ? QString()
                                   : QString::number(q.value(1).toDouble(), 'g', 17),
                         ram = q.value(2).isNull()
                                   ? QString()
                                   : QString::number(q.value(2).toDouble(), 'g', 17);
                    auto extra = q.value(3).toString();
                    if (csv) {
                        extra.replace('"', "\"\"");
                        write((utc + "," +
                               (q.value(4).isNull() ? QString()
                                                    : QString::number(q.value(4).toLongLong())) +
                               "," + cpu + "," + ram + ",\"" + extra + "\"\r\n")
                                  .toUtf8());
                    } else {
                        if (!first)
                            write(",");
                        first = false;
                        write(
                            QJsonDocument(
                                QJsonObject{{"utc", utc},
                                            {"elapsedMs",
                                             q.value(4).isNull()
                                                 ? QJsonValue()
                                                 : QJsonValue(double(q.value(4).toLongLong()))},
                                            {"cpuPercent", q.value(1).isNull()
                                                               ? QJsonValue()
                                                               : QJsonValue(q.value(1).toDouble())},
                                            {"ramPercent", q.value(2).isNull()
                                                               ? QJsonValue()
                                                               : QJsonValue(q.value(2).toDouble())},
                                            {"auxiliary",
                                             QJsonDocument::fromJson(q.value(3).toString().toUtf8())
                                                 .object()}})
                                .toJson(QJsonDocument::Compact));
                    }
                    ++count;
                }
                if (q.lastError().isValid())
                    error = q.lastError().text();
                if (cancel->load())
                    error = "Export annulé ; destination initiale conservée.";
                if (!csv)
                    write("]}");
                if (error.isEmpty()) {
                    if (!output.commit())
                        error = output.errorString();
                } else
                    output.cancelWriting();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return {{QJsonObject{{"Destination", destination},
                         {"Relevés exportés", double(count)},
                         {"État", error.isEmpty() ? "Terminé" : "Non terminé"}}},
            error};
}
QJsonArray HistoryStore::markers(qint64 session) {
    QJsonArray rows;
    QSqlQuery q(db_);
    q.prepare("SELECT time,text,elapsed FROM markers WHERE session=? ORDER BY rowid");
    q.addBindValue(session);
    q.exec();
    while (q.next())
        rows.append(QJsonObject{
            {"UTC", QDateTime::fromMSecsSinceEpoch(q.value(0).toLongLong(), QTimeZone::UTC)
                        .toString(Qt::ISODateWithMs)},
            {"Marqueur", q.value(1).toString()},
            {"Temps écoulé ms",
             q.value(2).isNull() ? QJsonValue() : QJsonValue(double(q.value(2).toLongLong()))}});
    return rows;
}
QJsonArray HistoryStore::reports(const QString &type) {
    QJsonArray rows;
    QSqlQuery q(db_);
    q.prepare("SELECT time,data FROM reports WHERE type=? ORDER BY time DESC LIMIT 1000");
    q.addBindValue(type);
    q.exec();
    while (q.next()) {
        auto data = QJsonDocument::fromJson(q.value(1).toString().toUtf8()).array();
        for (auto item : data) {
            auto obj = item.toObject();
            obj["Enregistrement UTC"] =
                QDateTime::fromMSecsSinceEpoch(q.value(0).toLongLong(), QTimeZone::UTC)
                    .toString(Qt::ISODateWithMs);
            rows.append(obj);
        }
    }
    return rows;
}
bool HistoryStore::protectSession(qint64 id, bool enabled) {
    if (!flush())
        return false;
    QSqlQuery q(db_);
    q.prepare(enabled ? "INSERT OR IGNORE INTO protected_sessions(session) VALUES(?)"
                      : "DELETE FROM protected_sessions WHERE session=?");
    q.addBindValue(id);
    if (!q.exec()) {
        error_ = q.lastError().text();
        return false;
    }
    return true;
}
bool HistoryStore::deleteSession(qint64 id) {
    if (id == active_)
        return false;
    QSqlQuery q(db_);
    q.prepare("DELETE FROM sessions WHERE id=?");
    q.addBindValue(id);
    if (!q.exec()) {
        error_ = q.lastError().text();
        return false;
    }
    return true;
}
} // namespace features
