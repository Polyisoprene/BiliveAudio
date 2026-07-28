#include "StreamPlayer.h"
#include "utils/Logger.h"
#include <QAudioFormat>
#include <QMediaDevices>
#include <algorithm>

StreamPlayer::StreamPlayer(QObject *parent)
    : QObject(parent)
{
    m_feedTimer = new QTimer(this);
    m_feedTimer->setInterval(40);
    connect(m_feedTimer, &QTimer::timeout, this, &StreamPlayer::feedAudio);
}

StreamPlayer::~StreamPlayer()
{
    stop();
}

void StreamPlayer::play(const QString &streamUrl)
{
    stop();
    m_streamUrl = streamUrl;
    m_running = true;
    m_playing = true;
    m_thread = QThread::create([this] { decodeLoop(); });
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
    emit started();
    emit logMessage("播放线程启动");
}

void StreamPlayer::stop()
{
    m_running = false;
    m_paused = false;
    m_feedTimer->stop();

    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_audioDevice = nullptr;
    }

    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
        m_thread = nullptr;
    }

    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
    }

    m_wp = 0;
    m_rp = 0;
    m_audioReady = false;
    m_playing = false;
    emit stopped("stopped");
}

void StreamPlayer::pause()
{
    m_paused = true;
    if (m_audioSink)
        m_audioSink->suspend();
}

void StreamPlayer::resume()
{
    m_paused = false;
    m_wp.store(m_rp.load());  // flush buffer
    if (m_audioSink)
        m_audioSink->resume();
    emit logMessage("跳到直播实时位置");
}

void StreamPlayer::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
}

int StreamPlayer::volume() const { return m_volume; }

void StreamPlayer::decodeLoop()
{
    avformat_network_init();

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "reconnect_delay_max", "5", 0);
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "probesize", "500000", 0);
    av_dict_set(&opts, "referer", "https://live.bilibili.com/", 0);
    av_dict_set(&opts, "user_agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36", 0);
    av_dict_set(&opts, "timeout", "10000000", 0);

    if (avformat_open_input(&m_fmtCtx, m_streamUrl.toUtf8().constData(), nullptr, &opts) != 0) {
        emit logMessage("错误: avformat_open_input 失败");
        m_running = false;
        return;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        emit logMessage("错误: 无法获取流信息");
        m_running = false;
        return;
    }

    m_audioStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIdx < 0) {
        emit logMessage("错误: 未找到音频流");
        m_running = false;
        return;
    }

    const AVStream *stream = m_fmtCtx->streams[m_audioStreamIdx];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        emit logMessage("错误: 不支持的音频编码");
        m_running = false;
        return;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, stream->codecpar);

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        emit logMessage("错误: 无法打开解码器");
        m_running = false;
        return;
    }

    // Set up resampler → 44100 s16 stereo
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout inLayout = m_codecCtx->ch_layout;
    swr_alloc_set_opts2(&m_swrCtx,
        &outLayout, AV_SAMPLE_FMT_S16, 44100,
        &inLayout, m_codecCtx->sample_fmt, m_codecCtx->sample_rate,
        0, nullptr);
    if (!m_swrCtx || swr_init(m_swrCtx) < 0) {
        emit logMessage("错误: 无法初始化重采样器");
        m_running = false;
        return;
    }

    // Start audio output on main thread
    m_audioReady = false;
    QMetaObject::invokeMethod(this, [this] {
        QAudioFormat fmt;
        fmt.setSampleRate(44100);
        fmt.setChannelCount(2);
        fmt.setSampleFormat(QAudioFormat::Int16);
        auto dev = QMediaDevices::defaultAudioOutput();
        emit logMessage(QString("音频设备: %1").arg(dev.description()));
        if (dev.isNull()) {
            emit logMessage("错误: 无可用音频输出设备");
            m_audioReady = true;
            return;
        }
        m_audioSink = new QAudioSink(dev, fmt);
        m_audioSink->setVolume(m_volume / 100.0);
        m_audioDevice = m_audioSink->start();
        if (!m_audioDevice) {
            emit logMessage("错误: QAudioSink::start() 返回空");
            m_audioReady = true;
            return;
        }
        emit logMessage("音频输出就绪");
        m_audioReady = true;
        m_feedTimer->start();
    }, Qt::QueuedConnection);

    while (!m_audioReady && m_running)
        QThread::msleep(5);
    if (!m_running) return;

    emit logMessage(QString("解码器就绪: %1").arg(codec->name));

    // Decode loop
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    while (m_running) {
        if (m_paused) {
            QThread::msleep(50);
            continue;
        }

        int ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) break;
            if (ret == AVERROR_EXIT) break;
            QThread::msleep(100);
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->stream_index != m_audioStreamIdx) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(m_codecCtx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;

        while (true) {
            ret = avcodec_receive_frame(m_codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // Convert to s16 stereo
            uint8_t *out[2] = { nullptr, nullptr };
            int outSamples = swr_get_out_samples(m_swrCtx, frame->nb_samples);
            av_samples_alloc(out, nullptr, 2, outSamples, AV_SAMPLE_FMT_S16, 0);
            int converted = swr_convert(m_swrCtx, out, outSamples,
                                        (const uint8_t **)frame->data, frame->nb_samples);
            if (converted > 0) {
                int16_t *samples = reinterpret_cast<int16_t *>(out[0]);
                int count = converted * 2;
                int wp = m_wp.load(std::memory_order_relaxed);
                int rp = m_rp.load(std::memory_order_acquire);
                int space = kBufSize - ((wp - rp) & kBufMask);

                if (space >= count) {
                    for (int i = 0; i < count; i++) {
                        m_buf[(wp + i) & kBufMask] = samples[i];
                    }
                    m_wp.store((wp + count) & kBufMask, std::memory_order_release);
                }
                // else: buffer full, drop packet (live streaming)
            }
            av_freep(&out[0]);
            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&m_fmtCtx);
    m_running = false;
    emit stopped("eof");
}

void StreamPlayer::feedAudio()
{
    if (!m_audioDevice || m_paused) return;

    int wp = m_wp.load(std::memory_order_acquire);
    int rp = m_rp.load(std::memory_order_relaxed);
    int avail = (wp - rp) & kBufMask;
    if (avail < 2048) return;  // wait for ~23ms of audio

    int toRead = std::min(avail, 3528);   // 40ms worth at 44100 s16 stereo
    float vol = m_volume / 100.0f;

    alignas(16) int16_t tmp[4096];
    for (int i = 0; i < toRead; i++) {
        int16_t s = m_buf[(rp + i) & kBufMask];
        tmp[i] = static_cast<int16_t>(s * vol);
    }

    m_rp.store((rp + toRead) & kBufMask, std::memory_order_release);
    m_audioDevice->write(reinterpret_cast<const char *>(tmp), toRead * sizeof(int16_t));
}
