#include "StreamPlayer.h"
#include "utils/Logger.h"

#ifdef __linux__
#include <alsa/asoundlib.h>
#endif

StreamPlayer::StreamPlayer(QObject *parent) : QObject(parent) {}
StreamPlayer::~StreamPlayer() { stop(); }

void StreamPlayer::play(const QString &streamUrl)
{
    emit logMessage("play() 开始");
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
    emit logMessage("stop() 开始");
    m_paused = false;
    m_running = false;

    if (m_thread) {
        emit logMessage("stop() 等待线程退出...");
        m_thread->quit();
        if (!m_thread->wait(3000))
            emit logMessage("stop() 警告: 线程未在 3 秒内退出");
        m_thread = nullptr;
        emit logMessage("stop() 线程已退出");
    }

    m_wp.store(0);
    m_rp.store(0);
    m_playing = false;
    emit stopped("stopped");
    emit logMessage("stop() 结束");
}

void StreamPlayer::pause()
{
    m_paused = true;
}

void StreamPlayer::resume()
{
    m_paused = false;
    m_wp.store(m_rp.load());
    emit logMessage("跳到直播实时位置");
}

void StreamPlayer::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
}

int StreamPlayer::volume() const { return m_volume; }

// Interrupt callback — ffmpeg calls this during blocking I/O
static int interruptCb(void *ctx)
{
    return *static_cast<std::atomic<bool>*>(ctx) ? 0 : 1;
}

// ── decodeLoop ── all resources are RAII locals, no class members
void StreamPlayer::decodeLoop()
{
    emit logMessage("decodeLoop() 线程开始");

    // ── ffmpeg ──
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

    AVFormatContext *rawFmt = nullptr;
    int ret = avformat_open_input(&rawFmt, m_streamUrl.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret != 0 || !rawFmt) {
        emit logMessage("错误: avformat_open_input 失败");
        m_running = false; return;
    }
    rawFmt->interrupt_callback.callback = interruptCb;
    rawFmt->interrupt_callback.opaque = &m_running;
    std::unique_ptr<AVFormatContext, AvFmtDeleter> fmtCtx(rawFmt);

    if (avformat_find_stream_info(fmtCtx.get(), nullptr) < 0) {
        emit logMessage("错误: 无法获取流信息");
        m_running = false; return;
    }

    int audioIdx = av_find_best_stream(fmtCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIdx < 0) {
        emit logMessage("错误: 未找到音频流");
        m_running = false; return;
    }

    const AVStream *stream = fmtCtx->streams[audioIdx];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) { emit logMessage("错误: 不支持的音频编码"); m_running = false; return; }

    AVCodecContext *rawCodec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(rawCodec, stream->codecpar);
    if (avcodec_open2(rawCodec, codec, nullptr) < 0) {
        emit logMessage("错误: 无法打开解码器");
        avcodec_free_context(&rawCodec);
        m_running = false; return;
    }
    std::unique_ptr<AVCodecContext, AvCodecDeleter> codecCtx(rawCodec);

    // ── swresample → 44100 s16 stereo ──
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout inLayout = codecCtx->ch_layout;
    SwrContext *rawSwr = nullptr;
    swr_alloc_set_opts2(&rawSwr,
        &outLayout, AV_SAMPLE_FMT_S16, 44100,
        &inLayout, codecCtx->sample_fmt, codecCtx->sample_rate,
        0, nullptr);
    if (!rawSwr || swr_init(rawSwr) < 0) {
        emit logMessage("错误: 无法初始化重采样器");
        if (rawSwr) swr_free(&rawSwr);
        m_running = false; return;
    }
    std::unique_ptr<SwrContext, SwrDeleter> swrCtx(rawSwr);

    // ── Linux: ALSA ──
