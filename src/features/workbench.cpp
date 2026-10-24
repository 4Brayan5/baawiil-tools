#include "workbench.h"
#include "log.h"
#include "telemetry.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>
#include <QDateTime>
#include <QStyle>
#include <QTimer>
#include <QFileInfo>
#include <QTextDocument>
#include <QPrinter>
#include "../window.h"
#include <QDir>
#include <QSet>
#include <QProgressBar>
#include <limits>
#include <QSlider>
#include <QScrollArea>

namespace features {
namespace {
QString text(QJsonValue value) {
    if (value.isString())
        return value.toString();
    if (value.isNull() || value.isUndefined())
        return "Indisponible";
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isBool())
        return value.toBool() ? "Oui" : "Non";
    return QString::number(value.toDouble(), 'g', 14);
}
QString csv(QString value) {
    if (!value.isEmpty() && QString("=+-@\t\r").contains(value.front()))
        value.prepend('\'');
    value.replace('"', "\"\"");
    return '"' + value + '"';
}
QString html(const QJsonArray &rows) {
    QString result = "<!doctype html><meta charset=\"utf-8\"><title>Rapport "
                     "Baawiil</title><h1>Rapport local Baawiil</h1><table border=\"1\">";
    for (auto row : rows) {
        result += "<tr>";
        auto obj = row.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            result += "<td><b>" + it.key().toHtmlEscaped() + "</b><br>" +
                      text(it.value()).toHtmlEscaped() + "</td>";
        result += "</tr>";
    }
    return result + "</table>";
}
} // namespace
DataPage::DataPage(QString title, QString category, QWidget *parent)
    : QWidget(parent), category_(std::move(category)) {
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(title);
    heading->setStyleSheet("font-size:23px;font-weight:600;");
    layout->addWidget(heading);
    bar_ = new QToolBar;
    bar_->setMovable(false);
    layout->addWidget(bar_);
    refresh_ = new QPushButton("Actualiser");
    bar_->addWidget(refresh_);
    connect(refresh_, &QPushButton::clicked, this, &DataPage::load);
    auto *exportButton = new QPushButton("Exporter…");
    bar_->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, this, &DataPage::exportRows);
    if (!category_.isEmpty()) {
        auto *automatic = automatic_ = new QCheckBox("Actualiser toutes les 5 s");
        bar_->addWidget(automatic);
        auto *timer = new QTimer(this);
        timer->setInterval(5000);
        connect(automatic, &QCheckBox::toggled, timer, [timer](bool enabled) {
            if (enabled)
                timer->start();
            else
                timer->stop();
        });
        connect(timer, &QTimer::timeout, this, [this] {
            if (isVisible())
                load();
        });
    }
    auto *search = new QLineEdit;
    search->setPlaceholderText("Filtrer les résultats…");
    search->setAccessibleName("Filtrer les résultats");
    layout->addWidget(search);
    table_ = new QTableWidget;
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionsMovable(true);
    layout->addWidget(table_, 1);
    status_ = new QLabel(category_.isEmpty()
                             ? "Utiliser les commandes ci-dessus."
                             : "Cliquer sur Actualiser pour lire les informations locales.");
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &value) {
        for (int r = 0; r < table_->rowCount(); ++r) {
            bool match = false;
            for (int c = 0; c < table_->columnCount(); ++c)
                if (table_->item(r, c) &&
                    table_->item(r, c)->text().contains(value, Qt::CaseInsensitive))
                    match = true;
            table_->setRowHidden(r, !match);
        }
    });
    if (category_.isEmpty())
        refresh_->setEnabled(false);
}
void DataPage::addButton(QString text, std::function<void()> action) {
    auto *command = bar_->addAction(text);
    connect(command, &QAction::triggered, this, std::move(action));
}
void DataPage::load() {
    if (!category_.isEmpty()) {
        cancel_->store(false);
        run(
            [category = category_, cancel = cancel_] {
                return isolatedTask("inventory", QJsonObject{{"category", category}}, cancel);
            },
            true);
    }
}
void DataPage::run(std::function<Result()> task, bool keepAutomatic) {
    if (!keepAutomatic && automatic_)
        automatic_->setChecked(false);
    if (busy_)
        return;
    busy_ = true;
    refresh_->setEnabled(false);
    status_->setText("Opération en cours…");
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
        auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        refresh_->setEnabled(!category_.isEmpty());
        showRows(result.rows, result.error);
    });
    watcher->setFuture(QtConcurrent::run([task = std::move(task)] {
        try {
            return task();
        } catch (const std::exception &error) {
            return Result{{}, QString::fromUtf8(error.what())};
        } catch (...) {
            return Result{{}, "Erreur inattendue du module."};
        }
    }));
}
void DataPage::showRows(const QJsonArray &rows, QString message) {
    if (!message.isEmpty())
        logEvent(category_.isEmpty() ? "local-report" : category_,
                 rows.isEmpty() ? "empty-result-with-message" : "result-with-message");
    rows_ = rows;
    QStringList columns;
    for (auto row : rows) {
        auto obj = row.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            if (!columns.contains(it.key()))
                columns.append(it.key());
    }
    table_->setSortingEnabled(false);
    table_->clear();
    const int displayed = std::min<int>(10000, int(rows.size()));
    table_->setRowCount(displayed);
    table_->setColumnCount(int(columns.size()));
    table_->setHorizontalHeaderLabels(columns);
    for (int r = 0; r < displayed; ++r) {
        auto obj = rows[r].toObject();
        for (int c = 0; c < columns.size(); ++c) {
            auto value = obj.value(columns[c]);
            auto *item = new QTableWidgetItem;
            if (value.isDouble())
                item->setData(Qt::DisplayRole, value.toDouble());
            else
                item->setText(text(value));
            item->setData(Qt::UserRole, r);
            table_->setItem(r, c, item);
            if (columns[c] == "Part du périmètre %" && value.isDouble()) {
                auto *bar = new QProgressBar;
                bar->setRange(0, 1000);
                bar->setValue(qRound(std::clamp(value.toDouble(), 0.0, 100.0) * 10));
                bar->setFormat(QString::number(value.toDouble(), 'f', 2) + " %");
                bar->setAccessibleName("Part du périmètre analysé");
                table_->setCellWidget(r, c, bar);
            }
        }
    }
    table_->setSortingEnabled(true);
    table_->resizeColumnsToContents();
    status_->setText(QString::number(rows.size()) + " résultats. " + message);
    if (rows.size() > displayed)
        status_->setText(status_->text() +
                         " • Affichage limité à 10 000 lignes ; CSV/JSON conservent les données de "
                         "cette vue, et l'export de session complet conserve tous les originaux.");
}
QJsonObject DataPage::selected() const {
    int row = table_->currentRow();
    if (row < 0 || !table_->item(row, 0))
        return {};
    int index = table_->item(row, 0)->data(Qt::UserRole).toInt();
    return index >= 0 && index < rows_.size() ? rows_[index].toObject() : QJsonObject();
}
void DataPage::exportRows() {
    if (rows_.isEmpty()) {
        QMessageBox::information(this, "Export", "Aucune donnée à exporter.");
        return;
    }
    auto path = QFileDialog::getSaveFileName(
        this, "Exporter les données", {}, "JSON (*.json);;CSV (*.csv);;HTML (*.html);;PDF (*.pdf)");
    if (path.isEmpty())
        return;
    if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
        QPrinter printer;
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        QTextDocument doc;
        doc.setHtml(html(rows_));
        doc.print(&printer);
        return;
    }
    QByteArray bytes;
    if (path.endsWith(".html", Qt::CaseInsensitive))
        bytes = html(rows_).toUtf8();
    else if (path.endsWith(".csv", Qt::CaseInsensitive)) {
        QStringList columns;
        for (auto row : rows_) {
            auto obj = row.toObject();
            for (auto it = obj.begin(); it != obj.end(); ++it)
                if (!columns.contains(it.key()))
                    columns.append(it.key());
        }
        QStringList lines, header;
        for (const auto &col : columns)
            header.append(csv(col));
        lines.append(header.join(','));
        for (auto row : rows_) {
            QStringList values;
            for (const auto &col : columns)
                values.append(csv(text(row.toObject().value(col))));
            lines.append(values.join(','));
        }
        bytes = (lines.join("\r\n") + "\r\n").toUtf8();
    } else
        bytes =
            QJsonDocument(QJsonObject{{"schema", 1},
                                      {"exportedUtc",
                                       QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                      {"rows", rows_}})
                .toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, "Export", file.errorString());
}
Workbench::Workbench(QObject *parent)
    : QObject(parent),
      history_(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
               "/history.sqlite") {
    tray_ = new QSystemTrayIcon(qApp->style()->standardIcon(QStyle::SP_ComputerIcon), this);
    tray_->setToolTip("Baawiil Monitor • surveillance locale");
    if (auto *window = qobject_cast<QWidget *>(parent)) {
        auto *menu = new QMenu(window);
        menu->addAction("Afficher", window, [window] {
            window->showNormal();
            window->raise();
            window->activateWindow();
        });
        menu->addAction("Quitter…", window, [window] {
            window->setProperty("forceQuit", true);
            window->close();
            window->setProperty("forceQuit", false);
        });
        tray_->setContextMenu(menu);
        connect(tray_, &QSystemTrayIcon::activated, window,
                [window](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::DoubleClick) {
                        window->showNormal();
                        window->raise();
                    }
                });
    }
    QFile config(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                 "/features.json");
    if (config.exists()) {
        if (config.open(QIODevice::ReadOnly) && config.size() < 65536) {
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(config.readAll(), &error);
            if (error.error == QJsonParseError::NoError && doc.object()["schema"].toInt() == 1)
                preferences_ = doc.object();
            else
                writable_ = false;
        } else
            writable_ = false;
    }
    rebuildRules();
    for (auto key : preferences_["recordAuxiliary"].toArray())
        if (QStringList{"gpu-percent", "gpu-temperature", "disk-used-percent", "network-loss",
                        "network-receive-bytes-per-sec", "network-send-bytes-per-sec",
                        "disk-read-bytes-per-sec", "disk-write-bytes-per-sec"}
                .contains(key.toString()))
            recordedAuxiliary_.append(key.toString());
    telemetry_ = new QProcess(this);
