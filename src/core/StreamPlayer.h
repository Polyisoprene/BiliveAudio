#pragma once
#include <QObject>
#include <QString>
#ifndef BILIVE_AUDIO_WINDOWS
#include <mpv/client.h>
#endif

class StreamPlayer : public QObject {
    Q_OBJECT
public:
    explicit StreamPlayer(QObject *parent = nullptr);
    ~StreamPlayer() override;

    void play(const QString &streamUrl);
    void stop();
    void pause();
    void resume();
    void setVolume(int percent);
    int volume() const;
    bool isPlaying() const { return m_playing; }

signals:
    void started();
    void stopped();
    void paused();
    void error(const QString &msg);
    void positionChanged(double seconds);

private:
#ifndef BILIVE_AUDIO_WINDOWS
    mpv_handle *m_mpv = nullptr;
    void initMpv();
    void handleEvent(mpv_event *event);
    static void onMpvWakeup(void *ctx);
    void processEvents();
#endif
    bool m_playing = false;
    int m_volume = 80;
};
