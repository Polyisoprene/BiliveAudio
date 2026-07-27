#pragma once
#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>

class TrayManager : public QObject {
    Q_OBJECT
public:
    explicit TrayManager(QObject *parent = nullptr);
    void showNotification(const QString &title, const QString &msg);

signals:
    void showWindowRequested();
    void quitRequested();
    void playPauseRequested();

private:
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    void setupIcon();
};
