#include "features/history.h"
#include "features/telemetry.h"
#include "features/analysis.h"
#include "features/settings_io.h"
#include "core.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QDateTime>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <iostream>
#include <stdexcept>
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory");
    qint64 session = 0;
    {
        features::HistoryStore store(directory.filePath("history.sqlite"));
        check(store.ready(), store.error().toUtf8().constData());
        check(!store.append(1, 50, 60), "no implicit recording");
        check(store.start("Session ' ; SELECT", 7, 10ll * 1024 * 1024),
              "create session with bound parameters");
        check(!store.start("overlap", 7, 10ll * 1024 * 1024), "no overlapping recording");
        session = store.sessions().first().toObject().value("ID").toInteger();
        check(!store.deleteSession(session), "protect active session");
        auto now = QDateTime::currentMSecsSinceEpoch();
        check(store.append(now, 25.5, std::nullopt, QJsonObject{{"gpu-percent", 55}}),
              "append valid CPU, missing RAM and auxiliary telemetry");
        check(store.append(now + 1000, std::nullopt, 71.2), "append missing CPU and valid RAM");
        check(store.marker("jeu lancé"), "marker");
        check(store.stop(), "stop recording");
        check(store.samples(session).size() == 2, "two recorded samples");
        check(store.samples(session)[0].toObject()["RAM %"].isNull(), "missing remains null");
        check(store.samples(session)[0]
                      .toObject()["Capteurs supplémentaires"]
                      .toObject()["gpu-percent"]
                      .toInt() == 55,
              "auxiliary sensor round trip");
        check(store.protectSession(session, true),
              "protect recorded session from automatic pruning");
        check(store.sessions()[0].toObject()["Conservée"].toBool(), "protected flag persisted");
        check(store.markers(session).size() == 1, "marker persisted");
        check(store.statistics(session)[0].toObject()["Moyenne arithmétique"].toDouble() == 25.5,
              "session statistics exclude missing data");
        auto cancel = std::make_shared<std::atomic_bool>(false);
        auto exported = features::exportSession(store.path(), session,
                                                directory.filePath("session.json"), cancel);
        check(exported.error.isEmpty(), "complete JSON export");
        QFile jsonFile(directory.filePath("session.json"));
        check(jsonFile.open(QIODevice::ReadOnly), "read JSON export");
        auto exportedJson = QJsonDocument::fromJson(jsonFile.readAll());
        check(exportedJson.object()["samples"].toArray().size() == 2,
              "JSON export retains all samples");
        check(!features::exportSession(store.path(), session, store.path(), cancel).error.isEmpty(),
              "export cannot overwrite live database");
        QFile oldFile(directory.filePath("cancel.csv"));
        check(oldFile.open(QIODevice::WriteOnly), "prepare old destination");
        oldFile.write("keep me");
        oldFile.close();
        cancel->store(true);
        check(!features::exportSession(store.path(), session, oldFile.fileName(), cancel)
                   .error.isEmpty(),
              "canceled export reports failure");
        check(oldFile.open(QIODevice::ReadOnly) && oldFile.readAll() == "keep me",
              "canceled export preserves existing destination");
        check(store.saveReport("benchmark", QJsonArray{QJsonObject{{"score", 123}}}),
              "save report");
        check(store.reports("benchmark").size() == 1, "read report");
    }
    {
        auto path = directory.filePath("legacy.sqlite");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "legacy-fixture");
            db.setDatabaseName(path);
            check(db.open(), "create legacy fixture");
            QSqlQuery q(db);
            check(q.exec("CREATE TABLE sessions(id INTEGER PRIMARY KEY,name TEXT,start INTEGER,end "
                         "INTEGER,status TEXT)"),
                  "legacy sessions");
            check(q.exec("CREATE TABLE samples(session INTEGER,time INTEGER,cpu REAL,ram REAL)"),
                  "legacy samples");
            check(q.exec("INSERT INTO sessions VALUES(1,'interrupted',0,NULL,'active')"),
                  "legacy unfinished session");
            check(q.exec("PRAGMA user_version=1"), "legacy schema version");
            q.finish();
            db.close();
        }
        QSqlDatabase::removeDatabase("legacy-fixture");
        features::HistoryStore store(path);
        check(store.ready(), store.error().toUtf8().constData());
        check(store.sessions()[0].toObject()["État"].toString() == QString::fromUtf8("interrompue"),
              "recover interrupted session");
        check(!QDir(directory.path()).entryList({"legacy.sqlite.before-v5-*.sqlite"}).isEmpty(),
              "backup created before schema migration");
    }
    {
        auto path = directory.filePath("future.sqlite");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "future-fixture");
            db.setDatabaseName(path);
            check(db.open(), "future database fixture");
            QSqlQuery q(db);
            check(q.exec("PRAGMA user_version=99"), "future schema marker");
            q.finish();
            db.close();
        }
        QSqlDatabase::removeDatabase("future-fixture");
        features::HistoryStore store(path);
        check(!store.ready(), "unknown future schema is rejected");
    }
    {
        features::HistoryStore store(directory.filePath("history.sqlite"));
        check(store.ready(), "reopen database");
        check(store.sessions().size() == 1 && store.samples(session).size() == 2,
              "data survives reopening");
        check(store.deleteSession(session), "delete session");
        check(store.samples(session).isEmpty() && store.markers(session).isEmpty(),
              "cascade deletes samples and markers");
    }
    {
        auto path = directory.filePath("long.sqlite");
        {
            features::HistoryStore store(path);
            check(store.ready(), "long-session schema");
        }
        const qint64 base = QDateTime::currentMSecsSinceEpoch();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "long-fixture");
            db.setDatabaseName(path);
            check(db.open(), "long-session fixture");
            QSqlQuery q(db);
            q.prepare("INSERT INTO sessions(id,name,start,status) VALUES(1,'synthetic long "
                      "session',?,'terminée')");
            q.addBindValue(base);
            check(q.exec(), "long session identity");
            q.prepare("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                      "x<100001) INSERT INTO samples(session,time,cpu,ram,extra) SELECT "
                      "1,?+x*250,CASE WHEN x=50001 THEN 99 WHEN x=50002 THEN NULL ELSE 10 END,CASE "
                      "WHEN x=80001 THEN 1 ELSE 50 END,'{}' FROM n");
            q.addBindValue(base);
            check(q.exec(), "generate synthetic high-frequency recording");
            q.finish();
            db.close();
        }
        QSqlDatabase::removeDatabase("long-fixture");
        features::HistoryStore store(path);
        auto samples = store.samples(1);
        check(!samples.isEmpty() && samples.size() < 100001, "long recordings are decimated");
        bool peak = false, trough = false, missing = false;
        for (auto value : samples) {
            auto row = value.toObject();
            if (row["CPU %"].toDouble() == 99)
                peak = true;
            if (row["RAM %"].toDouble() == 1)
                trough = true;
            if (row["CPU %"].isNull())
                missing = true;
        }
        check(peak && trough && missing, "decimation retains extrema and missing state");
        check(QDateTime::fromString(samples.last().toObject()["UTC"].toString(), Qt::ISODateWithMs)
                      .toMSecsSinceEpoch() == base + 100001ll * 250,
              "long recording is not truncated at 100000 samples");
        auto cancel = std::make_shared<std::atomic_bool>(false);
        auto result = features::exportSession(path, 1, directory.filePath("full-long.csv"), cancel);
        check(result.error.isEmpty() &&
                  result.rows[0].toObject()["Relevés exportés"].toInteger() == 100001,
              "complete export bypasses visual decimation");
    }
    {
        auto root = directory.filePath("preferences");
        QDir().mkpath(root);
        auto write = [](const QString &path, const QJsonObject &value) {
            QFile file(path);
            check(file.open(QIODevice::WriteOnly), "preference fixture write");
            auto bytes = QJsonDocument(value).toJson();
            check(file.write(bytes) == bytes.size(), "complete preference fixture");
        };
        write(root + "/settings.json",
              QJsonObject{{"version", 1}, {"dark", true}, {"intervalMs", 1000}});
        write(root + "/features.json",
              QJsonObject{{"schema", 1}, {"autoRecord", true}, {"alertsEnabled", true}});
        write(root + "/updates.json", QJsonObject{{"schema", 1}, {"automatic", true}});
        auto backup = directory.filePath("preferences.json");
        check(features::exportSettings(root, backup).isEmpty(), "export preference bundle");
        write(root + "/settings.json",
              QJsonObject{{"version", 1}, {"dark", false}, {"intervalMs", 500}});
        check(features::importSettings(root, backup).isEmpty(), "restore preference bundle");
        QFile restored(root + "/settings.json");
        check(restored.open(QIODevice::ReadOnly), "read restored preferences");
        check(QJsonDocument::fromJson(restored.readAll()).object()["dark"].toBool(),
              "theme restored");
        restored.close();
        QFile feature(root + "/features.json");
        check(feature.open(QIODevice::ReadOnly), "read restored features");
        auto options = QJsonDocument::fromJson(feature.readAll()).object();
        check(!options["autoRecord"].toBool() && !options["alertsEnabled"].toBool(),
              "import does not silently reactivate automation");
        check(!QDir(root).entryList({"settings-before-import-*.json"}).isEmpty(),
              "preferences preserved before import");
        auto bad = directory.filePath("bad-preferences.json");
        write(bad,
              QJsonObject{{"schema", 1},
                          {"files", QJsonObject{{"../escape.json", QJsonObject{{"schema", 1}}}}}});
        check(!features::importSettings(root, bad).isEmpty() &&
                  !QFileInfo::exists(directory.filePath("escape.json")),
              "unknown paths rejected");
        write(root + "/settings.json", QJsonObject{{"version", 99}});
        check(!features::importSettings(root, backup).isEmpty(),
              "future preferences are not overwritten");
    }
    {
        features::HistoryStore store(directory.filePath("clock.sqlite"));
        check(store.start("Clock regression fixture", 7, 10ll * 1024 * 1024),
              "start clock fixture");
        const qint64 utc = QDateTime::currentMSecsSinceEpoch(), mono = monitor::monotonicMs();
        check(store.append(utc, 1, 2, {}, mono - 100000),
              "old queued sample is ignored without stopping recording");
        auto id = store.sessions()[0].toObject()["ID"].toInteger();
        check(store.samples(id).isEmpty(), "pre-recording sample was not stored");
        check(store.append(utc, 50, 60, {}, mono), "first real source timestamp");
        check(store.append(utc - 3600000, 70, 80, {}, mono + 1000),
              "wall clock can move backwards");
        check(store.marker("Clock marker"), "monotonic marker");
        check(store.stop(), "flush clock fixture");
        auto rows = store.samples(id);
        check(rows.size() == 2 && rows[0].toObject()["CPU %"].toDouble() == 50 &&
                  rows[1].toObject()["CPU %"].toDouble() == 70,
              "capture order is preserved independently of UTC");
        check(rows[1].toObject()["Temps écoulé ms"].toInteger() -
                      rows[0].toObject()["Temps écoulé ms"].toInteger() ==
                  1000,
              "elapsed duration uses source monotonic time");
        check(store.markers(id)[0].toObject()["Temps écoulé ms"].isDouble(),
              "markers preserve elapsed time");
        auto result = features::exportSession(store.path(), id, directory.filePath("clock.json"),
                                              std::make_shared<std::atomic_bool>(false));
        check(result.error.isEmpty(), "export clock fixture");
        QFile file(directory.filePath("clock.json"));
        check(file.open(QIODevice::ReadOnly), "read clock export");
        auto samples = QJsonDocument::fromJson(file.readAll()).object()["samples"].toArray();
        check(samples[0].toObject()["cpuPercent"].toDouble() == 50 &&
                  samples[1].toObject()["elapsedMs"].toInteger() -
                          samples[0].toObject()["elapsedMs"].toInteger() ==
                      1000,
              "export retains chronology and measured elapsed duration");
    }
    {
        features::ThresholdRule sampled(90, 3, 10000, 1000);
        check(!sampled.observeSample(95, 0, 0, 7500),
              "first sensor observation starts pending interval");
        for (int now = 1000; now <= 15000; now += 1000)
            check(!sampled.observeSample(95, 0, now, 7500),
                  "repeated stale sensor value cannot satisfy sustained alert");
        check(!sampled.observeSample(95, 20000, 20000, 7500),
              "missing sensor interval resets pending duration");
        check(!sampled.observeSample(95, 25000, 25000, 7500),
              "two fresh observations are insufficient for 10 seconds");
        check(sampled.observeSample(95, 30000, 30000, 7500),
              "three genuine observations satisfy duration");
        check(!sampled.observeSample(95, 30000, 31000, 7500),
              "duplicate source observation never repeats notification");
    }
    {
        QJsonObject packet{{"capturedMonotonicMs", 1000},
                           {"capturedUtcMs", 2000},
                           {"rows", QJsonArray{QJsonObject{{"gpu", 95}}}}};
        auto batch = features::decodeTelemetry(packet, 15000);
        check(batch.valid && batch.sourceMonotonic == 1000 && batch.values["gpu"] == 95,
              "delayed telemetry retains producer timestamp");
        check(!features::decodeTelemetry(packet, 500).valid, "future telemetry rejected");
        packet.remove("capturedMonotonicMs");
        check(!features::decodeTelemetry(packet, 15000).valid, "undated telemetry rejected");
    }
    features::ThresholdRule rule(90, 3, 1000, 10000);
    check(!rule.observe(95, 0), "threshold duration starts at time zero");
    check(!rule.observe(95, 999), "duration not yet elapsed");
    check(rule.observe(95, 1000), "threshold fires after duration");
    check(!rule.observe(95, 2000), "no repeat while high");
    check(!rule.observe(std::nullopt, 3000), "missing data is not resolution");
    check(!rule.observe(88, 4000), "hysteresis");
    check(!rule.observe(80, 5000), "rearm below hysteresis");
    check(!rule.observe(95, 6000), "new sustained exceedance");
    check(!rule.observe(95, 7000), "cooldown respected");
    check(rule.observe(95, 11000), "fires after cooldown and duration");
    features::DiagnosticWindow window;
    window.append({0, 50, std::nullopt});
    window.append({1000, 100, 80});
    auto s = window.summary();
    check(s.cpu == 75 && s.ram == 80 && s.cpuCount == 2 && s.ramCount == 1,
          "valid samples counted separately");
    window.append({122000, 20, 30});
    s = window.summary();
    check(s.cpuCount == 1 && s.cpu == 20, "old diagnostics evicted");
    std::cout << "History, SQL parameter binding, retention identity, cascading deletion, "
                 "threshold timing, cooldown, hysteresis and diagnostic-window tests passed.\n";
}
