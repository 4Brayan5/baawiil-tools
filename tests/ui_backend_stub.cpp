// Component-test adapter only. It is never linked into the Windows application.
#include "collector.h"
#include "features/backend.h"
Collector::~Collector() = default;
void Collector::start() {}
void Collector::setInterval(int) {}
void Collector::sample() {}
namespace features {
void shutdownWorkers() {}
QString startupFileToggle(const QString &, bool) {
    return "Démarrage non modifié dans ce test.";
}
Result inventory(const QString &) {
    return {{}, "Backend Windows non exécuté dans ce test d'interface."};
}
Result directoryUsage(const QString &, const std::shared_ptr<std::atomic_bool> &) {
    return {{}, "Test d'interface uniquement."};
}
Result benchmark(const QString &, int, const QString &, const std::shared_ptr<std::atomic_bool> &) {
    return {{}, "Test d'interface uniquement."};
}
QString processAction(quint32, quint64, const QString &, quint64) {
    return "Action non exécutée dans un test d'interface.";
}
QString serviceAction(const QString &, const QString &) {
    return "Action non exécutée dans un test d'interface.";
}
QString startupToggle(const QString &, bool) {
    return "Action non exécutée dans un test d'interface.";
}
Result networkTest(const QString &, bool) {
    return {{}, "Aucun accès réseau dans ce test."};
}
Result processDetails(quint32, quint64, bool) {
    return {};
}
Result processTree(quint32, quint64) {
    return {};
}
Result processHandles(quint32, quint64) {
    return {};
}
QString elevatedAction(const QStringList &) {
    return "Aucune élévation dans un test d'interface.";
}
QString verifyInstaller(const QString &, const QString &) {
    return "Aucune vérification Windows dans ce test.";
}
QString launchInstaller(const QString &, const QString &) {
    return "Aucun lancement dans un test d'interface.";
}
bool autoStartEnabled() {
    return false;
}
QString setAutoStart(bool) {
    return "Démarrage Windows non modifié par ce test.";
}
Result isolatedTask(const QString &, const QJsonObject &,
                    const std::shared_ptr<std::atomic_bool> &) {
    return {{}, "Collecteur Windows non exécuté dans ce test d'interface."};
}
} // namespace features
