#include "StreamPlayer.h"
#include "utils/Logger.h"
#include <QApplication>
#include <QTimer>
#ifdef __linux__
#include <malloc.h>
#endif

StreamPlayer::StreamPlayer(QObject *parent)
    : QObject(parent)
{
    initMpv();
}

StreamPlayer::~StreamPlayer()
{
    if (m_mpv) {
        mpv_command_string(m_mpv, "stop");
        mpv_terminate_destroy(m_mpv);
    }
}

void StreamPlayer::initMpv()
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        LOG_CRITICAL("Failed to create mpv handle");
        return;
    }

    // Audio-only configuration
    mpv_set_option_string(m_mpv, "vo", "null");
    mpv_set_option_string(m_mpv, "video", "no");
    mpv_set_option_string(m_mpv, "audio-display", "no");

    // Live streaming optimization
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "cache-secs", "5");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "5MiB");
    mpv_set_option_string(m_mpv, "demuxer-readahead-secs", "2");
    mpv_set_option_string(m_mpv, "stream-lavf-o",
                          "reconnect=1:reconnect_streamed=1:reconnect_delay_max=5");

    // Low latency
    mpv_set_option_string(m_mpv, "profile", "low-latency");

    // Error on missing audio (not video)
    mpv_set_option_string(m_mpv, "no-audio-display", "yes");

    if (mpv_initialize(m_mpv) < 0) {
        LOG_CRITICAL("Failed to initialize mpv");
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_set_wakeup_callback(m_mpv, onMpvWakeup, this);
    LOG_INFO("mpv initialized");
}

void StreamPlayer::onMpvWakeup(void *ctx)
{
    auto *self = static_cast<StreamPlayer *>(ctx);
    QTimer::singleShot(0, self, &StreamPlayer::processEvents);
}

void StreamPlayer::processEvents()
{
    if (!m_mpv) return;
    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        handleEvent(event);
    }
}

void StreamPlayer::handleEvent(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_START_FILE:
        LOG_INFO("mpv: started playback");
        m_playing = true;
        emit started();
        break;
    case MPV_EVENT_END_FILE:
        LOG_INFO("mpv: playback ended (reason={})", event->error);
        m_playing = false;
        emit stopped();
        break;
    default:
        break;
    }
}

void StreamPlayer::play(const QString &streamUrl)
{
    if (!m_mpv) {
        emit error("mpv not initialized");
        return;
    }

    LOG_INFO("Playing: {}", streamUrl.toStdString());
    const char *cmd[] = {"loadfile", streamUrl.toUtf8().constData(), "replace", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

void StreamPlayer::stop()
{
    if (!m_mpv) return;
    mpv_command_string(m_mpv, "stop");
    m_playing = false;
#ifdef __linux__
    malloc_trim(0);
#endif
    LOG_INFO("mpv: stopped, heap trimmed");
}

void StreamPlayer::pause()
{
    if (!m_mpv) return;
    mpv_set_property_string(m_mpv, "pause", "yes");
}

void StreamPlayer::resume()
{
    if (!m_mpv) return;
    mpv_set_property_string(m_mpv, "pause", "no");
}

void StreamPlayer::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
    if (m_mpv)
        mpv_set_property_string(m_mpv, "volume", QByteArray::number(m_volume).constData());
}

int StreamPlayer::volume() const { return m_volume; }
