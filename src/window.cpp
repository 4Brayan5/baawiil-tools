#include "window.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QCloseEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QToolTip>
#include <QFileDialog>
#include <QSlider>
#include "features/updates.h"
#include "features/settings_io.h"
#include <array>
#include <QColorDialog>
#include <QMenu>
#include <limits>
#include <QInputDialog>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {
QString configPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.json";
}
QString percent(std::optional<double> v) {
    return v ? QString::number(*v, 'f', 1) + " %" : QStringLiteral("Indisponible / initialisation");
}
QString mib(quint64 value) {
    return QString::number(double(value) / 1048576.0, 'f', 1) + " Mio";
}
QStandardItem *cell(QString text, QVariant sort = {}) {
    auto *item = new QStandardItem(text);
    item->setEditable(false);
    item->setData(sort.isValid() ? sort : QVariant(text), Qt::UserRole);
    return item;
}
} // namespace
Graph::Graph(QString title, QWidget *parent) : QWidget(parent), title_(std::move(title)) {
    setMinimumHeight(170);
    setAccessibleName(title_);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    animation_ = new QTimer(this);
    animation_->setInterval(16);
    connect(animation_, &QTimer::timeout, this, [this] {
        if (isVisible() && !paused_ && !recorded_)
            update();
    });
    setToolTip(QStringLiteral(
        "Charge en %, échantillons récents. Les lacunes indiquent des données indisponibles."));
}
void Graph::append(std::optional<double> value, qint64 utc, std::optional<double> sourceSeconds) {
    const auto time = std::chrono::steady_clock::now().time_since_epoch();
    values_.push({sourceSeconds.value_or(std::chrono::duration<double>(time).count()),
                  utc ? utc : QDateTime::currentMSecsSinceEpoch(), value});
    if (!paused_)
        update();
}
void Graph::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (animation_ && !paused_ && !recorded_)
        animation_->start();
}
void Graph::hideEvent(QHideEvent *event) {
    if (animation_)
        animation_->stop();
    QWidget::hideEvent(event);
}
void Graph::appendRecorded(qint64 utc, std::optional<double> value,
                           std::optional<double> elapsedSeconds) {
    recorded_ = true;
    if (animation_)
        animation_->stop();
    values_.push({elapsedSeconds.value_or(utc / 1000.0), utc, value});
    update();
}
void Graph::exportPng() {
    auto path = QFileDialog::getSaveFileName(this, "Exporter le graphique", {}, "PNG (*.png)");
    if (!path.isEmpty() && !grab().save(path, "PNG"))
        QMessageBox::warning(this, "Export", "Écriture PNG impossible.");
}
void Graph::exportCsv() {
    auto path = QFileDialog::getSaveFileName(this, "Exporter les relevés", {}, "CSV (*.csv)");
    if (path.isEmpty())
        return;
    QByteArray bytes = "utc,value,unit\r\n";
    const auto &data = paused_ ? frozen_ : values_.data();
    for (const auto &point : data)
        bytes += (QDateTime::fromMSecsSinceEpoch(point.utc).toUTC().toString(Qt::ISODateWithMs) +
                  "," + (point.value ? QString::number(*point.value, 'g', 14) : QString()) + "," +
                  unit_ + "\r\n")
                     .toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, "Export", file.errorString());
}
void Graph::mouseMoveEvent(QMouseEvent *event) {
    const auto &data = paused_ ? frozen_ : values_.data();
    if (data.empty() || width() <= 56)
        return;
    double end = lastRenderedEnd_, start = end - span_;
    double target =
        start + std::clamp((event->position().x() - 40) / (width() - 56), 0.0, 1.0) * span_;
    auto nearest = std::lower_bound(data.begin(), data.end(), target,
                                    [](const Point &p, double t) { return p.time < t; });
    if (nearest == data.end())
        return;
    QToolTip::showText(
        event->globalPosition().toPoint(),
        QDateTime::fromMSecsSinceEpoch(nearest->utc).toString("yyyy-MM-dd HH:mm:ss.zzz") + "\n" +
            (nearest->value ? QString::number(*nearest->value, 'f', 2) + " " + unit_
                            : QString("Indisponible")),
        this);
}
void Graph::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    p.setPen(palette().text().color());
    p.drawText(16, 24, title_ + (paused_ ? QStringLiteral(" — pause visuelle") : QString()));
    QRectF plot(40, 40, width() - 56, height() - 65);
    p.setPen(QColor(110, 120, 140, 70));
    for (int i = 0; i <= 4; ++i) {
        double y = plot.bottom() - plot.height() * i / 4;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    p.setPen(palette().text().color());
    p.drawText(4, 46, QString::number(maximum_, 'g', 3));
    p.drawText(15, height() - 24, "0");
    QPainterPath path;
    bool started = false;
    const auto &values = paused_ ? frozen_ : values_.data();
    const double liveEnd =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double end = values.empty() ? 0
                       : paused_
                           ? frozenEnd_ - offset_
                           : (referenceEnd_.value_or(recorded_ ? values.back().time : liveEnd) -
                              offset_),
                 start = end - span_;
    lastRenderedEnd_ = end;
    double min = std::numeric_limits<double>::infinity(), max = 0, sum = 0;
    int count = 0;
    std::array<const Point *, 4> bucket{};
    int pixel = -1;
    auto draw = [&](const Point *sample) {
        QPointF point(plot.left() + plot.width() * (sample->time - start) / span_,
                      plot.bottom() - plot.height() * *sample->value / maximum_);
        if (started)
            path.lineTo(point);
        else
            path.moveTo(point);
        started = true;
    };
    auto flush = [&] {
        if (!bucket[0])
            return;
        std::sort(bucket.begin(), bucket.end(),
                  [](const Point *a, const Point *b) { return a->time < b->time; });
        const Point *previous = nullptr;
        for (auto *point : bucket)
            if (point != previous) {
                draw(point);
                previous = point;
            }
        bucket = {};
    };
    auto begin =
        std::lower_bound(values.begin(), values.end(), start,
                         [](const Point &point, double time) { return point.time < time; });
    for (auto it = begin; it != values.end() && it->time <= end; ++it) {
        const auto &sample = *it;
        if (!sample.value) {
            flush();
            started = false;
            pixel = -1;
            continue;
        }
        min = std::min(min, *sample.value);
        max = std::max(max, *sample.value);
        sum += *sample.value;
        ++count;
        int nextPixel = int(plot.width() * (sample.time - start) / span_);
        if (nextPixel != pixel) {
            flush();
            bucket.fill(&sample);
            pixel = nextPixel;
        } else {
            if (*sample.value < *bucket[1]->value)
                bucket[1] = &sample;
            if (*sample.value > *bucket[2]->value)
                bucket[2] = &sample;
            bucket[3] = &sample;
        }
    }
    flush();
    p.setPen(QPen(palette().highlight().color(), 2));
    p.drawPath(path);
    p.setPen(palette().text().color());
    const QString statistics =
        count ? QString("%1 s • min %2 / moyenne %3 / max %4 %5 • %6 relevés valides")
                    .arg(span_)
                    .arg(min, 0, 'f', 1)
                    .arg(sum / count, 0, 'f', 1)
                    .arg(max, 0, 'f', 1)
                    .arg(unit_)
                    .arg(count)
              : QString("%1 s • aucune valeur valide").arg(span_);
    p.drawText(40, height() - 5, statistics);
    if (accessibleDescription() != statistics)
        setAccessibleDescription(statistics);
}
Window::Window() {
    setWindowTitle(QStringLiteral("Baawiil Monitor — aperçu de développement"));
    resize(1180, 800);
    auto *root = new QWidget;
    setCentralWidget(root);
    auto *layout = new QHBoxLayout(root);
    auto *nav = new QListWidget;
    nav->addItems({"Tableau de bord", "Processus", "Processeur", "Mémoire", "Carte graphique",
                   "Stockage", "Réseau", "Services Windows", "Démarrage", "Historique",
                   "Diagnostics", "Benchmarks", "Alertes", "Informations système", "Paramètres"});
    nav->setFixedWidth(185);
    nav->setAccessibleName("Navigation principale");
    layout->addWidget(nav);
    auto *pages = new QStackedWidget;
    layout->addWidget(pages, 1);
    connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    auto *dashboard = new QWidget;
    auto *d = new QVBoxLayout(dashboard);
    auto *heading = new QLabel("Vue d’ensemble");
    heading->setStyleSheet("font-size:26px;font-weight:600;");
    d->addWidget(heading);
    cpu_ = new QLabel;
    memory_ = new QLabel;
    d->addWidget(cpu_);
    d->addWidget(memory_);
    cpu_->setToolTip(
        "GetSystemTimes : temps actif rapporté à la capacité globale, pas Processor Utility.");
    memory_->setToolTip("GlobalMemoryStatusEx : mémoire physique utilisée = total − disponible.");
    cpuGraph_ = new Graph("Processeur • capacité machine");
    memoryGraph_ = new Graph("Mémoire physique utilisée");
    d->addWidget(cpuGraph_, 1);
    d->addWidget(memoryGraph_, 1);
    auto *graphBar = new QHBoxLayout;
    auto *period = new QComboBox;
    for (int seconds : {60, 300, 900, 3600, 86400})
        period->addItem(seconds < 3600 ? QString::number(seconds / 60) + " min"
                                       : QString::number(seconds / 3600) + " h",
                        seconds);
    graphBar->addWidget(new QLabel("Fenêtre"));
    graphBar->addWidget(period);
    auto *customPeriod = new QPushButton("Période…");
    graphBar->addWidget(customPeriod);
    connect(customPeriod, &QPushButton::clicked, this, [this, period] {
        bool ok = false;
        int seconds = QInputDialog::getInt(
            this, "Période personnalisée",
            "Fenêtre en secondes (les mesures absentes ne sont pas reconstruites)",
            period->currentData().toInt(), 1, 315360000, 1, &ok);
        if (!ok)
            return;
        int index = period->findData(seconds);
        if (index < 0) {
            period->addItem(QString::number(seconds) + " s", seconds);
            index = period->count() - 1;
        }
        period->setCurrentIndex(index);
    });
    auto *offset = new QSlider(Qt::Horizontal);
    offset->setRange(0, 86400);
    offset->setAccessibleName("Recul dans les graphiques, en secondes");
    graphBar->addWidget(offset);
    connect(period, &QComboBox::currentIndexChanged, this, [this, period] {
        cpuGraph_->setSpan(period->currentData().toInt());
        memoryGraph_->setSpan(period->currentData().toInt());
    });
    connect(offset, &QSlider::valueChanged, this, [this](int seconds) {
        cpuGraph_->setOffset(seconds);
        memoryGraph_->setOffset(seconds);
    });
    for (bool cpu : {true, false})
        for (bool png : {true, false}) {
            auto *button = new QPushButton(QString(cpu ? "CPU " : "RAM ") + (png ? "PNG" : "CSV"));
            graphBar->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, cpu, png] {
                auto *graph = cpu ? cpuGraph_ : memoryGraph_;
                if (png)
                    graph->exportPng();
                else
                    graph->exportCsv();
            });
        }
    d->addLayout(graphBar);
    auto *widgets = new QPushButton("Widgets…");
    graphBar->addWidget(widgets);
    auto *widgetMenu = new QMenu(widgets);
    for (auto *graph : {cpuGraph_, memoryGraph_}) {
        auto *action = widgetMenu->addAction(graph == cpuGraph_ ? "Courbe CPU" : "Courbe RAM");
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, this, [this, graph](bool shown) {
            graph->setVisible(shown);
            saveSettings();
        });
    }
    widgetMenu->addAction("Inverser l'ordre CPU/RAM", this, [this, d] {
        bool first = !property("ramFirst").toBool();
        setProperty("ramFirst", first);
        d->removeWidget(cpuGraph_);
        d->removeWidget(memoryGraph_);
        d->insertWidget(3, first ? memoryGraph_ : cpuGraph_, 1);
        d->insertWidget(4, first ? cpuGraph_ : memoryGraph_, 1);
        saveSettings();
    });
    widgets->setMenu(widgetMenu);
    auto *pause = new QPushButton("Pause des graphiques");
    pause->setCheckable(true);
    d->addWidget(pause);
    connect(pause, &QPushButton::toggled, this, [this](bool paused) {
        cpuGraph_->setPaused(paused);
        memoryGraph_->setPaused(paused);
    });
    status_ = new QLabel("Initialisation des compteurs…");
    status_->setWordWrap(true);
    d->addWidget(status_);
    pages->addWidget(dashboard);
    auto *processes = new QWidget;
    auto *pl = new QVBoxLayout(processes);
    auto *search = new QLineEdit;
    search->setPlaceholderText("Rechercher un nom, PID ou chemin…");
    search->setAccessibleName("Filtrer les processus");
    pl->addWidget(search);
    auto *exportProcesses = new QPushButton("Exporter les processus filtrés…");
    pl->addWidget(exportProcesses);
    connect(exportProcesses, &QPushButton::clicked, this, [this] {
        QJsonArray rows;
        for (int r = 0; r < proxy_->rowCount(); ++r) {
            auto source = proxy_->mapToSource(proxy_->index(r, 0)).row();
            if (source < 0 || source >= displayedProcesses_.size())
                continue;
            const auto &row = displayedProcesses_[source];
            rows.append(QJsonObject{
                {"Nom", row.name},
                {"PID", double(row.pid)},
                {"Création FILETIME",
                 row.created ? QJsonValue(QString::number(row.created)) : QJsonValue()},
                {"CPU capacité machine %", row.cpu ? QJsonValue(*row.cpu) : QJsonValue()},
                {"Résidente octets",
                 row.memoryAvailable ? QJsonValue(double(row.workingSet)) : QJsonValue()},
                {"Privée octets",
                 row.memoryAvailable ? QJsonValue(double(row.privateBytes)) : QJsonValue()},
                {"Threads", double(row.threads)},
                {"Chemin", row.path},
                {"État d'accès", row.state},
                {"Source UTC", QDateTime::fromMSecsSinceEpoch(displayedProcessTime_)
                                   .toUTC()
                                   .toString(Qt::ISODateWithMs)}});
        }
        features::DataPage exportPage("Liste des processus", {}, this);
        exportPage.showRows(rows);
        exportPage.exportRows();
    });
    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"Nom", "PID", "CPU machine %", "Résidente (Mio)",
                                       "Privée (Mio)", "Threads", "Chemin", "État"});
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setSortRole(Qt::UserRole);
    proxy_->setFilterKeyColumn(-1);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    table_ = new QTableView;
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setStretchLastSection(true);
    pl->addWidget(table_);
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_->horizontalHeader(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint &point) {
                QMenu menu;
                for (int column = 0; column < model_->columnCount(); ++column) {
                    auto *action =
                        menu.addAction(model_->headerData(column, Qt::Horizontal).toString());
                    action->setCheckable(true);
                    action->setChecked(!table_->isColumnHidden(column));
                    connect(action, &QAction::toggled, this, [this, column](bool visible) {
                        table_->setColumnHidden(column, !visible);
                        saveSettings();
                    });
                }
                menu.exec(table_->horizontalHeader()->mapToGlobal(point));
            });
    connect(search, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
    pages->addWidget(processes);
    auto *cpuPage = new QWidget;
    auto *cl = new QVBoxLayout(cpuPage);
    cl->addWidget(new QLabel("Charge par processeur logique • groupe,index • PDH Processor Time"));
    logicalCpu_ = new QLabel("Initialisation…");
    logicalCpu_->setWordWrap(true);
    logicalCpu_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cl->addWidget(logicalCpu_);
    cl->addStretch();
    pages->addWidget(cpuPage);
    auto *memoryPage = new QWidget;
    auto *ml = new QVBoxLayout(memoryPage);
    ml->addWidget(new QLabel("Mémoire physique • GlobalMemoryStatusEx"));
    memoryDetails_ = new QLabel;
    memoryDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ml->addWidget(memoryDetails_);
    ml->addStretch();
    pages->addWidget(memoryPage);
    workbench_ = new features::Workbench(this);
    for (const auto &category :
         QStringList{"gpu", "disks", "network", "services", "startup", "history", "diagnostics",
                     "benchmarks", "alerts", "system"})
        pages->addWidget(workbench_->page(category));
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto index = table_->indexAt(point);
        if (!index.isValid())
            return;
        auto source = proxy_->mapToSource(index);
        if (source.row() >= 0 && source.row() < displayedProcesses_.size())
            workbench_->processMenu(this, displayedProcesses_[source.row()],
                                    table_->viewport()->mapToGlobal(point));
    });
    auto *settings = new QWidget;
    auto *sl = new QVBoxLayout(settings);
    sl->addWidget(new QLabel("Apparence"));
    theme_ = new QComboBox;
    theme_->addItems({"Sombre", "Clair"});
    sl->addWidget(theme_);
    sl->addWidget(new QLabel("Intervalle de collecte"));
    interval_ = new QComboBox;
    for (int ms : {250, 500, 1000, 2000, 5000})
        interval_->addItem(QString::number(ms) + " ms", ms);
    interval_->setCurrentIndex(2);
    sl->addWidget(interval_);
    auto *accent = new QPushButton("Couleur d'accentuation…");
    sl->addWidget(accent);
    connect(accent, &QPushButton::clicked, this, [this] {
        auto color = QColorDialog::getColor(QColor(property("accent").toString()), this,
                                            "Accent des graphiques");
        if (color.isValid()) {
            setProperty("accent", color.name());
            applyTheme();
            saveSettings();
        }
    });
    auto *background = new QCheckBox(
        "Continuer dans la zone de notification lors de la fermeture (Quitter via l'icône)");
    sl->addWidget(background);
    connect(background, &QCheckBox::toggled, this, [this](bool enabled) {
        setProperty("trayMode", enabled);
        workbench_->setTray(enabled);
        saveSettings();
    });
    auto *startup = new QCheckBox("Lancer Baawiil à l'ouverture de ma session Windows");
    startup->setChecked(features::autoStartEnabled());
    sl->addWidget(startup);
    connect(startup, &QCheckBox::toggled, this, [this, startup](bool enabled) {
        auto error = features::setAutoStart(enabled);
        if (!error.isEmpty()) {
            QSignalBlocker blocker(startup);
            startup->setChecked(!enabled);
            QMessageBox::warning(this, "Démarrage Windows", error);
        }
    });
    sl->addWidget(new QLabel(
        "Données locales uniquement. Aucune télémétrie.\nLes capteurs matériels dépendent du "
        "fournisseur disponible. Les opérations refusées par Windows ne sont jamais contournées."));
    auto *updates = new features::Updates;
    sl->addWidget(updates, 1);
    auto *backupSettings = new QPushButton("Sauvegarder les paramètres…");
    sl->addWidget(backupSettings);
    connect(backupSettings, &QPushButton::clicked, this, [this] {
        auto path =
            QFileDialog::getSaveFileName(this, "Sauvegarder les paramètres", {}, "JSON (*.json)");
        if (path.isEmpty())
            return;
        auto error = features::exportSettings(
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation), path);
        if (!error.isEmpty())
            QMessageBox::warning(this, "Paramètres", error);
    });
    auto *restoreSettings = new QPushButton("Restaurer les paramètres…");
    sl->addWidget(restoreSettings);
    connect(restoreSettings, &QPushButton::clicked, this, [this, updates] {
        auto path = QFileDialog::getOpenFileName(this, "Restaurer une sauvegarde de confiance", {},
                                                 "JSON (*.json)");
        if (path.isEmpty())
            return;
        if (QMessageBox::question(
                this, "Restaurer les paramètres",
                "Les préférences actuelles seront sauvegardées avant remplacement. Les mises à "
                "jour automatiques, alertes et enregistrements automatiques resteront désactivés "
                "jusqu'à réactivation manuelle. Redémarrage requis. Continuer ?") !=
            QMessageBox::Yes)
            return;
        auto error = features::importSettings(
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation), path);
        if (!error.isEmpty())
            QMessageBox::warning(this, "Paramètres", error);
        else {
            setProperty("configReadOnly", true);
            workbench_->freezePreferences();
            updates->setProperty("suspendPersistence", true);
            QMessageBox::information(
                this, "Paramètres restaurés",
                "Redémarrer l'application pour utiliser les paramètres restaurés. Les anciens "
                "fichiers ont été préservés dans une sauvegarde locale.");
        }
    });
    pages->addWidget(settings);
    QString configWarning;
    QFile file(configPath());
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly) && file.size() <= 65536) {
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(file.readAll(), &error);
            auto obj = doc.object();
            const int ms = obj.value("intervalMs").toInt(-1);
            if (error.error == QJsonParseError::NoError && doc.isObject() &&
                obj.value("version").toInt() == 1 && obj.value("dark").isBool() &&
                interval_->findData(ms) >= 0) {
                theme_->setCurrentIndex(obj.value("dark").toBool() ? 0 : 1);
                interval_->setCurrentIndex(interval_->findData(ms));
                QColor color(obj["accent"].toString());
                setProperty("accent", color.isValid() ? color.name() : QString("#52b9ff"));
                {
                    QSignalBlocker blocker(background);
                    background->setChecked(obj["trayMode"].toBool());
                    setProperty("trayMode", background->isChecked());
                    workbench_->setTray(background->isChecked());
                }
                const auto header = obj["processHeader"].toString();
                if (header.size() < 16384)
                    table_->horizontalHeader()->restoreState(
                        QByteArray::fromBase64(header.toLatin1()));
                if (obj["ramFirst"].toBool()) {
                    setProperty("ramFirst", true);
                    d->removeWidget(memoryGraph_);
                    d->insertWidget(3, memoryGraph_, 1);
                }
                cpuGraph_->setVisible(obj["cpuVisible"].toBool(true));
                memoryGraph_->setVisible(obj["ramVisible"].toBool(true));
                {
                    QSignalBlocker cpuBlock(widgetMenu->actions()[0]),
                        ramBlock(widgetMenu->actions()[1]);
                    widgetMenu->actions()[0]->setChecked(!cpuGraph_->isHidden());
                    widgetMenu->actions()[1]->setChecked(!memoryGraph_->isHidden());
                }
            } else
                configWarning = "Configuration invalide ou version inconnue : fichier conservé, "
                                "préférences non enregistrées.";
        } else
            configWarning = "Configuration illisible ou trop volumineuse : fichier conservé.";
    }
    setProperty("configReadOnly", !configWarning.isEmpty());
    if (!configWarning.isEmpty()) {
        auto *warning = new QLabel(configWarning);
        warning->setWordWrap(true);
        sl->insertWidget(0, warning);
    }
    applyTheme();
    nav->setCurrentRow(0);
    connect(table_->horizontalHeader(), &QHeaderView::sectionMoved, this,
            [this] { saveSettings(); });
    collector_ = new Collector;
    collector_->moveToThread(&thread_);
    processCollector_ = new Collector(true);
    processCollector_->moveToThread(&processThread_);
    connect(&processThread_, &QThread::started, processCollector_, &Collector::start);
    connect(&processThread_, &QThread::finished, processCollector_, &QObject::deleteLater);
    connect(processCollector_, &Collector::collected, this, [this](Snapshot snapshot) {
        latest_.processes = std::move(snapshot.processes);
        latest_.processTimestamp = snapshot.timestamp;
        latest_.processMonotonicTime = snapshot.monotonicTime;
        latest_.processIntervalMs = snapshot.intervalMs;
        processError_ = snapshot.error;
    });
    connect(&thread_, &QThread::started, collector_, &Collector::start);
    connect(&thread_, &QThread::finished, collector_, &QObject::deleteLater);
    connect(collector_, &Collector::collected, this, [this](Snapshot snapshot) {
        snapshot.processes = std::move(latest_.processes);
        snapshot.processTimestamp = latest_.processTimestamp;
        snapshot.processMonotonicTime = latest_.processMonotonicTime;
        snapshot.processIntervalMs = latest_.processIntervalMs;
        latest_ = std::move(snapshot);
        if (!pending_) {
            pending_ = true;
            QTimer::singleShot(0, this, [this] {
                pending_ = false;
                render();
            });
        }
    });
    connect(theme_, &QComboBox::currentIndexChanged, this, [this] {
        applyTheme();
        saveSettings();
    });
    connect(interval_, &QComboBox::currentIndexChanged, this, [this] {
        const int ms = interval_->currentData().toInt();
        QMetaObject::invokeMethod(
            collector_, [this, ms] { collector_->setInterval(ms); }, Qt::QueuedConnection);
        saveSettings();
        QMetaObject::invokeMethod(
            processCollector_, [this, ms] { processCollector_->setInterval(std::max(500, ms)); },
            Qt::QueuedConnection);
    });
    thread_.start();
    processThread_.start();
    const int ms = interval_->currentData().toInt();
    QMetaObject::invokeMethod(
        collector_, [this, ms] { collector_->setInterval(ms); }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(
        processCollector_, [this, ms] { processCollector_->setInterval(std::max(500, ms)); },
        Qt::QueuedConnection);
    auto *watchdog = new QTimer(this);
    connect(watchdog, &QTimer::timeout, this, [this] {
        if (latest_.monotonicTime && monitor::monotonicMs() - latest_.monotonicTime >
                                         std::max(5000, interval_->currentData().toInt() * 3))
            status_->setText("Données périmées : le collecteur ne fournit plus de relevé récent.");
    });
    watchdog->start(1000);
}
Window::~Window() {
    thread_.quit();
    processThread_.quit();
    thread_.wait();
    processThread_.wait();
}
void Window::closeEvent(QCloseEvent *event) {
    if (property("trayMode").toBool() && !property("forceQuit").toBool()) {
        hide();
        event->ignore();
        return;
    }
    if (workbench_->recording() &&
        QMessageBox::question(
            this, "Enregistrement actif", "Fermer l'application et arrêter la session en cours ?",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    event->accept();
}
void Window::applyTheme() {
    const bool dark = theme_->currentIndex() == 0;
#ifdef Q_OS_WIN
    const BOOL useDark = dark ? TRUE : FALSE;
    const int rounded = 2;
    auto handle = reinterpret_cast<HWND>(winId());
    DwmSetWindowAttribute(handle, 20, &useDark, sizeof(useDark));
    DwmSetWindowAttribute(handle, 33, &rounded, sizeof(rounded));
#endif
    QColor accent(property("accent").toString());
    if (!accent.isValid())
        accent = QColor("#52b9ff");
    QPalette palette;
    palette.setColor(QPalette::Base, dark ? QColor("#202a3a") : QColor("white"));
    palette.setColor(QPalette::Text, dark ? QColor("#e4eaf5") : QColor("#182233"));
    palette.setColor(QPalette::Highlight, accent);
    qApp->setPalette(palette);
    qApp->setStyleSheet(dark ? "QWidget{background:#161c27;color:#e4eaf5;font-size:14px;} "
                               "QLineEdit,QComboBox,QTableView,QListWidget{background:#202a3a;"
                               "border:1px solid #3a4960;padding:6px;} "
                               "QPushButton{background:#263b55;padding:9px;border-radius:6px;} "
                               "QHeaderView::section{background:#263348;padding:6px;} "
                               "QListWidget::item:selected{background:#315582;}"
                             : "QWidget{background:#f3f5fa;color:#182233;font-size:14px;} "
                               "QLineEdit,QComboBox,QTableView,QListWidget{background:white;border:"
                               "1px solid #c0cad9;padding:6px;} "
                               "QPushButton{background:#d6e8ff;padding:9px;border-radius:6px;} "
                               "QHeaderView::section{background:#e2e8f0;padding:6px;} "
                               "QListWidget::item:selected{background:#bfdcff;}");
}
void Window::saveSettings() {
    if (property("configReadOnly").toBool())
        return;
    QDir().mkpath(QFileInfo(configPath()).absolutePath());
    QSaveFile file(configPath());
    const auto bytes =
        QJsonDocument(
            QJsonObject{{"version", 1},
                        {"dark", theme_->currentIndex() == 0},
                        {"intervalMs", interval_->currentData().toInt()},
                        {"accent", property("accent").toString()},
                        {"processHeader",
                         QString::fromLatin1(table_->horizontalHeader()->saveState().toBase64())},
                        {"cpuVisible", !cpuGraph_->isHidden()},
                        {"ramVisible", !memoryGraph_->isHidden()},
                        {"ramFirst", property("ramFirst").toBool()},
                        {"trayMode", property("trayMode").toBool()}})
            .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        status_->setText("Impossible d’enregistrer les préférences.");
}
void Window::render() {
    workbench_->observe(latest_);
    cpu_->setText("CPU : " + percent(latest_.cpu));
    cpu_->setToolTip(latest_.cpuSource);
    memory_->setText(latest_.totalMemory
                         ? "RAM : " + mib(latest_.totalMemory - latest_.availableMemory) + " / " +
                               mib(latest_.totalMemory)
                         : "RAM : indisponible");
    QStringList logical;
    for (const auto &item : latest_.logicalCpu)
        logical.append(item.first + " : " + QString::number(item.second, 'f', 1) + " %");
    logicalCpu_->setText(
        QString("%1 cœurs physiques • %2 processeurs logiques • %3 processus • %4 threads\n\n")
            .arg(latest_.topologyAvailable ? QString::number(latest_.physicalCores)
                                           : QString("indisponible"))
            .arg(latest_.logicalProcessors ? QString::number(latest_.logicalProcessors)
                                           : QString("indisponible"))
            .arg(latest_.performanceAvailable ? QString::number(latest_.systemProcesses)
                                              : QString("indisponible"))
            .arg(latest_.performanceAvailable ? QString::number(latest_.systemThreads)
                                              : QString("indisponible")) +
        (logical.isEmpty() ? QStringLiteral("Compteurs indisponibles / initialisation")
                           : logical.join("    •    ")));
    memoryDetails_->setText(
        latest_.totalMemory
            ? QString("Total accessible à Windows : %1\nDisponible : %2\nUtilisée : %3\nCache "
                      "système : %4\nEngagement : %5 / %6\n\nLe total peut différer de la RAM "
                      "physiquement installée.")
                  .arg(mib(latest_.totalMemory), mib(latest_.availableMemory),
                       mib(latest_.totalMemory - latest_.availableMemory),
                       latest_.performanceAvailable ? mib(latest_.systemCache)
                                                    : QString("Indisponible"),
                       latest_.performanceAvailable ? mib(latest_.committedMemory)
                                                    : QString("Indisponible"),
                       latest_.performanceAvailable ? mib(latest_.commitLimit)
                                                    : QString("Indisponible"))
            : QStringLiteral("Indisponible"));
    cpuGraph_->append(latest_.cpu, latest_.timestamp, latest_.monotonicTime / 1000.0);
    memoryGraph_->append(
        latest_.totalMemory
            ? std::optional<double>(100.0 * double(latest_.totalMemory - latest_.availableMemory) /
                                    double(latest_.totalMemory))
            : std::nullopt,
        latest_.timestamp, latest_.monotonicTime / 1000.0);
    status_->setText(QString::number(latest_.processes.size()) + " processus • " +
                     QDateTime::fromMSecsSinceEpoch(latest_.timestamp).toString("HH:mm:ss") +
                     " • " + latest_.error + " " + processError_);
    if (latest_.processMonotonicTime && monitor::monotonicMs() - latest_.processMonotonicTime >
                                            std::max(5000, interval_->currentData().toInt() * 3))
        status_->setText(status_->text() + " • inventaire processus périmé");
    status_->setText(status_->text() + QString(" • système actif %1 h %2 min")
                                           .arg(latest_.uptimeMs / 3600000)
                                           .arg((latest_.uptimeMs / 60000) % 60));
    if (!table_->isVisible())
        return; // Avoid rebuilding a hidden process table.
    QString selected;
    if (table_->currentIndex().isValid())
        selected = proxy_->index(table_->currentIndex().row(), 0).data(Qt::UserRole + 1).toString();
    proxy_->setDynamicSortFilter(false);
    model_->removeRows(0, model_->rowCount());
    displayedProcesses_ = latest_.processes;
    displayedProcessTime_ = latest_.processTimestamp;
    for (const auto &row : displayedProcesses_) {
        QList<QStandardItem *> cells{
            cell(row.name),
            cell(QString::number(row.pid), row.pid),
            cell(row.cpu ? QString::number(*row.cpu, 'f', 1) : "—", row.cpu ? *row.cpu : -1.0),
            cell(row.memoryAvailable ? mib(row.workingSet) : "—", double(row.workingSet)),
            cell(row.memoryAvailable ? mib(row.privateBytes) : "—", double(row.privateBytes)),
            cell(QString::number(row.threads), row.threads),
            cell(row.path),
            cell(row.state)};
        cells[0]->setData(
            row.created ? QString::number(row.pid) + ":" + QString::number(row.created) : QString(),
            Qt::UserRole + 1);
        model_->appendRow(cells);
    }
    proxy_->setDynamicSortFilter(true);
    proxy_->invalidate();
    if (!selected.isEmpty())
        for (int row = 0; row < proxy_->rowCount(); ++row)
            if (proxy_->index(row, 0).data(Qt::UserRole + 1).toString() == selected) {
                table_->selectRow(row);
                break;
            }
}
