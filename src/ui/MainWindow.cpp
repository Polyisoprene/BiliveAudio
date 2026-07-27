#include "MainWindow.h"
#include "TrayManager.h"
#include "LoginDialog.h"
#include "LiveListWidget.h"
#include "PlayerControl.h"
#include "DanmakuPanel.h"
#include "DanmakuWindow.h"
#include "core/BilibiliApi.h"
#include "core/AuthManager.h"
#include "core/LiveMonitor.h"
#include "core/StreamPlayer.h"
#include "core/DanmakuManager.h"
#include "utils/Logger.h"
#include "utils/Settings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QFile>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("BiliveAudio");
    resize(960, 640);
    setMinimumSize(640, 480);

    m_api = new BilibiliApi(this);
    m_auth = new AuthManager(m_api, this);
    m_monitor = new LiveMonitor(m_api, this);
    m_player = new StreamPlayer(this);
    m_danmaku = new DanmakuManager(m_api, this);

    setupUI();
    setupConnections();
    restoreSession();

    m_danmakuWindow = new DanmakuWindow;
    m_danmakuWindow->show();

    m_tray = new TrayManager(this);
    connect(m_tray, &TrayManager::showWindowRequested, this, [this] {
        showNormal();
        activateWindow();
    });
    connect(m_tray, &TrayManager::quitRequested, qApp, &QApplication::quit);

    LOG_INFO("MainWindow created");
}

MainWindow::~MainWindow()
{
    LOG_INFO("MainWindow destroyed");
}

void MainWindow::setupUI()
{
    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Top bar
    auto *topBar = new QHBoxLayout;
    m_statusLabel = new QLabel("未登录");
    m_loginBtn = new QPushButton("登录");
    m_logoutBtn = new QPushButton("退出登录");
    m_logoutBtn->setVisible(false);
    topBar->addWidget(m_statusLabel);
    topBar->addStretch();
    topBar->addWidget(m_loginBtn);
    topBar->addWidget(m_logoutBtn);
    mainLayout->addLayout(topBar);

    // Content area
    auto *contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(8);

    m_liveList = new LiveListWidget;
    m_liveList->setMinimumWidth(200);
    m_liveList->setMaximumWidth(300);
    contentLayout->addWidget(m_liveList);

    auto *rightPanel = new QVBoxLayout;
    rightPanel->setSpacing(8);

    auto *roomBar = new QHBoxLayout;
    m_roomInput = new QLineEdit;
    m_roomInput->setPlaceholderText("输入直播间号");
    m_openRoomBtn = new QPushButton("打开");
    roomBar->addWidget(m_roomInput);
    roomBar->addWidget(m_openRoomBtn);
    rightPanel->addLayout(roomBar);

    m_playerControl = new PlayerControl;
    rightPanel->addWidget(m_playerControl);

    m_danmakuPanel = new DanmakuPanel;
    rightPanel->addWidget(m_danmakuPanel);

    contentLayout->addLayout(rightPanel);
    mainLayout->addLayout(contentLayout, 1);

    // Log view
    m_logView = new QTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(130);
    m_logView->setPlaceholderText("日志输出...");
    m_logView->setStyleSheet("background-color: #0d1117; color: #8b949e; font-size: 12px; border: 1px solid #0f3460;");
    mainLayout->addWidget(m_logView);

    applyStyleSheet();
}

