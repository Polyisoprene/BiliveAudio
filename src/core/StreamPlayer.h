#pragma once
#include <QObject>
#include <QThread>
#include <QAtomicInt>
#include <mpv/client.h>

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
    int volume() const { return m_volume; }
    bool isPlaying() const { return m_playing; }

signals:
    void started();
    void stopped(const QString &reason);
    void error(const QString &msg);
    void logMessage(const QString &msg);

private:
    void initMpv();
    void handleEvent(mpv_event *event);

    mpv_handle *m_mpv = nullptr;
    QThread *m_eventThread = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_volume{80};
    bool m_playing = false;
};