#ifdef Q_OS_WIN
    telemetry_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000u; });
#endif
    connect(telemetry_, &QProcess::readyReadStandardOutput, this, [this] {
        telemetryBuffer_ += telemetry_->readAllStandardOutput();
        if (telemetryBuffer_.size() > 1024 * 1024) {
            telemetry_->kill();
            telemetryBuffer_.clear();
            auxiliaryMetrics_.clear();
            return;
        }
        for (int newline = telemetryBuffer_.indexOf('\n'); newline >= 0;
             newline = telemetryBuffer_.indexOf('\n')) {
            auto line = telemetryBuffer_.left(newline);
            telemetryBuffer_.remove(0, newline + 1);
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(line, &error);
            auto batch = error.error == QJsonParseError::NoError
                             ? decodeTelemetry(doc.object(), monitor::monotonicMs())
                             : TelemetryBatch{};
            auxiliaryMetrics_.clear();
            if (!batch.valid) {
                logEvent("telemetry", "invalid-source-metadata");
                continue;
            }
            auxiliaryMetrics_ = std::move(batch.values);
            auxiliaryTime_ = batch.sourceMonotonic;
            auxiliaryUtc_ = batch.sourceUtc;
        }
    });
    connect(telemetry_, &QProcess::finished, this, [this] {
        auxiliaryMetrics_.clear();
        nextTelemetryStart_ = monitor::monotonicMs() + 30000;
    });
    auto *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this] {
        const bool needed =
            (alertsEnabled_ && alertsEnabled_->isChecked() && !customRules_.empty()) ||
            (history_.recording() && !recordedAuxiliary_.isEmpty());
        auto now = monitor::monotonicMs();
        if (!needed) {
            if (telemetry_->state() != QProcess::NotRunning)
                telemetry_->kill();
            return;
        }
        if (telemetry_->state() == QProcess::NotRunning) {
            auto executable = QCoreApplication::applicationDirPath() + "/baawiil-worker.exe";
            if (now >= nextTelemetryStart_ && QFileInfo::exists(executable)) {
                telemetryBuffer_.clear();
                telemetryStarted_ = now;
                auxiliaryTime_ = 0;
                telemetry_->start(executable, {"--telemetry"});
                nextTelemetryStart_ = now + 30000;
            }
        } else if (now - std::max(auxiliaryTime_, telemetryStarted_) > 15000) {
            telemetry_->kill();
            auxiliaryMetrics_.clear();
        }
    });
    timer->start();
}
Workbench::~Workbench() {
    if (telemetry_ && telemetry_->state() != QProcess::NotRunning) {
        telemetry_->kill();
        telemetry_->waitForFinished(3000);
    }
}
void Workbench::savePreferences() {
    if (!writable_)
        return;
    preferences_["schema"] = 1;
    QJsonArray rules;
    for (const auto &rule : customRules_)
        rules.append(QJsonObject{{"metric", rule.metric},
                                 {"threshold", rule.threshold},
                                 {"duration", rule.duration},
                                 {"cooldown", rule.cooldown},
                                 {"pid", double(rule.pid)},
                                 {"creation", QString::number(rule.creation)},
                                 {"enabled", rule.enabled}});
    preferences_["rules"] = rules;
    if (alertsEnabled_) {
        preferences_["alertsEnabled"] = alertsEnabled_->isChecked();
        preferences_["sound"] = sound_->isChecked();
        preferences_["cpuThreshold"] = cpuThreshold_->value();
        preferences_["ramThreshold"] = ramThreshold_->value();
        preferences_["duration"] = duration_->value();
        preferences_["cooldown"] = cooldown_->value();
    }
    if (cpuEnabled_) {
        preferences_["cpuEnabled"] = cpuEnabled_->isChecked();
        preferences_["ramEnabled"] = ramEnabled_->isChecked();
    }
    if (recordCpu_) {
        preferences_["recordCpu"] = recordCpu_->isChecked();
        preferences_["recordRam"] = recordRam_->isChecked();
        preferences_["autoRecord"] = autoRecord_->isChecked();
    }
    QJsonArray extra;
    for (const auto &key : recordedAuxiliary_)
        extra.append(key);
    preferences_["recordAuxiliary"] = extra;
    auto path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(path);
    QSaveFile file(path + "/features.json");
    auto data = QJsonDocument(preferences_).toJson();
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
        file.commit();
}
void Workbench::rebuildRules() {
    customRules_.clear();
    for (auto item : preferences_["rules"].toArray()) {
        if (customRules_.size() >= 64)
            break;
        auto obj = item.toObject();
        QString metric = obj["metric"].toString();
        double threshold = obj["threshold"].toDouble(-1);
        int duration = obj["duration"].toInt(), cooldown = obj["cooldown"].toInt();
        if (!QStringList{"gpu-percent", "gpu-temperature", "disk-used-percent", "network-loss",
                         "process-cpu"}
                 .contains(metric) ||
            !std::isfinite(threshold) || threshold < 0 || threshold > 200 || duration < 1 ||
            duration > 3600 || cooldown < 1 || cooldown > 86400)
            continue;
        customRules_.push_back(
            {metric, threshold, duration, cooldown, quint32(obj["pid"].toInteger()),
             obj["creation"].toString().toULongLong(),
             std::make_unique<ThresholdRule>(threshold, 3, duration * 1000ll, cooldown * 1000ll)});
        customRules_.back().enabled = obj["enabled"].toBool(true);
    }
}
void Workbench::addRule() {
    if (customRules_.size() >= 64)
        return;
    bool ok = false;
    auto metric = QInputDialog::getItem(
        alertPage_, "Nouvelle alerte",
        "Mesure : GPU %, température GPU °C, occupation volume système %, perte réseau (100 = "
        "déconnecté), ou CPU processus %",
        {"gpu-percent", "gpu-temperature", "disk-used-percent", "network-loss", "process-cpu"}, 0,
        false, &ok);
    if (!ok)
        return;
    double threshold = QInputDialog::getDouble(
        alertPage_, "Seuil", "Seuil supérieur ou égal (unité selon la mesure)", 90, 1, 200, 1, &ok);
    if (!ok)
        return;
    quint32 pid = 0;
    quint64 creation = 0;
    if (metric == "process-cpu") {
        int selected = QInputDialog::getInt(alertPage_, "Processus",
                                            "PID actuel (la date de création sera liée à la règle)",
                                            1, 1, 2147483647, 1, &ok);
        if (!ok)
            return;
        pid = quint32(selected);
        creation = identities_.value(pid);
        if (!creation) {
            QMessageBox::warning(
                alertPage_, "Processus",
                "PID absent ou date de création inaccessible dans le dernier relevé.");
            return;
        }
    }
    customRules_.push_back(
        {metric, threshold, duration_->value(), cooldown_->value(), pid, creation,
         std::make_unique<ThresholdRule>(threshold, 3, duration_->value() * 1000ll,
                                         cooldown_->value() * 1000ll)});
    savePreferences();
    QJsonArray rows;
    for (const auto &rule : customRules_)
        rows.append(QJsonObject{{"Mesure", rule.metric},
                                {"Seuil", rule.threshold},
                                {"Durée s", rule.duration},
                                {"Temporisation s", rule.cooldown},
                                {"PID", double(rule.pid)}});
    alertPage_->showRows(rows, "Les règles sur capteurs indisponibles restent muettes ; elles ne "
                               "traitent pas l'absence comme un zéro.");
}
QWidget *Workbench::page(const QString &category) {
    if (category == "history") {
        auto *p = historyPage_ = new DataPage("Historique et enregistrement");
        recordingStatus_ = new QLabel("Enregistrement arrêté. Aucun historique automatique.");
        qobject_cast<QVBoxLayout *>(p->layout())->insertWidget(2, recordingStatus_);
        auto *options = new QWidget;
        auto *optionLayout = new QHBoxLayout(options);
        recordCpu_ = new QCheckBox("Enregistrer CPU");
        recordRam_ = new QCheckBox("Enregistrer RAM");
        autoRecord_ = new QCheckBox("Nouvelle session automatique à chaque lancement");
        recordCpu_->setChecked(preferences_["recordCpu"].toBool(true));
        recordRam_->setChecked(preferences_["recordRam"].toBool(true));
        autoRecord_->setEnabled(preferences_.contains("retentionDays"));
        autoRecord_->setChecked(preferences_["autoRecord"].toBool() && autoRecord_->isEnabled());
        optionLayout->addWidget(recordCpu_);
        optionLayout->addWidget(recordRam_);
        optionLayout->addWidget(autoRecord_);
        qobject_cast<QVBoxLayout *>(p->layout())->insertWidget(3, options);
        for (auto *option : {recordCpu_, recordRam_, autoRecord_})
            connect(option, &QCheckBox::toggled, this, [this] { savePreferences(); });
        p->addButton("Choisir les capteurs supplémentaires…", [this, p] {
            bool ok = false;
            QStringList metrics{"gpu-percent",
                                "gpu-temperature",
                                "disk-used-percent",
                                "network-loss",
                                "network-receive-bytes-per-sec",
                                "network-send-bytes-per-sec",
                                "disk-read-bytes-per-sec",
                                "disk-write-bytes-per-sec"};
            auto key = QInputDialog::getItem(
                p, "Capteurs enregistrés",
                "Sélectionner une mesure pour l'activer/désactiver. GPU dépend de NVML ; "
                "collecteur supplémentaire à 5 s, unités incluses dans les identifiants.",
                metrics, 0, false, &ok);
            if (!ok)
                return;
            if (recordedAuxiliary_.contains(key))
                recordedAuxiliary_.removeAll(key);
            else
                recordedAuxiliary_.append(key);
            savePreferences();
            QMessageBox::information(p, "Sélection",
                                     recordedAuxiliary_.isEmpty()
                                         ? "Aucun capteur supplémentaire sélectionné."
                                         : recordedAuxiliary_.join('\n'));
        });
        p->addButton("Enregistrer CPU/RAM", [this, p] {
            if (history_.recording()) {
                QMessageBox::information(p, "Historique", "Une session est déjà active.");
                return;
            }
            if (!recordCpu_->isChecked() && !recordRam_->isChecked() &&
                recordedAuxiliary_.isEmpty()) {
                QMessageBox::warning(p, "Historique", "Choisir au moins un capteur.");
                return;
            }
            bool ok = false;
            auto name = QInputDialog::getText(p, "Nouvelle session",
                                              "Nom de session (jeu, streaming ou autre)",
                                              QLineEdit::Normal, "Session locale", &ok);
            if (!ok)
                return;
            int days = QInputDialog::getInt(p, "Rétention",
                                            "Conserver les sessions pendant combien de jours ?", 7,
                                            1, 3650, 1, &ok);
            if (!ok)
                return;
            int mib = QInputDialog::getInt(p, "Quota",
                                           "Limite de données actives en Mio (les anciens "
                                           "enregistrements éligibles seront supprimés)",
                                           200, 10, 10240, 10, &ok);
            if (!ok)
                return;
            if (!history_.start(name, days, qint64(mib) * 1024 * 1024))
                QMessageBox::warning(p, "Enregistrement", history_.error());
            else {
                recordingStatus_->setText(
                    "ENREGISTREMENT ACTIF • capteurs CPU/RAM sélectionnés • stockage local");
                preferences_["retentionDays"] = days;
                preferences_["quotaMib"] = mib;
                autoRecord_->setEnabled(true);
                savePreferences();
            }
            p->showRows(history_.sessions());
        });
        p->addButton("Arrêter", [this, p] {
            history_.stop();
            recordingStatus_->setText("Enregistrement arrêté.");
            p->showRows(history_.sessions());
        });
        p->addButton("Marqueur", [this, p] {
            bool ok = false;
            auto text = QInputDialog::getText(p, "Marqueur", "Libellé", QLineEdit::Normal, {}, &ok);
            if (ok && !history_.marker(text))
                QMessageBox::warning(
                    p, "Marqueur",
                    "Impossible d'ajouter le marqueur (session inactive ou texte invalide).");
        });
        p->addButton("Sessions", [this, p] { p->showRows(history_.sessions(), history_.error()); });
        p->addButton("Conserver / autoriser la purge", [this, p] {
            auto row = p->selected();
            auto id = row["ID"].toInteger();
            if (id > 0) {
                if (!history_.protectSession(id, !row["Conservée"].toBool()))
                    QMessageBox::warning(p, "Historique", history_.error());
                p->showRows(history_.sessions());
            }
        });
        p->addButton("Exporter la session complète…", [this, p] {
            auto id = p->selected()["ID"].toInteger();
            if (id <= 0)
                return;
            auto path = QFileDialog::getSaveFileName(p, "Exporter tous les relevés validés", {},
                                                     "JSON (*.json);;CSV (*.csv)");
            if (path.isEmpty())
                return;
            if (!history_.flushPending()) {
                QMessageBox::warning(p, "Historique", history_.error());
                return;
            }
            auto cancel = p->cancellation();
            cancel->store(false);
            p->run([database = history_.path(), id, path, cancel] {
                return exportSession(database, id, path, cancel);
            });
        });
        p->addButton("Annuler l'export", [p] { p->cancellation()->store(true); });
        p->addButton("Relire", [this, p] {
            auto id = p->selected().value("ID").toInteger();
            if (id > 0)
                replay(id);
        });
        p->addButton("Comparer", [this, p] {
            auto id = p->selected().value("ID").toInteger();
            if (id <= 0)
                return;
            QStringList labels;
            QList<qint64> ids;
            for (auto value : history_.sessions()) {
                auto row = value.toObject();
                auto other = row["ID"].toInteger();
                if (other != id) {
                    labels.append(QString::number(other) + " • " + row["Nom"].toString());
                    ids.append(other);
                }
            }
            if (labels.isEmpty())
                return;
            bool ok = false;
            auto selected = QInputDialog::getItem(
                p, "Comparer les sessions", "Deuxième session (courbes alignées sur leur fin)",
                labels, 0, false, &ok);
            if (ok)
                replay(id, ids[labels.indexOf(selected)]);
        });
        p->addButton("Supprimer", [this, p] {
            auto id = p->selected().value("ID").toInteger();
            if (id > 0 &&
                QMessageBox::question(
                    p, "Supprimer", "Supprimer définitivement cette session et ses marqueurs ?") ==
                    QMessageBox::Yes) {
                if (!history_.deleteSession(id))
                    QMessageBox::warning(p, "Historique", history_.error());
                p->showRows(history_.sessions());
            }
        });
        p->showRows(history_.sessions(), history_.error());
        if (autoRecord_->isChecked())
            QTimer::singleShot(0, this, [this] {
                if ((recordCpu_->isChecked() || recordRam_->isChecked() ||
                     !recordedAuxiliary_.isEmpty()) &&
                    history_.start(
                        "Session automatique",
                        std::clamp(preferences_["retentionDays"].toInt(7), 1, 3650),
                        qint64(std::clamp(preferences_["quotaMib"].toInt(200), 10, 10240)) * 1024 *
                            1024))
                    recordingStatus_->setText(
                        "ENREGISTREMENT AUTOMATIQUE ACTIF • capteurs sélectionnés");
                else
                    recordingStatus_->setText("Enregistrement automatique impossible : " +
                                              history_.error());
            });
        return p;
    }
    if (category == "diagnostics") {
        auto *p = diagnosticPage_ = new DataPage("Diagnostics locaux explicables");
        p->addButton("Analyser les 2 dernières minutes", [this] { diagnosticReport(); });
        p->showRows({}, "Analyse CPU et RAM ; aucune conclusion automatique de fuite mémoire, de "
                        "panne ou de comportement malveillant.");
        return p;
    }
    if (category == "alerts") {
        auto *p = alertPage_ = new DataPage("Alertes locales CPU / RAM");
        auto *settings = new QWidget;
        auto *form = new QHBoxLayout(settings);
        alertsEnabled_ = new QCheckBox("Activer");
        sound_ = new QCheckBox("Son");
        cpuThreshold_ = new QDoubleSpinBox;
        ramThreshold_ = new QDoubleSpinBox;
        for (auto *box : {cpuThreshold_, ramThreshold_}) {
            box->setRange(1, 100);
            box->setValue(90);
            box->setSuffix(" %");
        }
        cpuEnabled_ = new QCheckBox("CPU");
        ramEnabled_ = new QCheckBox("RAM");
        cpuEnabled_->setChecked(preferences_["cpuEnabled"].toBool(true));
        ramEnabled_->setChecked(preferences_["ramEnabled"].toBool(true));
        duration_ = new QSpinBox;
        duration_->setRange(1, 3600);
        duration_->setValue(10);
        duration_->setSuffix(" s");
        cooldown_ = new QSpinBox;
        cooldown_->setRange(1, 86400);
        cooldown_->setValue(60);
        cooldown_->setSuffix(" s");
        cpuThreshold_->setValue(preferences_["cpuThreshold"].toDouble(90));
        ramThreshold_->setValue(preferences_["ramThreshold"].toDouble(90));
        duration_->setValue(preferences_["duration"].toInt(10));
        cooldown_->setValue(preferences_["cooldown"].toInt(60));
        alertsEnabled_->setChecked(preferences_["alertsEnabled"].toBool());
        sound_->setChecked(preferences_["sound"].toBool());
        form->addWidget(alertsEnabled_);
        form->addWidget(cpuEnabled_);
        form->addWidget(cpuThreshold_);
        form->addWidget(ramEnabled_);
        form->addWidget(ramThreshold_);
        form->addWidget(new QLabel("Durée"));
        form->addWidget(duration_);
        form->addWidget(new QLabel("Temporisation"));
        form->addWidget(cooldown_);
        form->addWidget(sound_);
        qobject_cast<QVBoxLayout *>(p->layout())->insertWidget(2, settings);
        auto reset = [this] {
            cpuRule_ = std::make_unique<ThresholdRule>(cpuThreshold_->value(), 3,
                                                       duration_->value() * 1000ll,
                                                       cooldown_->value() * 1000ll);
            ramRule_ = std::make_unique<ThresholdRule>(ramThreshold_->value(), 3,
                                                       duration_->value() * 1000ll,
                                                       cooldown_->value() * 1000ll);
            setTray(trayRequested_);
            savePreferences();
        };
        connect(alertsEnabled_, &QCheckBox::toggled, this, reset);
        connect(cpuThreshold_, &QDoubleSpinBox::valueChanged, this, reset);
        connect(ramThreshold_, &QDoubleSpinBox::valueChanged, this, reset);
        connect(duration_, &QSpinBox::valueChanged, this, reset);
        connect(cooldown_, &QSpinBox::valueChanged, this, reset);
        reset();
        connect(sound_, &QCheckBox::toggled, this, [this] { savePreferences(); });
        connect(cpuEnabled_, &QCheckBox::toggled, this, reset);
        connect(ramEnabled_, &QCheckBox::toggled, this, reset);
        p->addButton("Nouvelle règle", [this] { addRule(); });
        p->addButton("Règles", [this, p] {
            QJsonArray rows;
            for (std::size_t i = 0; i < customRules_.size(); ++i) {
                const auto &r = customRules_[i];
                rows.append(QJsonObject{{"Règle", int(i)},
                                        {"Mesure", r.metric},
                                        {"Seuil", r.threshold},
                                        {"Activée", r.enabled},
                                        {"Durée s", r.duration},
                                        {"Temporisation s", r.cooldown},
                                        {"PID", double(r.pid)}});
            }
            p->showRows(rows);
        });
        p->addButton("Activer/désactiver la règle", [this, p] {
            auto row = p->selected();
            if (!row.contains("Règle"))
                return;
            int index = row["Règle"].toInt(-1);
            if (index >= 0 && index < int(customRules_.size())) {
                customRules_[index].enabled = !customRules_[index].enabled;
                customRules_[index].state = std::make_unique<ThresholdRule>(
                    customRules_[index].threshold, 3, customRules_[index].duration * 1000ll,
                    customRules_[index].cooldown * 1000ll);
                savePreferences();
                p->showRows({}, "Règle modifiée. Cliquer sur Règles pour actualiser la liste.");
            }
        });
        p->addButton("Journal", [this, p] { p->showRows(history_.reports("alert")); });
        p->showRows(history_.reports("alert"),
                    "Hystérésis de 3 points. Notifications sur franchissement confirmé ; pas de "
                    "répétition tant que le seuil reste dépassé. Paramètres locaux persistants.");
        return p;
    }
    if (category == "benchmarks") {
        auto *p = benchmarkPage_ = new DataPage("Benchmarks locaux • protocoles Baawiil 1");
        for (const auto &kind :
             QStringList{"cpu-single", "cpu-multi", "memory", "disk-sequential", "disk-random",
                         "disk-read-sequential", "disk-read-random"})
            p->addButton(kind, [this, p, kind] {
                if (QMessageBox::question(
                        p, "Benchmark volontaire",
                        "Ce test charge fortement la ressource pendant 5 secondes. Les tests "
                        "disque écrivent dans un fichier temporaire dédié, avec cache Windows. "
                        "Aucun score n'est comparable aux suites tierces. Continuer ?") !=
                    QMessageBox::Yes)
                    return;
                QString directory;
                if (kind.startsWith("disk")) {
                    directory =
                        QFileDialog::getExistingDirectory(p, "Dossier du fichier temporaire");
                    if (directory.isEmpty())
                        return;
                }
                auto cancel = p->cancellation();
                cancel->store(false);
                p->run([kind, directory, cancel] {
                    return isolatedTask(
                        "benchmark",
                        QJsonObject{{"kind", kind}, {"seconds", 5}, {"directory", directory}},
                        cancel);
                });
            });
        p->addButton("Annuler", [p] { p->cancellation()->store(true); });
        p->addButton("Résultats conservés",
                     [this, p] { p->showRows(history_.reports("benchmark")); });
        // Saving is explicit: never claim a background result was persisted merely because its job
        // started.
        p->addButton("Conserver le résultat", [this, p] {
            auto row = p->selected();
            if (row.contains("Protocole"))
                history_.saveReport("benchmark", QJsonArray{row});
        });
        return p;
    }
    QString title = category;
    const QMap<QString, QString> titles{{"gpu", "Carte graphique"},
                                        {"disks", "Disques physiques"},
                                        {"volumes", "Volumes et espace disponible"},
                                        {"network", "Interfaces réseau"},
                                        {"services", "Services Windows"},
                                        {"startup", "Applications au démarrage"},
                                        {"system", "Informations système"},
                                        {"sensors", "Capteurs matériels facultatifs"}};
    title = titles.value(category, category);
    auto *p = new DataPage(title, category);
    if (category == "gpu") {
        p->addButton("Capteurs NVIDIA", [p] { p->setCategory("nvidia"); });
        p->addButton("Moteurs GPU", [p] { p->setCategory("gpuRates"); });
        p->addButton("VRAM", [p] { p->setCategory("vram"); });
        p->addButton("Capteurs", [p] { p->setCategory("sensors"); });
    }
    if (category == "disks") {
        p->addButton("Volumes", [p] { p->setCategory("volumes"); });
        p->addButton("Activité", [p] { p->setCategory("diskRates"); });
        p->addButton("Santé disponible", [p] { p->setCategory("health"); });
        p->addButton("Analyser un dossier", [p] {
            auto directory = QFileDialog::getExistingDirectory(
                p, "Analyse en lecture seule (délai maximal 30 minutes)");
            if (directory.isEmpty())
                return;
            auto cancel = p->cancellation();
            cancel->store(false);
            p->run([directory, cancel] {
                return isolatedTask("scan", QJsonObject{{"root", directory}}, cancel);
            });
        });
        p->addButton("Annuler l'analyse", [p] { p->cancellation()->store(true); });
    }
    if (category == "network") {
        p->addButton("Débits", [p] { p->setCategory("networkRates"); });
        p->addButton("Connexions", [p] { p->setCategory("connections"); });
        for (bool dns : {false, true})
            p->addButton(dns ? "Test DNS volontaire" : "Test ICMP volontaire", [p, dns] {
                bool ok = false;
                auto host = QInputDialog::getText(
                    p, "Test réseau actif",
                    dns ? "Nom à résoudre via le DNS configuré"
                        : "Destination : 4 requêtes ICMP, délai maximal 15 secondes",
                    QLineEdit::Normal, {}, &ok);
                if (ok && !host.isEmpty())
                    p->run([host, dns] { return networkTest(host, dns); });
            });
    }
    if (category == "services")
        for (const auto &action : QStringList{"start", "stop", "restart"})
            p->addButton(
                action == "start"  ? "Démarrer"
                : action == "stop" ? "Arrêter"
                                   : "Redémarrer",
                [p, action] {
                    auto name = p->selected().value("Nom").toString();
                    if (name.isEmpty())
                        return;
                    if (QMessageBox::question(p, "Service Windows",
                                              QString("%1 le service %2 ? Les dépendances ne "
                                                      "seront pas modifiées automatiquement.")
                                                  .arg(action, name)) != QMessageBox::Yes)
                        return;
                    p->run([name, action] {
                        auto error = serviceAction(name, action);
                        auto result = inventory("services");
                        if (!error.isEmpty())
                            result.error = error + " Aucune élévation automatique.";
                        return result;
                    });
                });
    if (category == "services")
        p->addButton("Administrateur…", [p] {
            auto name = p->selected()["Nom"].toString();
            if (name.isEmpty())
                return;
            bool ok = false;
            auto action = QInputDialog::getItem(
                p, "Opération élevée sur " + name,
                "Choisir l'action (UAC puis confirmation dans un composant séparé)",
                {"start", "stop", "restart"}, 0, false, &ok);
            if (!ok)
                return;
            p->run([name, action] {
                auto error = elevatedAction(
                    {"service", action,
                     QString::fromLatin1(name.toUtf8().toBase64(QByteArray::Base64UrlEncoding |
                                                                QByteArray::OmitTrailingEquals))});
                auto result = inventory("services");
                result.error = error;
                return result;
            });
        });
    if (category == "startup")
        p->addButton("Activer / désactiver (utilisateur)", [p] {
            auto row = p->selected();
            if (row.value("Portée").toString() != "Utilisateur") {
                QMessageBox::information(p, "Démarrage",
                                         "Seules les entrées Run et le dossier de démarrage de "
                                         "l'utilisateur courant sont modifiables.");
                return;
            }
            auto name = row.value("Nom").toString();
            auto path = row.value("Chemin").toString();
            const bool folder = row.value("Origine").toString() == "Dossier utilisateur";
            bool enable = !row.value("Activé").toBool();
            if (QMessageBox::question(
                    p, "Démarrage",
                    QString("%1 l'entrée %2 ? L'original est conservé dans le registre Baawiil ou "
                            "le dossier local de sauvegarde du démarrage.")
                        .arg(enable ? "Restaurer" : "Désactiver", name)) != QMessageBox::Yes)
                return;
            p->run([name, path, folder, enable] {
                auto error = folder ? startupFileToggle(path, enable) : startupToggle(name, enable);
                auto result = inventory("startup");
                if (!error.isEmpty())
                    result.error = error;
                return result;
            });
        });
    if (category == "system") {
        p->addButton("Processeur", [p] { p->setCategory("cpu"); });
        p->addButton("Modules RAM", [p] { p->setCategory("memory"); });
    }
    return p;
}
void Workbench::observe(const Snapshot &snapshot) {
    lastSystemSample_ = snapshot.monotonicTime;
    lastSystemInterval_ = snapshot.intervalMs;
    lastProcessInterval_ = snapshot.processIntervalMs;
    identities_.clear();
    for (const auto &row : snapshot.processes)
        if (row.created)
            identities_[row.pid] = row.created;
    auto ram = snapshot.totalMemory
                   ? std::optional<double>(100.0 *
                                           double(snapshot.totalMemory - snapshot.availableMemory) /
                                           double(snapshot.totalMemory))
                   : std::nullopt;
    auto now = monitor::monotonicMs();
    diagnostics_.append({snapshot.monotonicTime, snapshot.cpu, ram});
    if (snapshot.processMonotonicTime != lastProcessTimestamp_) {
        lastProcessTimestamp_ = snapshot.processMonotonicTime;
        QSet<QString> alive;
        for (const auto &row : snapshot.processes)
            if (row.created) {
                auto key = QString::number(row.pid) + ":" + QString::number(row.created);
                alive.insert(key);
                auto &trend = trends_[key];
                trend.name = row.name;
                trend.pid = row.pid;
                trend.creation = row.created;
                trend.cpu.push_back({snapshot.processMonotonicTime, row.cpu, {}});
                if (row.memoryAvailable)
                    trend.memory.emplace_back(snapshot.processMonotonicTime, row.privateBytes);
                while (!trend.cpu.empty() &&
                       (snapshot.processMonotonicTime - trend.cpu.front().time > 120000 ||
                        trend.cpu.size() > 600))
                    trend.cpu.pop_front();
                while (!trend.memory.empty() &&
                       (snapshot.processMonotonicTime - trend.memory.front().first > 120000 ||
                        trend.memory.size() > 600))
                    trend.memory.pop_front();
            }
        for (auto it = trends_.begin(); it != trends_.end();)
            if (!alive.contains(it.key()))
                it = trends_.erase(it);
            else
                ++it;
    }
    if (history_.recording()) {
        QJsonObject values, states;
        for (const auto &key : recordedAuxiliary_) {
            states[key] = !auxiliaryTime_                   ? "initialisation"
                          : now - auxiliaryTime_ >= 7500    ? "périmé"
                          : auxiliaryMetrics_.contains(key) ? "mesuré"
                                                            : "indisponible";
            values[key] = auxiliaryTime_ && now >= auxiliaryTime_ && now - auxiliaryTime_ < 7500 &&
                                  auxiliaryMetrics_.contains(key)
                              ? QJsonValue(auxiliaryMetrics_[key])
                              : QJsonValue();
        }
        QJsonObject extra{
            {"sourceUtc",
             auxiliaryTime_
                 ? QDateTime::fromMSecsSinceEpoch(auxiliaryUtc_).toUTC().toString(Qt::ISODateWithMs)
                 : QString()},
            {"sourceAgeMs",
             auxiliaryTime_ ? QJsonValue(double(now - auxiliaryTime_)) : QJsonValue()},
            {"quality", !auxiliaryTime_                ? "initialisation"
                        : now - auxiliaryTime_ >= 7500 ? "périmé"
                                                       : "mesuré"},
            {"values", values},
            {"states", states}};
        if (!history_.append(
                snapshot.timestamp, recordCpu_->isChecked() ? snapshot.cpu : std::nullopt,
                recordRam_->isChecked() ? ram : std::nullopt, extra, snapshot.monotonicTime) &&
            recordingStatus_)
            recordingStatus_->setText("Enregistrement suspendu : " + history_.error());
    }
    if (alertsEnabled_ && alertsEnabled_->isChecked()) {
        if (cpuEnabled_->isChecked() &&
            cpuRule_->observeSample(snapshot.cpu, snapshot.monotonicTime, monitor::monotonicMs(),
                                    std::max(1500, 3 * snapshot.intervalMs)) &&
            snapshot.cpu)
            alert("CPU", *snapshot.cpu, cpuThreshold_->value());
        if (ramEnabled_->isChecked() &&
            ramRule_->observeSample(ram, snapshot.monotonicTime, monitor::monotonicMs(),
                                    std::max(1500, 3 * snapshot.intervalMs)) &&
            ram)
            alert("RAM", *ram, ramThreshold_->value());
        for (auto &rule : customRules_)
            if (rule.enabled) {
                std::optional<double> value;
                const bool process = rule.metric == "process-cpu";
                if (process && snapshot.processMonotonicTime &&
                    monitor::monotonicMs() - snapshot.processMonotonicTime <
                        std::max(1500, 3 * snapshot.processIntervalMs)) {
                    for (const auto &row : snapshot.processes)
                        if (row.pid == rule.pid && row.created == rule.creation)
                            value = row.cpu;
                } else if (!process && now - auxiliaryTime_ < 7500 &&
                           auxiliaryMetrics_.contains(rule.metric))
                    value = auxiliaryMetrics_.value(rule.metric);
                if (rule.state->observeSample(
                        value, process ? snapshot.processMonotonicTime : auxiliaryTime_,
                        process ? monitor::monotonicMs() : now,
                        process ? std::max(1500, 3 * snapshot.processIntervalMs) : 7500) &&
                    value)
                    alert(process ? QString("process-cpu PID %1").arg(rule.pid) : rule.metric,
                          *value, rule.threshold, rule.metric == "gpu-temperature" ? "°C" : "%",
                          rule.duration);
            }
    }
}
void Workbench::alert(QString name, double value, double threshold, QString unit,
                      int durationSeconds) {
    auto message = QString("%1 : %2 %3 après dépassement de %4 %3 pendant au moins %5 secondes.")
                       .arg(name)
                       .arg(value, 0, 'f', 1)
                       .arg(unit)
                       .arg(threshold)
                       .arg(durationSeconds < 0 ? duration_->value() : durationSeconds);
    tray_->showMessage("Alerte Baawiil", message, QSystemTrayIcon::Warning, 10000);
    if (sound_->isChecked())
        QApplication::beep();
    history_.saveReport("alert", QJsonArray{QJsonObject{{"Mesure", name},
                                                        {"Valeur", value},
                                                        {"Seuil", threshold},
                                                        {"Unité", unit},
                                                        {"Message", message}}});
    if (alertPage_)
        alertPage_->showRows(history_.reports("alert"));
}
void Workbench::replay(qint64 session, qint64 other) {
    auto *dialog = new QWidget;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(other ? "Comparaison • alignement sur la fin des sessions"
                                 : "Relecture synchronisée CPU/RAM");
    dialog->resize(1100, 850);
    auto *outer = new QVBoxLayout(dialog);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    scroll->setWidget(content);
    auto *layout = new QVBoxLayout(content);
    auto *controls = new QHBoxLayout;
    auto *period = new QComboBox;
    for (int seconds : {60, 300, 900, 3600, 86400})
        period->addItem(QString::number(seconds) + " s", seconds);
    period->setCurrentIndex(1);
    controls->addWidget(period);
    auto *customPeriod = new QPushButton("Période…");
    controls->addWidget(customPeriod);
    connect(customPeriod, &QPushButton::clicked, dialog, [dialog, period] {
        bool ok = false;
        int seconds = QInputDialog::getInt(dialog, "Période de relecture", "Fenêtre en secondes",
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
    offset->setAccessibleName("Recul dans les sessions, secondes");
    controls->addWidget(offset);
    layout->addLayout(controls);
    QList<Graph *> graphs;
    QJsonArray merged;
    for (qint64 id : {session, other})
        if (id > 0) {
            auto samples = history_.samples(id);
            bool validTimeline = true;
            double previousTime = -std::numeric_limits<double>::infinity();
            for (auto value : samples) {
                auto row = value.toObject();
                double time = row["Temps écoulé ms"].isDouble()
                                  ? row["Temps écoulé ms"].toDouble() / 1000.0
                                  : QDateTime::fromString(row["UTC"].toString(), Qt::ISODateWithMs)
                                            .toMSecsSinceEpoch() /
                                        1000.0;
                if (time < previousTime)
                    validTimeline = false;
                previousTime = time;
            }
            auto *cpu = new Graph(QString("Session %1 • CPU").arg(id));
            auto *ram = new Graph(QString("Session %1 • RAM").arg(id));
            graphs.append(cpu);
            graphs.append(ram);
            layout->addWidget(cpu);
            layout->addWidget(ram);
            for (auto value : samples) {
                auto row = value.toObject();
                auto utc = QDateTime::fromString(row["UTC"].toString(), Qt::ISODateWithMs)
                               .toMSecsSinceEpoch();
                auto elapsed =
                    row["Temps écoulé ms"].isDouble()
                        ? std::optional<double>(row["Temps écoulé ms"].toDouble() / 1000.0)
                        : std::nullopt;
                if (validTimeline) {
                    cpu->appendRecorded(utc,
                                        row["CPU %"].isDouble()
                                            ? std::optional<double>(row["CPU %"].toDouble())
                                            : std::nullopt,
                                        elapsed);
                    ram->appendRecorded(utc,
                                        row["RAM %"].isDouble()
                                            ? std::optional<double>(row["RAM %"].toDouble())
                                            : std::nullopt,
                                        elapsed);
                }
                row["Session ID"] = double(id);
                merged.append(row);
            }
            if (!validTimeline) {
                cpu->setToolTip("Ancienne session avec horloge UTC non monotone : graphiques "
                                "désactivés, relevés originaux conservés.");
                ram->setToolTip(cpu->toolTip());
            }
            cpu->setSpan(300);
            ram->setSpan(300);
        }
    connect(period, &QComboBox::currentIndexChanged, dialog, [graphs, period] {
        for (auto *graph : graphs)
            graph->setSpan(period->currentData().toInt());
    });
    connect(offset, &QSlider::valueChanged, dialog, [graphs](int seconds) {
        for (auto *graph : graphs)
            graph->setOffset(seconds);
    });
    auto *table = new DataPage("Relevés et exports");
    table->setMinimumHeight(300);
    layout->addWidget(table);
    table->showRows(
        merged,
        "La période complète est représentée. Au-delà de 100 000 relevés : décimation temporelle "
        "avec extrêmes CPU/RAM conservés. Les statistiques des courbes portent sur les points "
        "affichés ; utiliser Statistiques exactes ou l'export complet pour les données "
        "originales.");
    table->addButton("Statistiques exactes", [this, table, session, other] {
        auto rows = history_.statistics(session);
        if (other > 0)
            for (auto value : history_.statistics(other)) {
                auto row = value.toObject();
                row["Deuxième session ID"] = double(other);
                rows.append(row);
            }
        table->showRows(rows, "Moyennes arithmétiques calculées sur tous les relevés valides "
                              "enregistrés, indépendamment de la décimation visuelle.");
    });
    table->addButton("Marqueurs",
                     [this, table, session] { table->showRows(history_.markers(session)); });
    table->addButton("Courbes PNG", [graphs] {
        for (auto *graph : graphs)
            graph->exportPng();
    });
    dialog->show();
}
void Workbench::diagnosticReport() {
    const auto now = monitor::monotonicMs();
    if (!lastSystemSample_ || now - lastSystemSample_ > std::max(1500, 3 * lastSystemInterval_)) {
        diagnosticPage_->showRows(QJsonArray{QJsonObject{
            {"Observation", "Mesures système absentes ou périmées : analyse indisponible"}}});
        return;
    }
    auto s = diagnostics_.summary();
    QJsonArray rows;
    for (bool cpu : {true, false}) {
        double value = cpu ? s.cpu : s.ram;
        auto count = cpu ? s.cpuCount : s.ramCount;
        rows.append(QJsonObject{
            {"Règle", cpu ? "CPU-MOYEN-1" : "RAM-MOYENNE-1"},
            {"Fenêtre observée s", s.duration / 1000.0},
            {"Échantillons valides", double(count)},
            {"Moyenne %", count ? QJsonValue(value) : QJsonValue()},
            {"Observation", !count        ? "Données insuffisantes"
                            : value >= 90 ? "Utilisation moyenne élevée"
                                          : "Pas de saturation moyenne observée"},
            {"Limites",
             "Moyenne arithmétique des relevés disponibles, sans preuve de cause. Fenêtre maximale "
             "120 s ; pas de diagnostic thermique ou de fuite mémoire."}});
    }
    struct Top {
        QString name;
        quint32 pid;
        double value;
    };
    std::vector<Top> top;
    for (const auto &trend : trends_) {
        if (!lastProcessTimestamp_ ||
            now - lastProcessTimestamp_ > std::max(1500, 3 * lastProcessInterval_))
            continue;
        double total = 0;
        int count = 0;
        for (const auto &sample : trend.cpu)
            if (sample.cpu) {
                total += *sample.cpu;
                ++count;
            }
        if (count)
            top.push_back({trend.name, trend.pid, total / count});
        if (trend.memory.size() >= 3) {
            const auto first = trend.memory.front(), last = trend.memory.back();
            if (last.first - first.first >= 60000 && last.second > first.second &&
                last.second - first.second >= 64ull * 1024 * 1024) {
                std::size_t increases = 0;
                for (std::size_t i = 1; i < trend.memory.size(); ++i)
                    if (trend.memory[i].second >= trend.memory[i - 1].second)
                        ++increases;
                if (increases * 10 >= (trend.memory.size() - 1) * 9)
                    rows.append(QJsonObject{
                        {"Règle", "CROISSANCE-MEMOIRE-1"},
                        {"Processus", trend.name},
                        {"PID", double(trend.pid)},
                        {"Augmentation privée octets", double(last.second - first.second)},
                        {"Fenêtre observée s", (last.first - first.first) / 1000.0},
                        {"Observation", "Croissance mémoire privée soutenue observée"},
                        {"Limites", "Au moins 64 Mio supplémentaires sur 60 s et 90 % de relevés "
                                    "non décroissants. Peut correspondre à un cache ou une charge "
                                    "normale ; aucune fuite mémoire prouvée."}});
            }
        }
    }
    std::sort(top.begin(), top.end(), [](const Top &a, const Top &b) { return a.value > b.value; });
    for (std::size_t i = 0; i < std::min<std::size_t>(3, top.size()); ++i)
        rows.append(
            QJsonObject{{"Règle", "CPU-PROCESSUS-1"},
                        {"Processus", top[i].name},
                        {"PID", double(top[i].pid)},
                        {"CPU moyen capacité machine %", top[i].value},
                        {"Limites", "Moyenne des relevés valides disponibles, fenêtre au plus 120 "
                                    "s. Classement relatif, pas une preuve de ralentissement."}});
    diagnosticPage_->showRows(rows);
    history_.saveReport("diagnostic", rows);
}
void Workbench::processMenu(QWidget *parent, const ProcessRow &row, const QPoint &globalPosition) {
    QMenu menu(parent);
    auto *copy = menu.addAction("Copier les informations");
    auto *location = menu.addAction("Ouvrir l’emplacement");
    auto *properties = menu.addAction("Propriétés et signature locale");
    auto *modules = menu.addAction("Modules chargés");
    auto *handles = menu.addAction("Handles accessibles");
    auto *children = menu.addAction("Arborescence et terminaison…");
    menu.addSeparator();
    auto *terminate = menu.addAction("Terminer…");
    auto *priority = menu.addAction("Modifier la priorité…");
    auto *affinity = menu.addAction("Affinité CPU…");
    auto *admin = menu.addAction("Action administrateur explicite…");
    auto *selected = menu.exec(globalPosition);
    if (!selected)
        return;
    if (selected == copy) {
        qApp->clipboard()->setText(
            QString("%1 • PID %2\n%3").arg(row.name).arg(row.pid).arg(row.path));
        return;
    }
    if (selected == location) {
        if (!row.path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(row.path).absolutePath()));
        return;
    }
    if (selected == properties || selected == modules || selected == children ||
        selected == handles) {
        auto *data = new DataPage(row.name + " • PID " + QString::number(row.pid));
        data->setAttribute(Qt::WA_DeleteOnClose);
        data->resize(1000, 650);
        data->show();
        if (selected == handles) {
            auto cancel = data->cancellation();
            data->run([row, cancel] {
                return isolatedTask("handles",
                                    QJsonObject{{"pid", double(row.pid)},
                                                {"creation", QString::number(row.created)}},
                                    cancel);
            });
        } else if (selected == children) {
            data->run([row] { return processTree(row.pid, row.created); });
            data->addButton("Terminer l'arborescence observée…", [data, row] {
                // Obtain a fresh snapshot and display every target before allowing execution.
                auto *watcher = new QFutureWatcher<Result>(data);
                connect(watcher, &QFutureWatcher<Result>::finished, data, [data, watcher] {
                    auto result = watcher->result();
                    watcher->deleteLater();
                    if (result.rows.isEmpty()) {
                        data->showRows({}, result.error);
                        return;
                    }
                    QStringList names;
                    for (auto value : result.rows) {
                        auto item = value.toObject();
                        names.append(item["Nom"].toString() + " (" +
                                     QString::number(item["PID"].toInt()) + ")");
                    }
                    if (QMessageBox::warning(
                            data, "Terminer les processus listés",
                            names.join('\n') +
                                "\n\nPerte de données possible. Les processus critiques et ceux "
                                "ayant changé d'identité seront refusés. Continuer ?",
                            QMessageBox::Yes | QMessageBox::Cancel,
                            QMessageBox::Cancel) != QMessageBox::Yes)
                        return;
                    data->run([rows = result.rows] {
                        Result output;
                        for (auto value : rows) {
                            auto item = value.toObject();
                            auto error = processAction(
                                quint32(item["PID"].toInteger()),
                                item["Création FILETIME"].toString().toULongLong(), "terminate");
                            item["Résultat"] = error.isEmpty() ? "Terminaison demandée" : error;
                            output.rows.append(item);
                        }
                        return output;
                    });
                });
                watcher->setFuture(
                    QtConcurrent::run([row] { return processTree(row.pid, row.created); }));
            });
        } else
            data->run([row, showModules = selected == modules] {
                return processDetails(row.pid, row.created, showModules);
            });
        return;
    }
    quint64 value = 0;
    QString action;
    const bool elevated = selected == admin;
    if (elevated) {
        bool ok = false;
        action =
            QInputDialog::getItem(parent, "Action élevée", "Choisir l'action pour ce processus",
                                  {"terminate", "priority", "affinity"}, 0, false, &ok);
        if (!ok)
            return;
        selected = action == "terminate" ? terminate : action == "priority" ? priority : affinity;
    }
    if (selected == terminate)
        action = "terminate";
    if (selected == priority) {
        bool ok = false;
        auto label = QInputDialog::getItem(
            parent, "Priorité", "Nouvelle priorité",
            {"Basse", "Inférieure à la normale", "Normale", "Supérieure à la normale", "Haute"}, 2,
            false, &ok);
        if (!ok)
            return;
        value = label == "Basse"                     ? 0x40
                : label == "Inférieure à la normale" ? 0x4000
                : label == "Normale"                 ? 0x20
                : label == "Haute"                   ? 0x80
                                                     : 0x8000;
        action = "priority";
    }
    if (selected == affinity) {
        bool ok = false;
        auto mask = QInputDialog::getText(parent, "Affinité",
                                          "Masque hexadécimal non nul (un seul groupe CPU)",
                                          QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        value = mask.toULongLong(&ok, 16);
        if (!ok || !value) {
            QMessageBox::warning(parent, "Affinité", "Masque invalide.");
            return;
        }
        action = "affinity";
    }
    if (QMessageBox::warning(
            parent, "Confirmer l’action",
            QString("Action %1 sur %2 (PID %3). Une terminaison peut perdre des données ; une "
                    "priorité ou affinité peut dégrader les performances. Continuer ?")
                .arg(action, row.name)
                .arg(row.pid),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    auto *watcher = new QFutureWatcher<QString>(parent);
    connect(watcher, &QFutureWatcher<QString>::finished, parent, [parent, watcher] {
        auto error = watcher->result();
        watcher->deleteLater();
        if (!error.isEmpty())
            QMessageBox::warning(parent, "Action refusée ou échouée", error);
    });
    watcher->setFuture(QtConcurrent::run([row, action, value, elevated] {
        return elevated
                   ? elevatedAction({"process", QString::number(row.pid),
                                     QString::number(row.created), action, QString::number(value)})
                   : processAction(row.pid, row.created, action, value);
    }));
}
} // namespace features