void MainWindow::setupConnections()
{
    connect(m_auth, &AuthManager::userInfoUpdated, this, [this](const UserInfo &info) {
        if (info.isLoggedIn)
            m_danmaku->setUid(info.uid);
    });

    connect(m_api, &BilibiliApi::cookieUpdated, this, [](const QString &cookie) {
        Settings::instance().setCookie(cookie);
    });

    connect(m_loginBtn, &QPushButton::clicked, this, [this] {
        auto *dlg = new LoginDialog(m_api, this);
        connect(dlg, &LoginDialog::loginSuccess, this, &MainWindow::onLoginSuccess);
        dlg->exec();
        dlg->deleteLater();
    });

    connect(m_logoutBtn, &QPushButton::clicked, this, &MainWindow::onLogout);

    connect(m_openRoomBtn, &QPushButton::clicked, this, [this] {
        QString text = m_roomInput->text().trimmed();
        if (text.isEmpty()) return;
        bool ok;
        qint64 roomId = text.toLongLong(&ok);
        if (!ok) {
            appendLog(QString("[警告] 无效的房间号: %1").arg(text));
            return;
        }
        appendLog(QString("[信息] 准备打开房间: %1").arg(roomId));
        m_playerControl->setRoomInfo(QString::number(roomId), "加载中...");
        m_openingRoom = true;
        m_player->stop();
        m_api->getRoomInfo(roomId);
    });

    connect(m_api, &BilibiliApi::requestError, this, [this](const QString &ctx, const QString &err) {
        appendLog(QString("[错误] %1: %2").arg(ctx, err));
    });

    connect(m_api, &BilibiliApi::roomInfoReady, this, [this](qint64 roomId, qint64 uid, const QString &title, qint64 cid) {
        appendLog(QString("[信息] 房间信息: room=%1 uid=%2 cid=%3").arg(roomId).arg(uid).arg(cid));
        m_playerControl->setRoomInfo(QString::number(roomId), title);
        m_api->getStreamUrl(roomId, cid);
        Settings::instance().setLastRoomId(roomId);
    });

    connect(m_api, &BilibiliApi::streamUrlReady, this, [this](qint64 roomId, const QString &url) {
        if (url.isEmpty()) {
            appendLog(QString("[错误] 未获取到房间 %1 的流地址").arg(roomId));
            return;
        }
        appendLog(QString("[信息] 房间 %1 流地址就绪，开始播放").arg(roomId));
        m_player->play(url);
        m_danmaku->connectRoom(roomId);
    });

    connect(m_playerControl, &PlayerControl::playPauseClicked, this, [this] {
        if (m_playerControl->isPlaying()) {
            m_player->pause();
            m_playerControl->setPlaying(false);
        } else {
            m_player->resume();
            m_playerControl->setPlaying(true);
        }
    });

    connect(m_playerControl, &PlayerControl::stopClicked, this, [this] {
        appendLog("[信息] 手动停止播放");
        m_player->stop();
        m_danmaku->disconnectRoom();
        m_playerControl->setPlaying(false);
        m_statusLabel->setText("已停止");
        m_playerControl->setRoomInfo({}, {});
    });

    connect(m_playerControl, &PlayerControl::volumeChanged, this, [this](int vol) {
        m_player->setVolume(vol);
        Settings::instance().setVolume(vol);
    });

    connect(m_player, &StreamPlayer::started, this, [this] {
        m_openingRoom = false;
        m_playerControl->setPlaying(true);
        m_statusLabel->setText("播放中");
        appendLog("[信息] 播放开始");
    });

    connect(m_player, &StreamPlayer::stopped, this, [this](const QString &reason) {
        if (m_openingRoom) return;
        m_playerControl->setPlaying(false);
        m_statusLabel->setText("已停止");
        m_danmaku->disconnectRoom();
        appendLog(QString("[信息] 播放已停止 — %1").arg(reason));
    });

    connect(m_player, &StreamPlayer::error, this, [this](const QString &msg) {
        m_statusLabel->setText("播放错误: " + msg);
        appendLog(QString("[错误] 播放器: %1").arg(msg));
    });

    connect(m_player, &StreamPlayer::logMessage, this, &MainWindow::appendLog);

    // Danmaku — dual output to panel + floating window
    connect(m_danmaku, &DanmakuManager::danmakuReceived, m_danmakuPanel, &DanmakuPanel::addDanmaku);
    connect(m_danmaku, &DanmakuManager::danmakuReceived, m_danmakuWindow, &DanmakuWindow::addDanmaku);
    connect(m_danmaku, &DanmakuManager::connected, this, [this] {
        m_danmakuPanel->setConnected(true);
        m_danmakuWindow->setConnected(true);
    });
    connect(m_danmaku, &DanmakuManager::disconnected, this, [this] {
        m_danmakuPanel->setConnected(false);
        m_danmakuWindow->setConnected(false);
    });
    connect(m_danmaku, &DanmakuManager::logMessage, this, &MainWindow::appendLog);
    connect(m_danmakuPanel, &DanmakuPanel::sendDanmakuRequested, m_danmaku, &DanmakuManager::sendDanmaku);

    // Live monitor
    connect(m_monitor, &LiveMonitor::liveListUpdated, m_liveList, &LiveListWidget::updateList);
    connect(m_liveList, &LiveListWidget::roomSelected, this, [this](qint64 roomId, const QString &username, const QString &title) {
        m_playerControl->setRoomInfo(username, title);
        m_player->stop();
        m_danmaku->disconnectRoom();
        m_api->getRoomInfo(roomId);
    });
    connect(m_monitor, &LiveMonitor::newLiveStarted, this, [this](const LiveRoom &room) {
        if (m_tray)
            m_tray->showNotification("正在直播",
                QString("%1 - %2").arg(room.username, room.title));
    });
}

void MainWindow::restoreSession()
{
    auto &settings = Settings::instance();
    QString cookie = settings.cookie();
    if (!cookie.isEmpty()) {
        m_api->setCookie(cookie);
        m_auth->restoreSession(cookie);
        m_statusLabel->setText("恢复会话中...");
    }

    int vol = settings.volume();
    m_playerControl->setVolume(vol);
    m_player->setVolume(vol);

    connect(m_api, &BilibiliApi::userInfoReady, this, [this](const UserInfo &info) {
        if (info.isLoggedIn) {
            m_statusLabel->setText(QString("已登录: %1").arg(info.username));
            m_loginBtn->setText("切换账号");
            m_logoutBtn->setVisible(true);
            m_monitor->setUid(info.uid);
            m_danmaku->setUid(info.uid);
            m_monitor->start();
            LOG_INFO("Session restored: {}", info.username.toStdString());
        } else {
            m_statusLabel->setText("会话已过期，请重新登录");
            m_logoutBtn->setVisible(false);
            Settings::instance().setCookie({});
        }
    }, Qt::SingleShotConnection);
}

void MainWindow::onLoginSuccess(const QString &cookie, const QString &username)
{
    Settings::instance().setCookie(cookie);
    m_statusLabel->setText(QString("已登录: %1").arg(username));
    m_loginBtn->setText("切换账号");
    m_logoutBtn->setVisible(true);

    m_api->getUserInfo();
}

void MainWindow::onLogout()
{
    m_api->setCookie({});
    Settings::instance().setCookie({});
    m_statusLabel->setText("未登录");
    m_loginBtn->setText("登录");
    m_logoutBtn->setVisible(false);
    m_monitor->stop();
    m_player->stop();
    m_danmaku->disconnectRoom();
    appendLog("[信息] 已退出登录");
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Save geometry
    auto &settings = Settings::instance();
    settings.setWindowGeometry(x(), y(), width(), height());

    if (m_tray && QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        event->ignore();
        LOG_INFO("Minimized to tray");
    }
}

void MainWindow::applyStyleSheet()
{
    QFile file(":/style.qss");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStyleSheet(file.readAll());
        file.close();
    }
}

void MainWindow::appendLog(const QString &msg)
{
    if (!m_logView) return;
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logView->append(QString("[%1] %2").arg(ts, msg));
}
