#include "Settings.h"

static Settings *g_instance = nullptr;

Settings &Settings::instance()
{
    if (!g_instance)
        g_instance = new Settings;
    return *g_instance;
}

Settings::Settings()
    : m_settings("BiliveAudio", "BiliveAudio")
{
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
