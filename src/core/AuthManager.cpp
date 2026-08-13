#include "AuthManager.h"
#include "utils/Logger.h"

AuthManager::AuthManager(BilibiliApi *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(2000);

    // 常规连接（不是SingleShotConnection）：否则定时器只触发一次轮询就断开
    connect(m_pollTimer, &QTimer::timeout, this, [this] {
        m_api->pollQRCode(m_qrcodeKey);
    });

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
    m_pollActive = true;
    m_api->fetchQRCode();
}

void AuthManager::logout()
{
    m_api->setCookie({});
    m_userInfo = {};
    setState(State::LoggedOut);
    m_pollActive = false;
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

    // 仅自己发起的登录（startLogin）才轮询；LoginDialog 的登录由对话框自己轮询，
    // 否则同一 m_api 会被两个定时器同时 poll，产生重复请求和重复登录成功
    if (!m_pollActive) return;
    m_pollTimer->start();
    LOG_INFO("QR code displayed, polling started");
}

void AuthManager::onQRCodePolled(const QString &status, const QString &cookie, const QString &username)
{
    if (status == "confirmed") {
        m_pollActive = false;
        m_pollTimer->stop();
        m_api->setCookie(cookie);
        // 不在这里 getUserInfo：LoginDialog 确认后会走 AppController::onLoginSuccess
        // 拉取用户信息，这里再发一次会造成同一登录流程触发两次 nav 请求
        setState(State::LoggedIn);
        LOG_INFO("Login successful: {}", username.toStdString());
    } else if (status == "expired") {
        // 二维码过期，停止轮询，等待界面刷新二维码
        m_pollActive = false;
        m_pollTimer->stop();
    }
}

void AuthManager::setState(State s)
{
    if (m_state != s) {
        m_state = s;
        emit stateChanged(s);
    }
}
