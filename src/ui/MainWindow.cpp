#include "MainWindow.h"
#include "TrayManager.h"
#include "LoginDialog.h"
#include "LiveListWidget.h"
#include "PlayerControl.h"
#include "DanmakuPanel.h"
#include "DanmakuWindow.h"
#include "SettingsDialog.h"
#include "core/AppController.h"
#include "core/BilibiliApi.h"
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

    m_ctrl = new AppController(this);

    setupUI();
    m_danmakuWindow = new DanmakuWindow;
    setupConnections();

    m_tray = new TrayManager(this);
    connect(m_tray, &TrayManager::showWindowRequested, this, [this] {
        showNormal();
        activateWindow();
    });
    connect(m_tray, &TrayManager::quitRequested, qApp, &QApplication::quit);

    m_ctrl->restoreSession();

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

    m_popOutBtn = new QPushButton("弹出弹幕框");
    m_popOutBtn->setStyleSheet("background-color: #444;");
    topBar->addWidget(m_popOutBtn);

    auto *settingsBtn = new QPushButton("设置");
    settingsBtn->setStyleSheet("background-color: #444;");
    topBar->addWidget(settingsBtn);
    connect(settingsBtn, &QPushButton::clicked, this, [this] {
        SettingsDialog dlg(this);
        dlg.exec();
    });

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
    // Login / logout buttons
    connect(m_loginBtn, &QPushButton::clicked, this, [this] {
        auto *dlg = new LoginDialog(m_ctrl->api(), this);
        connect(dlg, &LoginDialog::loginSuccess, this, [this](const QString &cookie, const QString &username) {
            m_ctrl->onLoginSuccess(cookie, username);
        });
        dlg->exec();
        dlg->deleteLater();
    });

    connect(m_logoutBtn, &QPushButton::clicked, m_ctrl, &AppController::logout);

    // Room open
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
        m_ctrl->openRoom(roomId);
    });

    // Live list room selection
    connect(m_liveList, &LiveListWidget::roomSelected, this, [this](qint64 roomId, const QString &username, const QString &title) {
        m_playerControl->setRoomInfo(username, title);
        m_danmakuPanel->clear();
        m_danmakuWindow->clear();
        m_ctrl->openRoom(roomId);
    });

    // Player control
    connect(m_playerControl, &PlayerControl::playPauseClicked, this, [this] {
        m_ctrl->togglePlayPause();
    });

    connect(m_playerControl, &PlayerControl::stopClicked, m_ctrl, &AppController::closeRoom);

    connect(m_playerControl, &PlayerControl::volumeChanged, this, [this](int vol) {
        m_ctrl->setVolume(vol);
    });

    // AppController → UI
    connect(m_ctrl, &AppController::loginStateChanged, this, [this](bool loggedIn, const QString &username) {
        if (loggedIn) {
            m_statusLabel->setText(QString("已登录: %1").arg(username));
            m_loginBtn->setText("切换账号");
            m_logoutBtn->setVisible(true);
        } else {
            m_statusLabel->setText("未登录");
            m_loginBtn->setText("登录");
            m_logoutBtn->setVisible(false);
        }
    });

    connect(m_ctrl, &AppController::statusMessage, m_statusLabel, &QLabel::setText);

    connect(m_ctrl, &AppController::streamReady, this, [this](qint64 roomId, const QString &url) {
        Q_UNUSED(roomId);
        Q_UNUSED(url);
        m_danmakuPanel->clear();
        m_danmakuWindow->clear();
    });

    connect(m_ctrl, &AppController::playStateChanged, this, [this](bool playing, const QString &status) {
        m_playerControl->setPlaying(playing);
        m_statusLabel->setText(status);
    });

    connect(m_ctrl, &AppController::playbackError, this, [this](const QString &msg) {
        m_statusLabel->setText("播放错误: " + msg);
        appendLog(QString("[错误] 播放器: %1").arg(msg));
    });

    // Danmaku - dual output
    connect(m_ctrl, &AppController::danmakuReceived, m_danmakuPanel, &DanmakuPanel::addDanmaku);
    connect(m_ctrl, &AppController::danmakuReceived, m_danmakuWindow, &DanmakuWindow::addDanmaku);
    connect(m_ctrl, &AppController::danmakuConnected, this, [this](bool connected) {
        m_danmakuPanel->setConnected(connected);
        m_danmakuWindow->setConnected(connected);
    });

    connect(m_danmakuPanel, &DanmakuPanel::sendDanmakuRequested, m_ctrl, &AppController::sendDanmaku);
    connect(m_danmakuWindow, &DanmakuWindow::sendDanmakuRequested, m_ctrl, &AppController::sendDanmaku);

    // Pop-out / close danmaku window
    connect(m_popOutBtn, &QPushButton::clicked, this, [this] {
        m_danmakuPanel->setVisible(false);
        m_danmakuWindow->setVisible(true);
    });
    connect(m_danmakuWindow, &DanmakuWindow::closed, this, [this] {
        m_danmakuWindow->setVisible(false);
        m_danmakuPanel->setVisible(true);
    });

    // Live list
    connect(m_ctrl, &AppController::liveListUpdated, m_liveList, &LiveListWidget::updateList);
    connect(m_ctrl, &AppController::newLiveStarted, this, [this](const LiveRoom &room) {
        if (m_tray)
            m_tray->showNotification("正在直播",
                QString("%1 - %2").arg(room.username, room.title));
    });

    // Errors & logs
    connect(m_ctrl, &AppController::error, this, [this](const QString &ctx, const QString &err) {
        appendLog(QString("[错误] %1: %2").arg(ctx, err));
    });
    connect(m_ctrl, &AppController::logMessage, this, &MainWindow::appendLog);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
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
