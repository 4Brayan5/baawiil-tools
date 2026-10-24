#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QTimer>
#include <chrono>
#include <map>
#include <optional>
#include "core.h"

struct ProcessRow {
    quint32 pid = 0;
    quint64 created = 0, workingSet = 0, privateBytes = 0;
    quint32 threads = 0;
    QString name, path, state;
    std::optional<double> cpu;
    bool memoryAvailable = false;
};
struct Snapshot {
    qint64 timestamp = 0;
    qint64 processTimestamp = 0;
    qint64 monotonicTime = 0, processMonotonicTime = 0;
    int intervalMs = 1000, processIntervalMs = 1000;
    std::optional<double> cpu;
    QVector<QPair<QString, double>> logicalCpu;
    quint64 totalMemory = 0, availableMemory = 0;
    quint64 committedMemory = 0, commitLimit = 0, systemCache = 0, uptimeMs = 0;
    quint32 physicalCores = 0, logicalProcessors = 0, systemProcesses = 0, systemThreads = 0;
    bool performanceAvailable = false, topologyAvailable = false;
    QString cpuSource;
    QString error;
    QVector<ProcessRow> processes;
};
Q_DECLARE_METATYPE(Snapshot)

class Collector : public QObject {
    Q_OBJECT
    QTimer *timer_ = nullptr;
    bool processMode_ = false;
    std::optional<qint64> lastSampleMonotonic_;
    std::optional<monitor::CpuTimes> previous_;
    void *pdhQuery_ = nullptr;
    void *pdhCounter_ = nullptr;
    struct Previous {
        quint64 created, ticks;
        std::chrono::steady_clock::time_point time;
    };
    std::map<quint32, Previous> processes_;

  public:
    explicit Collector(bool processMode = false) : processMode_(processMode) {}
    ~Collector() override;
  public slots:
    void start();
    void setInterval(int milliseconds);
    void sample();
  signals:
    void collected(Snapshot snapshot);
};
