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
    mpv_set_option_string(m_mpv, "cache", "no");
    mpv_set_option_string(m_mpv, "audio-buffer", "1");
    mpv_set_option_string(m_mpv, "reconnect", "1");
    mpv_set_option_string(m_mpv, "reconnect_streamed", "1");
    mpv_set_option_string(m_mpv, "user-agent",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    mpv_set_option_string(m_mpv, "referrer",
        "https://live.bilibili.com/");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "2M");
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "512k");

    // Wake up the main thread's event loop when mpv has events
    mpv_set_wakeup_callback(m_mpv, wakeup, this);

    if (mpv_initialize(m_mpv) < 0) {
        emit error("mpv_initialize failed");
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    // Observe playback state and cache
    mpv_observe_property(m_mpv, 0, "playback-time", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "demuxer-cache-state", MPV_FORMAT_NODE);
}

void StreamPlayer::wakeup(void *ctx)
{
    auto *player = static_cast<StreamPlayer*>(ctx);
    QMetaObject::invokeMethod(player, "processEvents", Qt::QueuedConnection);
}

void StreamPlayer::processEvents()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(event);
    }
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
        if (!prop) break;
        // Copy name before comparing — mpv may free the pointer after we return
        const char *name = prop->name;
        if (!name) break;
        if (prop->format == MPV_FORMAT_FLAG && strcmp(name, "eof-reached") == 0) {
            if (*static_cast<int*>(prop->data)) {
                m_playing = false;
                emit stopped("eof");
            }
        }
        if (prop->format == MPV_FORMAT_NODE && strcmp(name, "demuxer-cache-state") == 0) {
            if (auto *node = static_cast<mpv_node*>(prop->data)) {
                auto *map = node->u.list;
                for (int i = 0; i < map->num; i += 2) {
                    if (map->values[i].u.string && strcmp(map->values[i].u.string, "fw-bytes") == 0) {
                        double bytes = map->values[i+1].u.double_;
                        if (bytes > 1024 * 1024) {
                            emit logMessage(QString("mpv demuxer cache: %1 MB").arg(bytes / 1024 / 1024, 0, 'f', 1));
                        }
                    }
                }
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
