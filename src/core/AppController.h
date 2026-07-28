#pragma once
#include <QObject>
#include "models/Danmaku.h"
#include "models/LiveRoom.h"

class BilibiliApi;
class AuthManager;
class LiveMonitor;
class StreamPlayer;
class DanmakuManager;
struct UserInfo;

class AppController : public QObject {
    Q_OBJECT
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    static AppController *instance() { return s_instance; }

    BilibiliApi *api() const { return m_api; }

    // Session
    void restoreSession();
    void onLoginSuccess(const QString &cookie, const QString &username);
    void logout();

    // Room control
    void openRoom(qint64 roomId);
    void closeRoom();
    bool isOpeningRoom() const { return m_openingRoom; }

    // Playback
    void togglePlayPause();
    int volume() const;
    void setVolume(int percent);

    // Danmaku
    void sendDanmaku(const QString &text);

signals:
    void loginStateChanged(bool loggedIn, const QString &username);
    void sessionExpired();

    void roomOpened(qint64 roomId, const QString &username, const QString &title);
    void streamReady(qint64 roomId, const QString &url);
    void playStateChanged(bool playing, const QString &status);
    void playbackError(const QString &msg);

    void danmakuReceived(const Danmaku &dm);
    void danmakuConnected(bool connected);

    void liveListUpdated(const QVector<LiveRoom> &rooms);
    void newLiveStarted(const LiveRoom &room);

    void error(const QString &context, const QString &msg);
    void logMessage(const QString &msg);
    void cookieUpdated(const QString &cookie);
    void statusMessage(const QString &msg);

private:
    void setupConnections();
    void onUserInfoReady(const UserInfo &info);

    static AppController *s_instance;

    BilibiliApi *m_api = nullptr;
    AuthManager *m_auth = nullptr;
    LiveMonitor *m_monitor = nullptr;
    StreamPlayer *m_player = nullptr;
    DanmakuManager *m_danmaku = nullptr;
    bool m_openingRoom = false;
};
