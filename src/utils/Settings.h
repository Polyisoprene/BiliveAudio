#pragma once
#include <QSettings>
#include <QString>

class Settings {
public:
    static Settings &instance();

    QString cookie() const;
    void setCookie(const QString &cookie);

    int volume() const;
    void setVolume(int vol);

    int windowX() const;
    int windowY() const;
    int windowWidth() const;
    int windowHeight() const;
    void setWindowGeometry(int x, int y, int w, int h);

    qint64 lastRoomId() const;
    void setLastRoomId(qint64 roomId);

    QString logDir() const;
    void setLogDir(const QString &dir);

    int logRetentionDays() const;
    void setLogRetentionDays(int days);

private:
    Settings();
    static QString configPath();
    QSettings m_settings;
};
