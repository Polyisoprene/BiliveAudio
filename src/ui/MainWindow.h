#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QCloseEvent>

class TrayManager;
class LoginDialog;
class LiveListWidget;
class PlayerControl;
class DanmakuPanel;
class AppController;

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
    void appendLog(const QString &msg);

    AppController *m_ctrl = nullptr;

    // UI (Qt parent-child = RAII)
    TrayManager *m_tray = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_roomInput = nullptr;
    QPushButton *m_openRoomBtn = nullptr;
    QPushButton *m_loginBtn = nullptr;
    QPushButton *m_logoutBtn = nullptr;
    LiveListWidget *m_liveList = nullptr;
    PlayerControl *m_playerControl = nullptr;
    DanmakuPanel *m_danmakuPanel = nullptr;
    QTextEdit *m_logView = nullptr;
};
