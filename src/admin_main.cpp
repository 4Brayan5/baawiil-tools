#include "features/backend.h"
#include <QApplication>
#include <QMessageBox>
#include <QStringList>
#include <QRegularExpression>
#include <functional>
#include "runtime_lock.h"
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    RuntimeLock runtime;
    if (!runtime.valid())
        return 1;
    auto args = app.arguments();
    QString description, error;
    std::function<QString()> operation;
    if (args.size() == 4 && args[1] == "service" &&
        QStringList{"start", "stop", "restart"}.contains(args[2])) {
        const auto bytes =
            QByteArray::fromBase64(args[3].toLatin1(), QByteArray::Base64UrlEncoding |
                                                           QByteArray::AbortOnBase64DecodingErrors);
        const auto name = QString::fromUtf8(bytes);
        if (name.isEmpty() || name.size() > 256 || name.contains(QChar(0)))
            return 2;
        description = QString("Service : %1\nAction : %2\nLes dépendances ne seront pas arrêtées "
                              "automatiquement.")
                          .arg(name, args[2]);
        operation = [name, action = args[2]] { return features::serviceAction(name, action); };
    } else if (args.size() == 6 && args[1] == "process" &&
               QStringList{"terminate", "priority", "affinity"}.contains(args[4])) {
        bool a = false, b = false, c = false;
        auto pid = args[2].toUInt(&a);
        auto creation = args[3].toULongLong(&b);
        auto value = args[5].toULongLong(&c);
        if (!a || !b || !c || !pid || !creation)
            return 2;
        description =
            QString("Processus PID %1\nIdentité de création FILETIME : %2\nAction : %3\nValeur : "
                    "%4\nPerte de données ou dégradation de performances possible.")
                .arg(pid)
                .arg(creation)
                .arg(args[4])
                .arg(value);
        operation = [pid, creation, action = args[4], value] {
            return features::processAction(pid, creation, action, value);
        };
    } else
        return 2;
    if (QMessageBox::warning(nullptr, "Baawiil — opération administrateur explicite",
                             description + "\n\nAutoriser cette opération précise ?",
                             QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel) != QMessageBox::Yes)
        return 3;
    error = operation();
    if (!error.isEmpty()) {
        QMessageBox::warning(nullptr, "Opération refusée ou échouée", error);
        return 1;
    }
    QMessageBox::information(nullptr, "Baawiil", "Opération terminée selon l'API Windows.");
    return 0;
}
