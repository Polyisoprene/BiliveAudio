#include "StreamPlayer.h"
#include "utils/Logger.h"
#include <QAudioFormat>
#include <QMediaDevices>

StreamPlayer::StreamPlayer(QObject *parent)
    : QObject(parent)
{
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

    m_wp.store(0);
    m_rp.store(0);
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
    m_wp.store(m_rp.load());  // flush buffer, jump to live
    if (m_audioSink)
        m_audioSink->resume();
    emit logMessage("跳到直播实时位置");
}

void StreamPlayer::setVolume(int percent)
{
    m_volumeF = qBound(0, percent, 100) / 100.0f;
}

int StreamPlayer::volume() const
{
    return static_cast<int>(m_volumeF * 100);
}

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

    // Resampler → 44100 s16 stereo
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

    // Create audio sink + device on main thread
    QMetaObject::invokeMethod(this, [this] {
        QAudioFormat fmt;
        fmt.setSampleRate(44100);
        fmt.setChannelCount(2);
        fmt.setSampleFormat(QAudioFormat::Int16);
        auto dev = QMediaDevices::defaultAudioOutput();
        emit logMessage(QString("音频设备: %1").arg(dev.description()));
        if (dev.isNull()) {
            emit logMessage("错误: 无可用音频输出设备");
            return;
        }
        m_audioDevice = new AudioDevice(m_buf, kBufMask, m_wp, m_rp, &m_volumeF);
        m_audioDevice->open(QIODevice::ReadOnly);
        m_audioSink = new QAudioSink(dev, fmt);
        m_audioSink->start(m_audioDevice);
        emit logMessage("音频输出就绪");
    }, Qt::BlockingQueuedConnection);

    emit logMessage(QString("解码器就绪: %1").arg(codec->name));

    // Decode loop — writes into ring buffer, AudioDevice reads from it
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int frameCount = 0;

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

            uint8_t *out[2] = { nullptr, nullptr };
            int outSamples = swr_get_out_samples(m_swrCtx, frame->nb_samples);
            av_samples_alloc(out, nullptr, 2, outSamples, AV_SAMPLE_FMT_S16, 0);
            int converted = swr_convert(m_swrCtx, out, outSamples,
                                        (const uint8_t **)frame->data, frame->nb_samples);
            if (converted > 0) {
                auto *samples = reinterpret_cast<int16_t *>(out[0]);
                int count = converted * 2;
                int wp = m_wp.load(std::memory_order_relaxed);
                int rp = m_rp.load(std::memory_order_acquire);
                int space = kBufSize - ((wp - rp) & kBufMask);

                if (space >= count) {
                    for (int i = 0; i < count; i++)
                        m_buf[(wp + i) & kBufMask] = samples[i];
                    m_wp.store((wp + count) & kBufMask, std::memory_order_release);
                    if ((++frameCount % 100) == 0)
                        emit logMessage(QString("解码中... %1 samples, buf=%2/%3")
                            .arg(count).arg((m_wp.load() - m_rp.load()) & kBufMask).arg(kBufSize));
                }
            }
            av_freep(&out[0]);
            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&m_fmtCtx);
    m_running = false;

    // Stop audio after decode finishes
    QMetaObject::invokeMethod(this, [this] {
        if (m_audioSink) {
            m_audioSink->stop();
            delete m_audioSink;
            m_audioSink = nullptr;
            m_audioDevice = nullptr;
        }
    }, Qt::BlockingQueuedConnection);

    emit stopped("eof");
}

// Called by QAudioSink when it needs PCM data (push model)
qint64 StreamPlayer::AudioDevice::readData(char *data, qint64 maxLen)
{
    if (maxLen < 64) return 0;

    int wp = m_wp.load(std::memory_order_acquire);
    int rp = m_rp.load(std::memory_order_relaxed);
    int avail = (wp - rp) & m_mask;
    int wanted = static_cast<int>(maxLen / sizeof(int16_t));
    int toRead = std::min(avail, wanted);

    if (toRead <= 0) return 0;

    float vol = *m_vol;
    auto *dst = reinterpret_cast<int16_t *>(data);
    for (int i = 0; i < toRead; i++)
        dst[i] = static_cast<int16_t>(m_buf[(rp + i) & m_mask] * vol);

    m_rp.store((rp + toRead) & m_mask, std::memory_order_release);
    return toRead * sizeof(int16_t);
}
