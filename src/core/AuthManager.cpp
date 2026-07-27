#include "AuthManager.h"
#include "utils/Logger.h"

AuthManager::AuthManager(BilibiliApi *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(2000);

    connect(m_api, &BilibiliApi::qrCodeFetched, this, &AuthManager::onQRCodeFetched);
    connect(m_api, &BilibiliApi::qrCodePollResult, this, &AuthManager::onQRCodePolled);
    connect(m_api, &BilibiliApi::userInfoReady, this, [this](const UserInfo &info) {
        m_userInfo = info;
        emit userInfoUpdated(info);
    });
}

void AuthManager::startLogin()
{
    setState(State::LoggingIn);
    m_api->fetchQRCode();
}

void AuthManager::logout()
{
    m_api->setCookie({});
    m_userInfo = {};
    setState(State::LoggedOut);
    m_pollTimer->stop();
    LOG_INFO("User logged out");
}

void AuthManager::restoreSession(const QString &cookie)
{
    if (cookie.isEmpty()) return;
    m_api->setCookie(cookie);
    m_api->getUserInfo();
}

void AuthManager::onQRCodeFetched(const QString &url, const QString &key)
{
    m_qrcodeKey = key;
    emit qrCodeReady(url, key);

    QObject::connect(m_pollTimer, &QTimer::timeout, this, [this] {
        m_api->pollQRCode(m_qrcodeKey);
    }, Qt::SingleShotConnection);
    m_pollTimer->start();
    LOG_INFO("QR code displayed, polling started");
}

void AuthManager::onQRCodePolled(const QString &status, const QString &cookie, const QString &username)
{
    if (status == "confirmed") {
        m_pollTimer->stop();
        m_api->setCookie(cookie);
        m_api->getUserInfo();
        setState(State::LoggedIn);
        LOG_INFO("Login successful: {}", username.toStdString());
    }
}

void AuthManager::setState(State s)
{
    if (m_state != s) {
        m_state = s;
        emit stateChanged(s);
    }
}
