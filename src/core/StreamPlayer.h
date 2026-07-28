#pragma once
#include <QObject>
#include <QThread>
#include <QMutex>
#include <cstdint>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#ifdef __linux__
#include <alsa/asoundlib.h>
#endif
}

struct IAudioClient;
struct IAudioRenderClient;

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
    void drainToAlsa();
    void wasapiPullLoop();

    QThread *m_thread = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};

    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    int m_audioStreamIdx = -1;
    QString m_streamUrl;

#ifdef __linux__
    snd_pcm_t *m_pcm = nullptr;
#endif
#ifdef _WIN32
    IAudioClient *m_audioClient = nullptr;
    IAudioRenderClient *m_renderClient = nullptr;
    QThread *m_audioThread = nullptr;
    void *m_audioEvent = nullptr;  // HANDLE
#endif

    static constexpr int kBufBits = 19;
    static constexpr int kBufMask = (1 << kBufBits) - 1;
    static constexpr int kBufSize = 1 << kBufBits;
    int16_t m_buf[kBufSize];
    std::atomic<int> m_wp{0};
    std::atomic<int> m_rp{0};

    QMutex m_alsaMutex;

    int m_volume = 80;
    bool m_playing = false;
};
