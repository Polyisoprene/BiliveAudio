#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

class BilibiliApi;

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(BilibiliApi *api, QWidget *parent = nullptr);

signals:
    void loginSuccess(const QString &cookie, const QString &username);

private:
    BilibiliApi *m_api = nullptr;
    QLabel *m_qrLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QTimer *m_pollTimer = nullptr;
    QString m_qrcodeKey;

    void fetchQRCode();
    void onQRCodeFetched(const QString &url, const QString &key);
    void onPollResult(const QString &status, const QString &cookie, const QString &username);
};
