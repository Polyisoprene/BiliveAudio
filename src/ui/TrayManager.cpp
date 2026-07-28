#include "TrayManager.h"
#include <QIcon>
#include <QApplication>

TrayManager::TrayManager(QObject *parent)
    : QObject(parent)
{
    m_trayMenu = new QMenu;

    auto *showAction = m_trayMenu->addAction("显示窗口");
    connect(showAction, &QAction::triggered, this, &TrayManager::showWindowRequested);

    auto *playPauseAction = m_trayMenu->addAction("播放/暂停");
    connect(playPauseAction, &QAction::triggered, this, &TrayManager::playPauseRequested);

    m_trayMenu->addSeparator();

    auto *quitAction = m_trayMenu->addAction("退出");
    connect(quitAction, &QAction::triggered, this, &TrayManager::quitRequested);

    m_trayIcon = new QSystemTrayIcon;
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("BiliveAudio");
    setupIcon();
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            emit showWindowRequested();
    });
}

void TrayManager::setupIcon()
{
    QIcon icon(":/icon.png");
    if (!icon.isNull())
        m_trayIcon->setIcon(icon);
    else
        m_trayIcon->setIcon(QIcon::fromTheme("multimedia-player"));
}

void TrayManager::showNotification(const QString &title, const QString &msg)
{
    m_trayIcon->showMessage(title, msg, QSystemTrayIcon::Information, 5000);
}
