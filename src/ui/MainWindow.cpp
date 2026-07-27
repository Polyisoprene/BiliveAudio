#include "MainWindow.h"
#include "TrayManager.h"
#include "LoginDialog.h"
#include "LiveListWidget.h"
#include "PlayerControl.h"
#include "DanmakuPanel.h"
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("BiliveAudio");
    resize(960, 640);
    setMinimumSize(640, 480);

    // Init core services
    m_api = new BilibiliApi(this);
    m_auth = new AuthManager(m_api, this);
    m_monitor = new LiveMonitor(m_api, this);
    m_player = new StreamPlayer(this);
    m_danmaku = new DanmakuManager(m_api, this);

    setupUI();
    setupConnections();
    restoreSession();

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
    topBar->addWidget(m_statusLabel);
    topBar->addStretch();
    topBar->addWidget(m_loginBtn);
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

    applyStyleSheet();
}

void MainWindow::setupConnections()
{
    connect(m_loginBtn, &QPushButton::clicked, this, [this] {
        auto *dlg = new LoginDialog(m_api, this);
        connect(dlg, &LoginDialog::loginSuccess, this, &MainWindow::onLoginSuccess);
        dlg->exec();
        dlg->deleteLater();
    });

    connect(m_openRoomBtn, &QPushButton::clicked, this, [this] {
        QString text = m_roomInput->text().trimmed();
        if (text.isEmpty()) return;
        bool ok;
        qint64 roomId = text.toLongLong(&ok);
        if (!ok) {
            LOG_WARN("Invalid room ID: {}", text.toStdString());
            return;
        }
        LOG_INFO("Opening room: {}", roomId);
        m_playerControl->setRoomInfo(QString::number(roomId), "加载中...");

        m_api->getRoomInfo(roomId);
    });

    connect(m_api, &BilibiliApi::roomInfoReady, this, [this](qint64 roomId, qint64 uid, const QString &title, qint64 cid) {
        m_playerControl->setRoomInfo(QString::number(roomId), title);
        m_api->getStreamUrl(roomId, cid);
        Settings::instance().setLastRoomId(roomId);
    });

    connect(m_api, &BilibiliApi::streamUrlReady, this, [this](qint64 roomId, const QString &url) {
        LOG_INFO("Stream URL received for room {}: {}", roomId, url.toStdString());
        m_player->play(url);
        m_danmaku->connectRoom(roomId);
    });

    connect(m_playerControl, &PlayerControl::playPauseClicked, this, [this] {
        if (m_player->isPlaying())
            m_player->pause();
        else
            m_player->resume();
        m_playerControl->setPlaying(m_player->isPlaying());
    });

    connect(m_playerControl, &PlayerControl::volumeChanged, this, [this](int vol) {
        m_player->setVolume(vol);
        Settings::instance().setVolume(vol);
    });

    connect(m_player, &StreamPlayer::started, this, [this] {
        m_playerControl->setPlaying(true);
        m_statusLabel->setText("播放中");
    });

    connect(m_player, &StreamPlayer::stopped, this, [this] {
        m_playerControl->setPlaying(false);
        m_statusLabel->setText("已停止");
        m_danmaku->disconnectRoom();
    });

    connect(m_player, &StreamPlayer::error, this, [this](const QString &msg) {
        LOG_ERROR("Player error: {}", msg.toStdString());
        m_statusLabel->setText("播放错误: " + msg);
    });

    // Danmaku connections
    connect(m_danmaku, &DanmakuManager::danmakuReceived, m_danmakuPanel, &DanmakuPanel::addDanmaku);
    connect(m_danmaku, &DanmakuManager::connected, this, [this] {
        m_danmakuPanel->setConnected(true);
    });
    connect(m_danmaku, &DanmakuManager::disconnected, this, [this] {
        m_danmakuPanel->setConnected(false);
    });
    connect(m_danmakuPanel, &DanmakuPanel::sendDanmakuRequested, m_danmaku, &DanmakuManager::sendDanmaku);

    // Live monitor connections
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
            m_monitor->setUid(info.uid);
            m_monitor->start();
            LOG_INFO("Session restored: {}", info.username.toStdString());
        } else {
            m_statusLabel->setText("会话已过期，请重新登录");
            Settings::instance().setCookie({});
        }
    }, Qt::SingleShotConnection);
}

void MainWindow::onLoginSuccess(const QString &cookie, const QString &username)
{
    Settings::instance().setCookie(cookie);
    m_statusLabel->setText(QString("已登录: %1").arg(username));
    m_loginBtn->setText("切换账号");

    m_api->getUserInfo();
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
