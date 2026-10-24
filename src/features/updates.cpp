#include "updates.h"
#include "backend.h"
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QTimer>
#include <QProcess>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>
namespace features {
namespace {
QString config() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/updates.json";
}
bool https(const QUrl &url) {
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() &&
           url.userInfo().isEmpty();
}
QNetworkRequest request(const QUrl &url) {
    QNetworkRequest r(url);
    r.setTransferTimeout(30000);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setMaximumRedirectsAllowed(3);
    return r;
}
} // namespace
Updates::Updates(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Centre de mises à jour • version 0.2.0-dev"));
    layout->addWidget(
        new QLabel("Aucun canal publié n'est configuré. Une URL HTTPS de manifeste et l'empreinte "
                   "SHA-256 du certificat de l'éditeur sont nécessaires. Ne copier ces paramètres "
                   "que depuis une source de confiance."));
    endpoint_ = new QLineEdit;
    endpoint_->setPlaceholderText("https://…/manifest.json");
    endpoint_->setAccessibleName("URL du manifeste HTTPS");
    layout->addWidget(endpoint_);
    signer_ = new QLineEdit;
    signer_->setPlaceholderText(
        "Empreinte SHA-256 du certificat signataire : 64 caractères hexadécimaux");
    signer_->setAccessibleName("Certificat épinglé");
    layout->addWidget(signer_);
    automatic_ = new QCheckBox(
        "Vérification automatique facultative au lancement puis toutes les 24 heures");
    layout->addWidget(automatic_);
    check_ = new QPushButton("Vérifier manuellement");
    download_ = new QPushButton("Télécharger et vérifier");
    install_ = new QPushButton("Installer après confirmation…");
    download_->setEnabled(false);
    install_->setEnabled(false);
    layout->addWidget(check_);
    layout->addWidget(download_);
    layout->addWidget(install_);
    status_ = new QLabel("Aucune requête réseau effectuée.");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch();
    QFile file(config());
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly) && file.size() < 65536) {
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(file.readAll(), &error);
            auto obj = doc.object();
            if (error.error == QJsonParseError::NoError && obj["schema"].toInt() == 1) {
                endpoint_->setText(obj["endpoint"].toString());
                signer_->setText(obj["signerSha256"].toString());
                automatic_->setChecked(obj["automatic"].toBool());
            } else
                writable_ = false;
        } else
            writable_ = false;
    }
    if (!writable_)
        status_->setText("Configuration de mises à jour invalide : conservée sans écrasement.");
    connect(check_, &QPushButton::clicked, this, &Updates::check);
    connect(download_, &QPushButton::clicked, this, &Updates::download);
    connect(install_, &QPushButton::clicked, this, &Updates::install);
    connect(endpoint_, &QLineEdit::editingFinished, this, [this] {
        manifest_ = {};
        download_->setEnabled(false);
        install_->setEnabled(false);
        save();
    });
    connect(signer_, &QLineEdit::editingFinished, this, [this] {
        install_->setEnabled(false);
        save();
    });
    connect(automatic_, &QCheckBox::toggled, this, [this](bool enabled) {
        save();
        if (enabled)
            check();
    });
    auto *timer = new QTimer(this);
    timer->setInterval(24 * 60 * 60 * 1000);
    connect(timer, &QTimer::timeout, this, [this] {
        if (automatic_->isChecked())
            check();
    });
    timer->start();
    if (automatic_->isChecked())
        QTimer::singleShot(1000, this, &Updates::check);
}
void Updates::save() {
    if (!writable_ || property("suspendPersistence").toBool())
        return;
    QDir().mkpath(QFileInfo(config()).absolutePath());
    QSaveFile file(config());
    auto data = QJsonDocument(QJsonObject{{"schema", 1},
                                          {"endpoint", endpoint_->text()},
                                          {"signerSha256", signer_->text()},
                                          {"automatic", automatic_->isChecked()}})
                    .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        status_->setText("Échec d'enregistrement des paramètres de mises à jour.");
}
void Updates::check() {
    QUrl url(endpoint_->text());
    if (!https(url)) {
        status_->setText("Configurer une URL HTTPS valide avant la vérification.");
        return;
    }
    check_->setEnabled(false);
    download_->setEnabled(false);
    install_->setEnabled(false);
    manifest_ = {};
    status_->setText("Vérification HTTPS…");
    auto *reply = network_.get(request(url));
    auto body = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, body] {
        body->append(reply->readAll());
        if (body->size() > 65536)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, body] {
        reply->deleteLater();
        check_->setEnabled(true);
        body->append(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || body->size() > 65536 ||
            !https(reply->url())) {
            status_->setText("Échec de vérification HTTPS : " + reply->errorString());
            return;
        }
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(*body, &error);
        auto obj = doc.object();
        QVersionNumber version = QVersionNumber::fromString(obj["version"].toString());
        const auto hash = obj["sha256"].toString();
        QUrl artifact(obj["url"].toString());
        qint64 size = obj["size"].toInteger();
        if (error.error != QJsonParseError::NoError || obj["schema"].toInt() != 1 ||
            version.isNull() || !https(artifact) || size <= 0 || size > 256ll * 1024 * 1024 ||
            !QRegularExpression("^[a-fA-F0-9]{64}$").match(hash).hasMatch()) {
            status_->setText("Manifeste invalide ou non pris en charge.");
            return;
        }
        if (QVersionNumber::compare(version, QVersionNumber(0, 2, 0)) <= 0) {
            status_->setText("Aucune version plus récente annoncée.");
            return;
        }
        manifest_ = obj;
        download_->setEnabled(true);
        status_->setText("Version " + obj["version"].toString() + " disponible.\n" +
                         obj["notes"].toString().left(8000));
    });
}
void Updates::download() {
    if (manifest_.isEmpty())
        return;
    if (!QRegularExpression("^[a-fA-F0-9]{64}$").match(signer_->text()).hasMatch()) {
        status_->setText("Une empreinte de certificat valide est obligatoire.");
        return;
    }
    auto manifest = manifest_;
    auto signer = signer_->text();
    auto directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/updates";
    QDir().mkpath(directory);
    auto path = directory + "/installer.exe";
    auto file = std::make_shared<QSaveFile>(path);
    if (!file->open(QIODevice::WriteOnly)) {
        status_->setText(file->errorString());
        return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto count = std::make_shared<qint64>(0);
    auto failed = std::make_shared<bool>(false);
    auto *reply = network_.get(request(QUrl(manifest["url"].toString())));
    check_->setEnabled(false);
    download_->setEnabled(false);
    install_->setEnabled(false);
    status_->setText("Téléchargement HTTPS, limite 256 Mio…");
    auto consume = [reply, file, hash, count, failed, manifest] {
        auto bytes = reply->readAll();
        *count += bytes.size();
        if (*count > manifest["size"].toInteger() || file->write(bytes) != bytes.size()) {
            *failed = true;
            reply->abort();
            return;
        }
        hash->addData(bytes);
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, file, hash, count, failed, manifest, path, signer, consume] {
            consume();
            reply->deleteLater();
            check_->setEnabled(true);
            download_->setEnabled(true);
            if (reply->error() != QNetworkReply::NoError || *failed ||
                *count != manifest["size"].toInteger() ||
                hash->result().toHex() != manifest["sha256"].toString().toLatin1().toLower() ||
                !https(reply->url())) {
                file->cancelWriting();
                status_->setText(
                    "Téléchargement rejeté : erreur réseau, taille ou somme SHA-256 incorrecte.");
                return;
            }
            if (!file->commit()) {
                status_->setText(file->errorString());
                return;
            }
            status_->setText("Vérification de signature dans le collecteur isolé…");
            download_->setEnabled(false);
            auto *watcher = new QFutureWatcher<Result>(this);
            connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, path] {
                auto result = watcher->result();
                watcher->deleteLater();
                download_->setEnabled(true);
                if (!result.error.isEmpty()) {
                    QFile::remove(path);
                    status_->setText("Installateur rejeté : " + result.error);
                    return;
                }
                downloaded_ = path;
                install_->setEnabled(true);
                status_->setText("SHA-256 et signature locale vérifiés ; certificat signataire "
                                 "conforme. La révocation en ligne n'a pas été vérifiée. "
                                 "Installation volontaire uniquement.");
            });
            watcher->setFuture(QtConcurrent::run([path, signer] {
                return isolatedTask("verify-installer",
                                    QJsonObject{{"path", path}, {"signer", signer}},
                                    std::make_shared<std::atomic_bool>(false));
            }));
        });
}
void Updates::install() {
    if (downloaded_.isEmpty())
        return;
    if (QMessageBox::question(
            this, "Installation",
            "Lancer l'installateur vérifié puis fermer l'application ? Arrêter d'abord les travaux "
            "et sessions actifs. La mise à jour reste facultative.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    auto *watcher = new QFutureWatcher<QString>(this);
    install_->setEnabled(false);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        auto error = watcher->result();
        watcher->deleteLater();
        status_->setText(error.isEmpty() ? "Installateur lancé ; installation non encore confirmée."
                                         : error);
        if (error.isEmpty())
            QCoreApplication::quit();
    });
    watcher->setFuture(QtConcurrent::run(
        [path = downloaded_, signer = signer_->text()] { return launchInstaller(path, signer); }));
}
} // namespace features
