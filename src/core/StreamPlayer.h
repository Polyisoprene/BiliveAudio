#pragma once
#include <QObject>
#include <QThread>
#include <QAudioSink>
#include <QIODevice>
#include <cstdint>
#include <atomic>
#include <algorithm>

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
    class AudioDevice : public QIODevice {
    public:
        AudioDevice(int16_t *buf, int mask,
                    std::atomic<int> &wp, std::atomic<int> &rp,
                    float *vol)
            : m_buf(buf), m_mask(mask), m_wp(wp), m_rp(rp), m_vol(vol) {}
        bool isSequential() const override { return true; }
    protected:
        qint64 readData(char *data, qint64 maxLen) override;
        qint64 writeData(const char *, qint64) override { return -1; }
    private:
        int16_t *m_buf;
        int m_mask;
        std::atomic<int> &m_wp;
        std::atomic<int> &m_rp;
        float *m_vol;
    };

    void decodeLoop();

    QThread *m_thread = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};

    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    int m_audioStreamIdx = -1;
    QString m_streamUrl;

    QAudioSink *m_audioSink = nullptr;
    AudioDevice *m_audioDevice = nullptr;

    static constexpr int kBufBits = 19;
    static constexpr int kBufMask = (1 << kBufBits) - 1;
    static constexpr int kBufSize = 1 << kBufBits;
    int16_t m_buf[kBufSize];
    std::atomic<int> m_wp{0};
    std::atomic<int> m_rp{0};

    float m_volumeF = 0.8f;
    bool m_playing = false;
};
