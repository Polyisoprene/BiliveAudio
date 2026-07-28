#pragma once
#include <QObject>
#include <QThread>
#include <QAudioSink>
#include <QTimer>
#include <QMutex>
#include <QByteArray>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

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
    void stopped(const QString &reason);
    void error(const QString &msg);
    void logMessage(const QString &msg);

private:
    void decodeLoop();
    void feedAudio();

    QThread *m_thread = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};

    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    int m_audioStreamIdx = -1;
    QString m_streamUrl;

    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QTimer *m_feedTimer = nullptr;

    static constexpr int kBufSize = 44100 * 4 * 4;
    QByteArray m_buf;
    QMutex m_bufMutex;
    int m_bufLevel = 0;

    int m_volume = 80;
    bool m_playing = false;
};
