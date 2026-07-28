#include "AppController.h"
#include "BilibiliApi.h"
#include "AuthManager.h"
#include "LiveMonitor.h"
#include "StreamPlayer.h"
#include "DanmakuManager.h"
#include "utils/Logger.h"
#include "utils/Settings.h"

#include <QApplication>

AppController *AppController::s_instance = nullptr;

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    s_instance = this;

    m_api = new BilibiliApi(this);
    m_auth = new AuthManager(m_api, this);
    m_monitor = new LiveMonitor(m_api, this);
    m_player = new StreamPlayer(this);
    m_danmaku = new DanmakuManager(m_api, this);

    setupConnections();

    LOG_INFO("AppController created");
}

AppController::~AppController()
{
    LOG_INFO("AppController destroyed");
    s_instance = nullptr;
}

void AppController::setupConnections()
{
    // Auth → cookie persistence
    connect(m_api, &BilibiliApi::cookieUpdated, this, [](const QString &cookie) {
        Settings::instance().setCookie(cookie);
    });

    // Auth → danmaku uid
    connect(m_auth, &AuthManager::userInfoUpdated, this, [this](const UserInfo &info) {
        if (info.isLoggedIn)
            m_danmaku->setUid(info.uid);
    });

    // API errors
    connect(m_api, &BilibiliApi::requestError, this, &AppController::error);

    // Room info → stream URL
    connect(m_api, &BilibiliApi::roomInfoReady, this, [this](qint64 roomId, qint64 uid, const QString &title, qint64 cid) {
        LOG_INFO("Room info: room={} uid={} cid={}", roomId, uid, cid);
        Settings::instance().setLastRoomId(roomId);
        m_api->getStreamUrl(roomId, cid);
    });

    // Stream URL → play + connect danmaku
    connect(m_api, &BilibiliApi::streamUrlReady, this, [this](qint64 roomId, const QString &url) {
        if (url.isEmpty()) {
            emit error("streamUrl", "未获取到流地址");
            return;
        }
        emit streamReady(roomId, url);
        m_player->play(url);
        m_danmaku->connectRoom(roomId);
    });

    // Player states
    connect(m_player, &StreamPlayer::started, this, [this] {
        m_openingRoom = false;
        emit playStateChanged(true, "播放中");
    });

    connect(m_player, &StreamPlayer::stopped, this, [this](const QString &reason) {
        if (m_openingRoom) return;
        m_danmaku->disconnectRoom();
        emit playStateChanged(false, "已停止");
    });

    connect(m_player, &StreamPlayer::error, this, [this](const QString &msg) {
        emit playbackError(msg);
    });

    connect(m_player, &StreamPlayer::logMessage, this, &AppController::logMessage);

    // Danmaku
    connect(m_danmaku, &DanmakuManager::danmakuReceived, this, &AppController::danmakuReceived);
    connect(m_api, &BilibiliApi::userFaceReady, this, [](qint64 uid, const QString &url) {
        DanmakuManager::addToFaceCache(QString::number(uid), url);
    });
    connect(m_danmaku, &DanmakuManager::connected, this, [this] {
        emit danmakuConnected(true);
    });
    connect(m_danmaku, &DanmakuManager::disconnected, this, [this] {
        emit danmakuConnected(false);
    });
    connect(m_danmaku, &DanmakuManager::logMessage, this, &AppController::logMessage);

    // Live monitor
    connect(m_monitor, &LiveMonitor::liveListUpdated, this, &AppController::liveListUpdated);
    connect(m_monitor, &LiveMonitor::newLiveStarted, this, &AppController::newLiveStarted);
}

void AppController::restoreSession()
{
    auto &settings = Settings::instance();
    QString cookie = settings.cookie();
    if (!cookie.isEmpty()) {
        m_api->setCookie(cookie);
        m_auth->restoreSession(cookie);
        emit statusMessage("恢复会话中...");
    }

    int vol = settings.volume();
    m_player->setVolume(vol);

    connect(m_api, &BilibiliApi::userInfoReady, this, &AppController::onUserInfoReady, Qt::SingleShotConnection);
}

void AppController::onUserInfoReady(const UserInfo &info)
{
    if (info.isLoggedIn) {
        m_monitor->setUid(info.uid);
        m_danmaku->setUid(info.uid);
        m_monitor->start();
        emit loginStateChanged(true, info.username);
        LOG_INFO("Session restored: {}", info.username.toStdString());
    } else {
        Settings::instance().setCookie({});
        emit loginStateChanged(false, {});
        emit statusMessage("会话已过期，请重新登录");
    }
}

void AppController::onLoginSuccess(const QString &cookie, const QString &username)
{
    Settings::instance().setCookie(cookie);
    m_api->setCookie(cookie);
    emit loginStateChanged(true, username);
    m_api->getUserInfo();
}

void AppController::logout()
{
    m_api->setCookie({});
    Settings::instance().setCookie({});
    m_monitor->stop();
    m_player->stop();
    m_danmaku->disconnectRoom();
    emit loginStateChanged(false, {});
    emit statusMessage("未登录");
    emit logMessage("已退出登录");
}

void AppController::openRoom(qint64 roomId)
{
    m_openingRoom = true;
    m_player->stop();
    m_danmaku->disconnectRoom();
    m_api->getRoomInfo(roomId);
}

void AppController::closeRoom()
{
    m_player->stop();
    m_danmaku->disconnectRoom();
    emit playStateChanged(false, "已停止");
    emit logMessage("手动停止播放");
}

void AppController::togglePlayPause()
{
    if (m_player->isPlaying()) {
        if (m_player->isPlaying())
            m_player->pause();
        else
            m_player->resume();
    }
}

int AppController::volume() const { return m_player->volume(); }

void AppController::setVolume(int percent)
{
    m_player->setVolume(percent);
    Settings::instance().setVolume(percent);
}

void AppController::sendDanmaku(const QString &text)
{
    m_danmaku->sendDanmaku(text);
}
