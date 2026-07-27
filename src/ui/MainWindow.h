#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCloseEvent>

class TrayManager;
class LoginDialog;
class LiveListWidget;
class PlayerControl;
class DanmakuPanel;
class BilibiliApi;
class AuthManager;
class LiveMonitor;
class StreamPlayer;
class DanmakuManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUI();
    void setupConnections();
    void applyStyleSheet();
    void restoreSession();
    void onLoginSuccess(const QString &cookie, const QString &username);

    // Core
    BilibiliApi *m_api = nullptr;
    AuthManager *m_auth = nullptr;
    LiveMonitor *m_monitor = nullptr;
    StreamPlayer *m_player = nullptr;
    DanmakuManager *m_danmaku = nullptr;

    // UI
    TrayManager *m_tray = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_roomInput = nullptr;
    QPushButton *m_openRoomBtn = nullptr;
    QPushButton *m_loginBtn = nullptr;
    LiveListWidget *m_liveList = nullptr;
    PlayerControl *m_playerControl = nullptr;
    DanmakuPanel *m_danmakuPanel = nullptr;
};
