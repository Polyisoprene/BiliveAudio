#include "StreamPlayer.h"
#include "utils/Logger.h"
#include <cstring>

StreamPlayer::StreamPlayer(QObject *parent)
    : QObject(parent)
{
}

StreamPlayer::~StreamPlayer()
{
    stop();
}

void StreamPlayer::initMpv()
{
    if (m_mpv) return;

    m_mpv = mpv_create();
    if (!m_mpv) {
        emit error("mpv_create failed");
        return;
    }

    // Audio-only configuration
    mpv_set_option_string(m_mpv, "vo", "null");
    mpv_set_option_string(m_mpv, "video", "no");
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "cache-secs", "10");
    mpv_set_option_string(m_mpv, "audio-buffer", "2");
    mpv_set_option_string(m_mpv, "reconnect", "1");
    mpv_set_option_string(m_mpv, "user-agent",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    mpv_set_option_string(m_mpv, "referrer",
        "https://live.bilibili.com/");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "10M");

    if (mpv_initialize(m_mpv) < 0) {
        emit error("mpv_initialize failed");
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    // Observe playback state
    mpv_observe_property(m_mpv, 0, "playback-time", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
}

void StreamPlayer::play(const QString &streamUrl)
{
    stop();
    initMpv();
    if (!m_mpv) return;

    m_playing = true;
    m_running = true;

    // Set volume
    double vol = m_volume.load();
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);

    // Start playback
    QByteArray url = streamUrl.toUtf8();
    const char *cmd[] = {"loadfile", url.constData(), nullptr};
    int r = mpv_command(m_mpv, cmd);
    if (r < 0) {
        emit error(QString("mpv loadfile failed: %1").arg(mpv_error_string(r)));
        m_playing = false;
        m_running = false;
        return;
    }

    // Start event thread
    m_eventThread = QThread::create([this] {
        while (m_running && m_mpv) {
            mpv_event *event = mpv_wait_event(m_mpv, 0.5);
            if (event->event_id == MPV_EVENT_NONE)
                continue;
            handleEvent(event);
        }
    });
    connect(m_eventThread, &QThread::finished, m_eventThread, &QObject::deleteLater);
    m_eventThread->start();

    emit started();
}

void StreamPlayer::handleEvent(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_END_FILE: {
        auto *ef = static_cast<mpv_event_end_file*>(event->data);
        QString msg;
        switch (ef->reason) {
        case MPV_END_FILE_REASON_EOF: msg = "eof"; break;
        case MPV_END_FILE_REASON_ERROR: msg = mpv_error_string(ef->error); break;
        default: msg = "stopped"; break;
        }
        m_playing = false;
        emit stopped(msg);
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *prop = static_cast<mpv_event_property*>(event->data);
        if (prop->format == MPV_FORMAT_FLAG && strcmp(prop->name, "eof-reached") == 0) {
            if (*static_cast<int*>(prop->data)) {
                m_playing = false;
                emit stopped("eof");
            }
        }
        break;
    }
    default:
        break;
    }
}

void StreamPlayer::stop()
{
    m_running = false;

    if (m_eventThread) {
        emit logMessage("stop() 等待事件线程退出...");
        m_eventThread->quit();
        if (!m_eventThread->wait(5000)) {
            m_eventThread->terminate();
            m_eventThread->wait();
        }
        m_eventThread = nullptr;
    }

    if (m_mpv) {
        const char *cmd[] = {"stop", nullptr};
        mpv_command(m_mpv, cmd);
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
    }

    m_playing = false;
    emit stopped("stopped");
    emit logMessage("stop() 结束");
}

void StreamPlayer::pause()
{
    if (!m_mpv) return;
    int flag = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void StreamPlayer::resume()
{
    if (!m_mpv) return;
    int flag = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void StreamPlayer::setVolume(int percent)
{
    m_volume.store(qBound(0, percent, 100));
    if (m_mpv) {
        double vol = m_volume.load();
        mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
    }
}
