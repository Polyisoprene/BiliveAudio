#pragma once
#include <QObject>
#include <QThread>
#include <memory>
#include <cstdint>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

// RAII deleters for C resources
struct AvFmtDeleter {
    void operator()(AVFormatContext *p) const { if (p) avformat_close_input(&p); }
};
struct AvCodecDeleter {
    void operator()(AVCodecContext *p) const { if (p) avcodec_free_context(&p); }
};
struct SwrDeleter {
    void operator()(SwrContext *p) const { if (p) swr_free(&p); }
};

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

    QThread *m_thread = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};

    QString m_streamUrl;

    static constexpr int kBufBits = 19;
    static constexpr int kBufMask = (1 << kBufBits) - 1;
    static constexpr int kBufSize = 1 << kBufBits;
    int16_t m_buf[kBufSize];
    std::atomic<int> m_wp{0};
    std::atomic<int> m_rp{0};

    int m_volume = 80;
    bool m_playing = false;
};
