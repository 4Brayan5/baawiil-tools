#include "window.h"
#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QDir>
#include <iostream>
#include <stdexcept>
#include <QProgressBar>
void requireUi(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_DATA_HOME", temporary.path().toUtf8());
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("BaawiilComponentTests");
    app.setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));
    qRegisterMetaType<Snapshot>();
    {
        Window window;
        window.show();
        app.processEvents();
        auto *nav = window.findChild<QListWidget *>();
        auto *pages = window.findChild<QStackedWidget *>();
        requireUi(nav && nav->count() == 15, "15 navigation entries");
        requireUi(pages && pages->count() == 15, "15 implemented pages");
        for (int index = 0; index < 15; ++index) {
            nav->setCurrentRow(index);
            app.processEvents();
            requireUi(pages->currentIndex() == index, "navigation selects matching page");
        }
        auto *model = window.findChild<QStandardItemModel *>();
        requireUi(model && model->columnCount() == 8, "process table model");
        Graph graph("Synthetic component-test series");
        graph.resize(700, 220);
        graph.appendRecorded(1000, 20);
        graph.appendRecorded(2000, 80);
        graph.show();
        app.processEvents();
        graph.setPaused(true);
        auto before = graph.grab().toImage();
        graph.appendRecorded(3000, 40);
        auto frozen = graph.grab().toImage();
        requireUi(before == frozen, "paused graph stays frozen despite collection");
        graph.setPaused(false);
        requireUi(before != graph.grab().toImage(), "resuming reveals new sample");
        window.close();
        Graph clockGraph("Synthetic wall-clock regression");
        clockGraph.resize(700, 220);
        clockGraph.appendRecorded(100000, 20, 0.0);
        clockGraph.appendRecorded(0, 80, 1.0);
        clockGraph.show();
        app.processEvents();
        clockGraph.grab();
        requireUi(clockGraph.accessibleDescription().contains("2 relevés valides"),
                  "replay follows elapsed time even when UTC moves backwards");
        features::DataPage manual("Synthetic task retention test", "gpu");
        auto *automatic = manual.findChild<QCheckBox *>();
        requireUi(automatic != nullptr, "inventory auto-refresh control exists");
        automatic->setChecked(true);
        manual.run([] { return features::Result{}; });
        requireUi(!automatic->isChecked(),
                  "one-off operation suspends inventory polling so results are retained");
        Graph live("Live animation component test");
        live.show();
        app.processEvents();
        auto *animation = live.findChild<QTimer *>();
        requireUi(animation && animation->isActive(),
                  "visible live graph animates independently of collection");
        live.hide();
        requireUi(!animation->isActive(), "hidden graph stops animation timer");
        features::DataPage occupancy("Synthetic storage component test");
        occupancy.showRows(
            QJsonArray{QJsonObject{{"Chemin", "synthetic-file"}, {"Part du périmètre %", 25.0}}});
        auto *bar = occupancy.findChild<QProgressBar *>();
        requireUi(bar && bar->value() == 250, "storage occupancy renders proportional bars");
    }
    // These paths are unique to this test application; no production settings are removed.
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).removeRecursively();
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .removeRecursively();
    std::cout << "15-page navigation, process model and graph pause/resume component checks "
                 "passed; Windows APIs were not executed.\n";
}
