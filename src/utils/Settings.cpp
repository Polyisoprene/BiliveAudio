#include "Settings.h"
#include <QStandardPaths>
#include <QDir>

static Settings *g_instance = nullptr;

Settings &Settings::instance()
{
    if (!g_instance)
        g_instance = new Settings;
    return *g_instance;
}

Settings::Settings()
    : m_settings(configPath(), QSettings::IniFormat)
{
}

QString Settings::configPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/biliveaudio.conf";
}

QString Settings::cookie() const
{
    return m_settings.value("auth/cookie").toString();
}

void Settings::setCookie(const QString &cookie)
{
    m_settings.setValue("auth/cookie", cookie);
}

int Settings::volume() const
{
    return m_settings.value("audio/volume", 80).toInt();
}

void Settings::setVolume(int vol)
{
    m_settings.setValue("audio/volume", vol);
}

int Settings::windowX() const
{
    return m_settings.value("window/x", -1).toInt();
}

int Settings::windowY() const
{
    return m_settings.value("window/y", -1).toInt();
}

int Settings::windowWidth() const
{
    return m_settings.value("window/width", 960).toInt();
}

int Settings::windowHeight() const
{
    return m_settings.value("window/height", 640).toInt();
}

void Settings::setWindowGeometry(int x, int y, int w, int h)
{
    m_settings.setValue("window/x", x);
    m_settings.setValue("window/y", y);
    m_settings.setValue("window/width", w);
    m_settings.setValue("window/height", h);
}

qint64 Settings::lastRoomId() const
{
    return m_settings.value("room/lastId", 0).toLongLong();
}

void Settings::setLastRoomId(qint64 roomId)
{
    m_settings.setValue("room/lastId", roomId);
}

QString Settings::logDir() const
{
    QString def = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
    return m_settings.value("log/dir", def).toString();
}

void Settings::setLogDir(const QString &dir)
{
    m_settings.setValue("log/dir", dir);
}

int Settings::logRetentionDays() const
{
    return m_settings.value("log/retention", 7).toInt();
}

void Settings::setLogRetentionDays(int days)
{
    m_settings.setValue("log/retention", qMax(1, days));
}

bool Settings::danmakuImageMode() const
{
    return m_settings.value("danmaku/imageMode", false).toBool();
}

void Settings::setDanmakuImageMode(bool enabled)
{
    m_settings.setValue("danmaku/imageMode", enabled);
}

QString Settings::avatarCacheDir() const
{
    QString def = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/cache/avatars";
    return m_settings.value("cache/avatarDir", def).toString();
}

void Settings::setAvatarCacheDir(const QString &dir)
{
    m_settings.setValue("cache/avatarDir", dir);
}
