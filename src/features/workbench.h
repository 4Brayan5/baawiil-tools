#pragma once
#include "backend.h"
#include "history.h"
#include "analysis.h"
#include "../collector.h"
#include <QObject>
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSystemTrayIcon>
#include <QElapsedTimer>
#include <QToolBar>
#include <QProcess>
#include <functional>
#include <vector>

namespace features {
class DataPage : public QWidget {
    Q_OBJECT
    QTableWidget *table_;
    QLabel *status_;
    QPushButton *refresh_;
    QCheckBox *automatic_ = nullptr;
    QToolBar *bar_;
    QJsonArray rows_;
    QString category_;
    bool busy_ = false;
    std::shared_ptr<std::atomic_bool> cancel_ = std::make_shared<std::atomic_bool>(false);

  public:
    explicit DataPage(QString title, QString category = {}, QWidget *parent = nullptr);
    ~DataPage() override {
        cancel_->store(true);
    }
    void load();
    void setCategory(QString category) {
        category_ = std::move(category);
        load();
    }
    void run(std::function<Result()> task, bool keepAutomatic = false);
    void showRows(const QJsonArray &rows, QString message = {});
    void addButton(QString text, std::function<void()> action);
    QJsonObject selected() const;
    void exportRows();
    auto cancellation() const {
        return cancel_;
    }
};
class Workbench : public QObject {
    Q_OBJECT
    HistoryStore history_;
    DiagnosticWindow diagnostics_;
    std::unique_ptr<ThresholdRule> cpuRule_, ramRule_;
    QSystemTrayIcon *tray_;
    bool trayRequested_ = false;
    DataPage *historyPage_ = nullptr, *diagnosticPage_ = nullptr, *alertPage_ = nullptr,
             *benchmarkPage_ = nullptr;
    QCheckBox *alertsEnabled_ = nullptr, *sound_ = nullptr;
    QCheckBox *cpuEnabled_ = nullptr, *ramEnabled_ = nullptr, *recordCpu_ = nullptr,
              *recordRam_ = nullptr, *autoRecord_ = nullptr;
    QDoubleSpinBox *cpuThreshold_ = nullptr, *ramThreshold_ = nullptr;
    QSpinBox *duration_ = nullptr, *cooldown_ = nullptr;
    QLabel *recordingStatus_ = nullptr;
    QJsonObject preferences_;
    bool writable_ = true;
    struct CustomRule {
        QString metric;
        double threshold;
        int duration, cooldown;
        quint32 pid = 0;
        quint64 creation = 0;
        std::unique_ptr<ThresholdRule> state;
        bool enabled = true;
    };
    std::vector<CustomRule> customRules_;
    QMap<QString, double> auxiliaryMetrics_;
    qint64 auxiliaryTime_ = 0;
    qint64 auxiliaryUtc_ = 0;
    QProcess *telemetry_ = nullptr;
    QByteArray telemetryBuffer_;
    qint64 telemetryStarted_ = 0, nextTelemetryStart_ = 0;
    QStringList recordedAuxiliary_;
    QMap<quint32, quint64> identities_;
    struct Trend {
        QString name;
        quint32 pid;
        quint64 creation;
        std::deque<Observation> cpu;
        std::deque<std::pair<qint64, quint64>> memory;
    };
    QMap<QString, Trend> trends_;
    qint64 lastProcessTimestamp_ = 0;
    qint64 lastSystemSample_ = 0;
    int lastSystemInterval_ = 1000, lastProcessInterval_ = 1000;
    void savePreferences();
    void addRule();
    void rebuildRules();
    void replay(qint64 session, qint64 other = 0);
    void diagnosticReport();
    void alert(QString name, double value, double threshold, QString unit = "%",
               int durationSeconds = -1);

  public:
    explicit Workbench(QObject *parent = nullptr);
    ~Workbench() override;
    QWidget *page(const QString &category);
    void observe(const Snapshot &snapshot);
    void processMenu(QWidget *parent, const ProcessRow &row, const QPoint &globalPosition);
    bool recording() const {
        return history_.recording();
    }
    void freezePreferences() {
        writable_ = false;
    }
    void setTray(bool requested) {
        trayRequested_ = requested;
        tray_->setVisible(requested || (alertsEnabled_ && alertsEnabled_->isChecked()));
    }
};
} // namespace features
