#pragma once
#include "collector.h"
#include <QMainWindow>
#include <QThread>
#include <QLabel>
#include <QComboBox>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QPushButton>
#include "features/workbench.h"

class Graph : public QWidget {
    struct Point {
        double time;
        qint64 utc;
        std::optional<double> value;
    };
    monitor::History<Point> values_{360001};
    bool paused_ = false;
    std::deque<Point> frozen_;
    QString title_;
    int span_ = 60;
    int offset_ = 0;
    double maximum_ = 100;
    QString unit_ = "%";
    std::optional<double> referenceEnd_;
    QTimer *animation_ = nullptr;
    bool recorded_ = false;
    double lastRenderedEnd_ = 0, frozenEnd_ = 0;

  public:
    explicit Graph(QString title, QWidget *parent = nullptr);
    void append(std::optional<double> value, qint64 utc = 0,
                std::optional<double> sourceSeconds = {});
    void appendRecorded(qint64 utc, std::optional<double> value,
                        std::optional<double> elapsedSeconds = {});
    void setSpan(int seconds) {
        span_ = std::max(1, seconds);
        if (animation_)
            animation_->setInterval(span_ <= 300 ? 16 : 1000);
        offset_ = 0;
        update();
    }
    void setOffset(int seconds) {
        offset_ = seconds;
        update();
    }
    void setScale(double maximum, QString unit) {
        maximum_ = std::max(1.0, maximum);
        unit_ = std::move(unit);
        update();
    }
    void setReferenceEnd(double seconds) {
        referenceEnd_ = seconds;
        update();
    }
    void exportCsv();
    void exportPng();
    void setPaused(bool paused) {
        if (paused && !paused_) {
            frozen_ = values_.data();
            frozenEnd_ = lastRenderedEnd_ + offset_;
        }
        paused_ = paused;
        if (animation_) {
            if (paused_ || recorded_)
                animation_->stop();
            else if (isVisible())
                animation_->start();
        }
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
};
class Window : public QMainWindow {
    Q_OBJECT
    QThread thread_;
    QThread processThread_;
    Collector *collector_;
    Collector *processCollector_;
    QString processError_;
    QLabel *cpu_, *memory_, *status_;
    QLabel *logicalCpu_, *memoryDetails_;
    Graph *cpuGraph_, *memoryGraph_;
    QStandardItemModel *model_;
    QSortFilterProxyModel *proxy_;
    QTableView *table_;
    QComboBox *theme_, *interval_;
    bool pending_ = false;
    Snapshot latest_;
    QVector<ProcessRow> displayedProcesses_;
    qint64 displayedProcessTime_ = 0;
    features::Workbench *workbench_;
    void render();
    void applyTheme();
    void saveSettings();

  public:
    Window();
    ~Window() override;

  protected:
    void closeEvent(QCloseEvent *event) override;
};
