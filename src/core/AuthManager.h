#pragma once
#include <QObject>
#include <QTimer>
#include <QImage>
#include "BilibiliApi.h"
#include "models/UserInfo.h"

class AuthManager : public QObject {
    Q_OBJECT
public:
    enum class State { LoggedOut, LoggingIn, LoggedIn };
    Q_ENUM(State)

    explicit AuthManager(BilibiliApi *api, QObject *parent = nullptr);

    State state() const { return m_state; }
    const UserInfo &userInfo() const { return m_userInfo; }

    void startLogin();
    void logout();
    void restoreSession(const QString &cookie);

signals:
    void stateChanged(State newState);
    void qrCodeReady(const QString &url, const QString &key);
    void loginFailed(const QString &reason);
    void userInfoUpdated(const UserInfo &info);

private:
    BilibiliApi *m_api = nullptr;
    State m_state = State::LoggedOut;
    UserInfo m_userInfo;
    QString m_qrcodeKey;
    QTimer *m_pollTimer = nullptr;

    void onQRCodeFetched(const QString &url, const QString &key);
    void onQRCodePolled(const QString &status, const QString &cookie, const QString &username);
    void setState(State s);
};
