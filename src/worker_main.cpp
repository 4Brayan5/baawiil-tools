#include "features/backend.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <windows.h>
#include "runtime_lock.h"
#include "core.h"
#include <QDateTime>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    RuntimeLock runtime;
    if (!runtime.valid())
        return 1;
    app.setOrganizationName("Baawiil");
    app.setApplicationName("Monitor");
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job ||
        !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, GetCurrentProcess())) {
        if (job)
            CloseHandle(job);
        return 2;
    }
    QFile output;
    output.open(stdout, QIODevice::WriteOnly);
    auto write = [&](const features::Result &result, qint64 sourceMonotonic, qint64 sourceUtc) {
        auto bytes = QJsonDocument(QJsonObject{{"rows", result.rows},
                                               {"error", result.error},
                                               {"capturedMonotonicMs", double(sourceMonotonic)},
                                               {"capturedUtcMs", double(sourceUtc)}})
                         .toJson(QJsonDocument::Compact);
        output.write(bytes);
        output.write("\n");
        output.flush();
    };
    auto args = app.arguments();
    if (args.size() == 2 && args[1] == "--telemetry") {
        QTimer timer;
        auto collect = [&] {
            const qint64 monotonic = monitor::monotonicMs(),
                         utc = QDateTime::currentMSecsSinceEpoch();
            auto result = features::inventory("alertMetrics");
            write(result, monotonic, utc);
        };
        QObject::connect(&timer, &QTimer::timeout, &app, collect);
        timer.start(5000);
        collect();
        return app.exec();
    }
    if (args.size() != 2 || args[1] != "--request")
        return 2;
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly))
        return 2;
    auto bytes = input.read(32769);
    if (bytes.size() > 32768)
        return 2;
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return 2;
    auto request = doc.object(), parameters = request["parameters"].toObject();
    auto task = request["task"].toString();
    auto cancel = std::make_shared<std::atomic_bool>(false);
    features::Result result;
    if (task == "inventory")
        result = features::inventory(parameters["category"].toString());
    else if (task == "verify-installer")
        result.error = features::verifyInstaller(parameters["path"].toString(),
                                                 parameters["signer"].toString());
    else if (task == "handles")
        result = features::processHandles(quint32(parameters["pid"].toInteger()),
                                          parameters["creation"].toString().toULongLong());
    else if (task == "scan")
        result = features::directoryUsage(parameters["root"].toString(), cancel);
    else if (task == "benchmark")
        result = features::benchmark(parameters["kind"].toString(), parameters["seconds"].toInt(),
                                     parameters["directory"].toString(), cancel);
    else
        return 2;
    write(result, monitor::monotonicMs(), QDateTime::currentMSecsSinceEpoch());
    output.close();
    return 0;
}
