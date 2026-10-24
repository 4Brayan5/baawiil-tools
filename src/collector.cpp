#include "collector.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <QDateTime>
#include <memory>
#include <vector>
#include <cstddef>

namespace {
struct CloseHandleDeleter {
    void operator()(void *value) const {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
using Handle = std::unique_ptr<void, CloseHandleDeleter>;
quint64 ticks(FILETIME time) {
    return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
} // namespace
Collector::~Collector() {
    if (pdhQuery_)
        PdhCloseQuery(pdhQuery_);
}
void Collector::start() {
    if (!processMode_ && PdhOpenQueryW(nullptr, 0, &pdhQuery_) == ERROR_SUCCESS) {
        if (PdhAddEnglishCounterW(pdhQuery_, L"\\Processor Information(*)\\% Processor Time", 0,
                                  &pdhCounter_) != ERROR_SUCCESS) {
            PdhCloseQuery(pdhQuery_);
            pdhQuery_ = nullptr;
        }
    }
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &Collector::sample);
    timer_->start(1000);
    sample();
}
void Collector::setInterval(int ms) {
    if (timer_ && (ms == 250 || ms == 500 || ms == 1000 || ms == 2000 || ms == 5000))
        timer_->setInterval(ms);
}
void Collector::sample() {
    Snapshot result;
    result.timestamp = QDateTime::currentMSecsSinceEpoch();
    result.monotonicTime = monitor::monotonicMs();
    result.intervalMs = timer_ ? timer_->interval() : 1000;
    const bool interrupted = lastSampleMonotonic_ && result.monotonicTime - *lastSampleMonotonic_ >
                                                         std::max(1500, 3 * result.intervalMs);
    if (interrupted) {
        previous_.reset();
        processes_.clear();
    }
    lastSampleMonotonic_ = result.monotonicTime;
    if (!processMode_) {
        if (pdhQuery_ && PdhCollectQueryData(pdhQuery_) == ERROR_SUCCESS) {
            DWORD bytes = 0, count = 0;
            if (PdhGetFormattedCounterArrayW(pdhCounter_, PDH_FMT_DOUBLE, &bytes, &count,
                                             nullptr) == PDH_MORE_DATA &&
                bytes) {
                std::vector<std::max_align_t> buffer((bytes + sizeof(std::max_align_t) - 1) /
                                                     sizeof(std::max_align_t));
                auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
                if (PdhGetFormattedCounterArrayW(pdhCounter_, PDH_FMT_DOUBLE, &bytes, &count,
                                                 items) == ERROR_SUCCESS) {
                    for (DWORD i = 0; i < count; ++i) {
                        const auto &item = items[i];
                        if (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                            item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
                            continue;
                        QString name = QString::fromWCharArray(item.szName);
                        if (!name.contains("_Total") && std::isfinite(item.FmtValue.doubleValue))
                            result.logicalCpu.append(
                                {name, std::clamp(item.FmtValue.doubleValue, 0.0, 100.0)});
                    }
                }
            }
        }
        if (interrupted)
            result.logicalCpu.clear();
        FILETIME idle{}, kernel{}, user{};
        if (GetActiveProcessorGroupCount() > 1) {
            if (!result.logicalCpu.isEmpty() &&
                result.logicalCpu.size() == GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)) {
                double total = 0;
                for (const auto &item : result.logicalCpu)
                    total += item.second;
                result.cpu = total / result.logicalCpu.size();
                result.cpuSource = "Moyenne PDH Processor Time de tous les processeurs logiques";
            } else
                result.error =
                    QStringLiteral("CPU global indisponible : compteurs des groupes incomplets.");
            previous_.reset();
        } else if (GetSystemTimes(&idle, &kernel, &user)) {
            monitor::CpuTimes current{ticks(idle), ticks(kernel), ticks(user)};
            if (previous_)
                result.cpu = monitor::cpuUsage(*previous_, current);
            result.cpuSource = "GetSystemTimes : temps actif, capacité machine";
            previous_ = current;
        } else {
            previous_.reset();
            result.error = QStringLiteral("Lecture CPU impossible.");
        }
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory)) {
            result.totalMemory = memory.ullTotalPhys;
            result.availableMemory = memory.ullAvailPhys;
        } else
            result.error += QStringLiteral(" Lecture mémoire impossible.");
        PERFORMANCE_INFORMATION performance{};
        performance.cb = sizeof(performance);
        if (GetPerformanceInfo(&performance, sizeof(performance))) {
            result.performanceAvailable = true;
            result.committedMemory = quint64(performance.CommitTotal) * performance.PageSize;
            result.commitLimit = quint64(performance.CommitLimit) * performance.PageSize;
            result.systemCache = quint64(performance.SystemCache) * performance.PageSize;
            result.systemProcesses = performance.ProcessCount;
            result.systemThreads = performance.ThreadCount;
        }
        result.logicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        result.uptimeMs = GetTickCount64();
        DWORD bytes = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
        std::vector<BYTE> topology(bytes);
        if (bytes &&
            GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(topology.data()),
                &bytes)) {
            for (DWORD offset = 0; offset < bytes;) {
                auto *info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                    topology.data() + offset);
                if (!info->Size || info->Size > bytes - offset)
                    break;
                ++result.physicalCores;
                offset += info->Size;
            }
            result.topologyAvailable = result.physicalCores > 0;
        }

        emit collected(std::move(result));
        return;
    }
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    std::map<quint32, Previous> next;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const unsigned processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (snapshot.get() != INVALID_HANDLE_VALUE && Process32FirstW(snapshot.get(), &entry)) {
        do {
            ProcessRow row;
            row.pid = entry.th32ProcessID;
            row.name = QString::fromWCharArray(entry.szExeFile);
            row.threads = entry.cntThreads;
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid));
            if (process) {
                wchar_t path[32768];
                DWORD size = 32768;
                if (QueryFullProcessImageNameW(process.get(), 0, path, &size))
                    row.path = QString::fromWCharArray(path, int(size));
                FILETIME created{}, exited{}, k{}, u{};
                if (GetProcessTimes(process.get(), &created, &exited, &k, &u)) {
                    row.created = ticks(created);
                    const auto now = std::chrono::steady_clock::now();
                    const auto total = ticks(k) + ticks(u);
                    auto old = processes_.find(row.pid);
                    if (old != processes_.end() && old->second.created == row.created)
                        row.cpu = monitor::processUsage(
                            old->second.ticks, total,
                            std::chrono::duration<double>(now - old->second.time).count(),
                            processors);
                    next.emplace(row.pid, Previous{row.created, total, now});
                }
                PROCESS_MEMORY_COUNTERS_EX counters{};
                counters.cb = sizeof(counters);
                BOOL gotMemory = GetProcessMemoryInfo(
                    process.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                    sizeof(counters));
                if (!gotMemory) {
                    Handle memoryHandle(OpenProcess(
                        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, row.pid));
                    if (memoryHandle)
                        gotMemory = GetProcessMemoryInfo(
                            memoryHandle.get(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                            sizeof(counters));
                }
                if (gotMemory) {
                    row.workingSet = counters.WorkingSetSize;
                    row.privateBytes = counters.PrivateUsage;
                    row.memoryAvailable = true;
                }
                row.state = QStringLiteral("Accessible");
            } else
                row.state = QStringLiteral("Accès limité / processus terminé");
            result.processes.push_back(std::move(row));
        } while (Process32NextW(snapshot.get(), &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES)
            result.error += QStringLiteral(" Liste des processus partielle.");
    } else
        result.error += QStringLiteral(" Inventaire des processus indisponible.");
    processes_ = std::move(next);
    emit collected(std::move(result));
}