#ifdef __linux__
    snd_pcm_t *rawPcm = nullptr;
    if (snd_pcm_open(&rawPcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0) {
        emit logMessage("错误: 无法打开 ALSA 设备");
        m_running = false; return;
    }
    auto pcmDeleter = [](snd_pcm_t *p) { if (p) { snd_pcm_drain(p); snd_pcm_close(p); } };
    std::unique_ptr<snd_pcm_t, decltype(pcmDeleter)> pcm(rawPcm, pcmDeleter);

    // Set ALSA params
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm.get(), hw);
    snd_pcm_hw_params_set_access(pcm.get(), hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm.get(), hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm.get(), hw, 2);
    unsigned rate = 44100;
    snd_pcm_hw_params_set_rate_near(pcm.get(), hw, &rate, nullptr);
    snd_pcm_hw_params(pcm.get(), hw);
    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(pcm.get(), sw);
    snd_pcm_uframes_t boundary;
    snd_pcm_sw_params_get_boundary(sw, &boundary);
    snd_pcm_sw_params_set_start_threshold(pcm.get(), sw, 1);
    snd_pcm_sw_params_set_stop_threshold(pcm.get(), sw, boundary);
    snd_pcm_sw_params(pcm.get(), sw);
    emit logMessage("ALSA 设备就绪");
#endif

    emit logMessage(QString("解码器就绪: %1").arg(codec->name));

    // ── Decode loop ──
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int totalWritten = 0;

    auto drainAlsa = [&]() {
#ifdef __linux__
        if (!pcm) return;
        snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm.get());
        if (avail <= 0) return;
        int wp = m_wp.load(std::memory_order_acquire);
        int rp = m_rp.load(std::memory_order_relaxed);
        int bufAvail = (wp - rp) & kBufMask;
        if (bufAvail <= 0) return;
        int frames = std::min(static_cast<int>(avail), bufAvail / 2);
        float vol = m_volume / 100.0f;
        if (vol < 1.0f) {
            alignas(16) int16_t tmp[4096];
            int total = frames * 2, pos = 0;
            while (pos < total) {
                int chunk = std::min(total - pos, 4096);
                for (int i = 0; i < chunk; i++)
                    tmp[i] = static_cast<int16_t>(m_buf[(rp + pos + i) & kBufMask] * vol);
                snd_pcm_sframes_t w = snd_pcm_writei(pcm.get(), tmp, chunk / 2);
                if (w < 0) {
                    if (w != -EAGAIN)
                        emit logMessage("ALSA 写入错误");
                    return;
                }
                pos += w * 2;
            }
            m_rp.store((rp + pos) & kBufMask, std::memory_order_release);
        } else {
            auto *src = reinterpret_cast<const int16_t *>(m_buf);
            snd_pcm_sframes_t w = snd_pcm_writei(pcm.get(), src + rp, frames);
            if (w > 0)
                m_rp.store((rp + w * 2) & kBufMask, std::memory_order_release);
            else if (w == -EPIPE)
                snd_pcm_prepare(pcm.get());
        }
#endif
    };

    while (m_running) {
        drainAlsa();
        if (m_paused) { QThread::msleep(50); continue; }

        ret = av_read_frame(fmtCtx.get(), pkt);
        if (ret < 0) {
            if (ret == AVERROR_EXIT) { emit logMessage("av_read_frame 被中断"); break; }
            if (ret == AVERROR_EOF) {
                emit logMessage("流 EOF, 3 秒内重试...");
                for (int i = 0; m_running && i < 30; i++) QThread::msleep(100);
                if (!m_running) break;
                emit logMessage("EOF 重试结束, 继续");
                continue;
            }
            emit logMessage(QString("av_read_frame 错误: %1").arg(ret));
            QThread::msleep(100); av_packet_unref(pkt); continue;
        }
        static int pktCount = 0;
        if ((++pktCount % 50) == 0)
            emit logMessage(QString("读取到包 #%1, stream=%2").arg(pktCount).arg(pkt->stream_index));

        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }

        ret = avcodec_send_packet(codecCtx.get(), pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;

        while (true) {
            ret = avcodec_receive_frame(codecCtx.get(), frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            uint8_t *out[2] = { nullptr, nullptr };
            int outSamples = swr_get_out_samples(swrCtx.get(), frame->nb_samples);
            av_samples_alloc(out, nullptr, 2, outSamples, AV_SAMPLE_FMT_S16, 0);
            int converted = swr_convert(swrCtx.get(), out, outSamples,
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
                    totalWritten += count;
                    if ((totalWritten % 88200) == 0)
                        emit logMessage(QString("播放中... %1 帧已解码").arg(totalWritten / 2));
                }
            }
            av_freep(&out[0]);
            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    emit logMessage("decodeLoop() 线程结束");
    emit stopped("eof");
}
