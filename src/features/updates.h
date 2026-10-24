#pragma once
#include <QWidget>
#include <QNetworkAccessManager>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QJsonObject>
namespace features {
class Updates : public QWidget {
    QNetworkAccessManager network_;
    QLineEdit *endpoint_, *signer_;
    QCheckBox *automatic_;
    QLabel *status_;
    QPushButton *check_, *download_, *install_;
    QJsonObject manifest_;
    QString downloaded_;
    bool writable_ = true;
    void save();
    void check();
    void download();
    void install();

  public:
    explicit Updates(QWidget *parent = nullptr);
};
} // namespace features
