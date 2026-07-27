#include "TrayManager.h"
#include <QIcon>
#include <QPainter>
#include <QApplication>
#include <QStyle>

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
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 64, 64);
    grad.setColorAt(0, QColor("#533483"));
    grad.setColorAt(1, QColor("#0f3460"));
    painter.setBrush(grad);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(2, 2, 60, 60, 12, 12);

    painter.setPen(QPen(Qt::white, 3));
    QFont font = painter.font();
    font.setPixelSize(28);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter, "B");
    painter.end();

    m_trayIcon->setIcon(QIcon(pixmap));
}

void TrayManager::showNotification(const QString &title, const QString &msg)
{
    m_trayIcon->showMessage(title, msg, QSystemTrayIcon::Information, 5000);
}
