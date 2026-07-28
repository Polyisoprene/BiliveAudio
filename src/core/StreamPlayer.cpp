#include "StreamPlayer.h"
#include "utils/Logger.h"

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

#ifdef _WIN32
    if (m_audioThread) {
        m_audioThread->quit();
        m_audioThread->wait(3000);
        m_audioThread = nullptr;
    }
    if (m_audioClient) {
        m_audioClient->Stop();
        m_audioClient->Release();
        m_audioClient = nullptr;
    }
    if (m_renderClient) {
        m_renderClient->Release();
        m_renderClient = nullptr;
    }
    if (m_audioEvent) {
        CloseHandle(m_audioEvent);
        m_audioEvent = nullptr;
    }
#endif
#ifdef __linux__
    if (m_pcm) {
        snd_pcm_drain(m_pcm);
        snd_pcm_close(m_pcm);
        m_pcm = nullptr;
    }
#endif

    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
        m_thread = nullptr;
    }

    if (m_swrCtx) { swr_free(&m_swrCtx); m_swrCtx = nullptr; }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); }
    if (m_fmtCtx) { avformat_close_input(&m_fmtCtx); }

    m_wp.store(0);
    m_rp.store(0);
    m_playing = false;
    emit stopped("stopped");
}

void StreamPlayer::pause()
{
    m_paused = true;
#ifdef __linux__
    if (m_pcm) snd_pcm_pause(m_pcm, 1);
#endif
#ifdef _WIN32
    if (m_audioClient) m_audioClient->Stop();
#endif
}

void StreamPlayer::resume()
{
    m_paused = false;
    m_wp.store(m_rp.load());
#ifdef __linux__
    if (m_pcm) {
        snd_pcm_pause(m_pcm, 0);
        snd_pcm_prepare(m_pcm);
    }
#endif
#ifdef _WIN32
    if (m_audioClient) m_audioClient->Reset();
    if (m_audioClient) m_audioClient->Start();
#endif
    emit logMessage("跳到直播实时位置");
}

void StreamPlayer::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
}

int StreamPlayer::volume() const { return m_volume; }

// ── Linux: ALSA ──────────────────────────────────────────────
#ifdef __linux__
static int alsaSetParams(snd_pcm_t *pcm)
{
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, 2);
    unsigned rate = 44100;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
    snd_pcm_hw_params(pcm, hw);
    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(pcm, sw);
    snd_pcm_uframes_t boundary;
    snd_pcm_sw_params_get_boundary(sw, &boundary);
    snd_pcm_sw_params_set_start_threshold(pcm, sw, 1);
    snd_pcm_sw_params_set_stop_threshold(pcm, sw, boundary);
    snd_pcm_sw_params(pcm, sw);
    return 0;
}

void StreamPlayer::drainToAlsa()
{
    if (!m_pcm) return;

    snd_pcm_sframes_t avail = snd_pcm_avail_update(m_pcm);
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
            snd_pcm_sframes_t w = snd_pcm_writei(m_pcm, tmp, chunk / 2);
            if (w < 0) {
                if (w == -EAGAIN) return;
                emit logMessage(QString("ALSA 写入错误: %1").arg(QString::fromUtf8(snd_strerror(static_cast<int>(w)))));
                snd_pcm_prepare(m_pcm); return;
            }
            pos += w * 2;
        }
        m_rp.store((rp + pos) & kBufMask, std::memory_order_release);
    } else {
        auto *src = reinterpret_cast<const int16_t *>(m_buf);
        snd_pcm_sframes_t w = snd_pcm_writei(m_pcm, src + rp, frames);
        if (w > 0) {
            m_rp.store((rp + w * 2) & kBufMask, std::memory_order_release);
        } else {
            if (w == -EPIPE) {
                emit logMessage("ALSA 欠载, 复位中...");
                snd_pcm_prepare(m_pcm);
            } else if (w != -EAGAIN) {
                emit logMessage(QString("ALSA 写入错误: %1").arg(QString::fromUtf8(snd_strerror(static_cast<int>(w)))));
            }
        }
    }
}
#endif

// ── Windows: WASAPI ──────────────────────────────────────────
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

void StreamPlayer::wasapiPullLoop()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Fill initial buffer
    UINT32 bufSize;
    m_audioClient->GetBufferSize(&bufSize);
    BYTE *data = nullptr;
    if (SUCCEEDED(m_renderClient->GetBuffer(bufSize, &data))) {
        int16_t *dst = reinterpret_cast<int16_t *>(data);
        int total = bufSize * 2;
        int wp = m_wp.load(std::memory_order_acquire);
        int rp = m_rp.load(std::memory_order_relaxed);
        int avail = (wp - rp) & kBufMask;
        int toFill = std::min(avail, total);
        float vol = m_volume / 100.0f;
        for (int i = 0; i < toFill; i++)
            dst[i] = static_cast<int16_t>(m_buf[(rp + i) & kBufMask] * vol);
        // Zero-fill remainder (buffer underrun protection)
        for (int i = toFill; i < total; i++)
            dst[i] = 0;
        m_rp.store((rp + toFill) & kBufMask, std::memory_order_release);
        m_renderClient->ReleaseBuffer(bufSize, 0);
    }

    m_audioClient->Start();
    HANDLE evt = (HANDLE)m_audioEvent;
    emit logMessage("WASAPI 音频输出就绪");

    while (m_running) {
        DWORD ret = WaitForSingleObject(evt, 100);
        if (ret == WAIT_TIMEOUT) continue;
        if (ret != WAIT_OBJECT_0 || !m_running) break;

        UINT32 padding;
        if (FAILED(m_audioClient->GetCurrentPadding(&padding))) break;
        UINT32 frames = bufSize - padding;
        if (frames == 0) continue;

        if (SUCCEEDED(m_renderClient->GetBuffer(frames, &data))) {
            int16_t *dst = reinterpret_cast<int16_t *>(data);
            int total = frames * 2;
            int wp = m_wp.load(std::memory_order_acquire);
            int rp = m_rp.load(std::memory_order_relaxed);
            int avail = (wp - rp) & kBufMask;
            int toFill = std::min(avail, total);
            float vol = m_volume / 100.0f;
            for (int i = 0; i < toFill; i++)
                dst[i] = static_cast<int16_t>(m_buf[(rp + i) & kBufMask] * vol);
            for (int i = toFill; i < total; i++)
                dst[i] = 0;
            m_rp.store((rp + toFill) & kBufMask, std::memory_order_release);
            m_renderClient->ReleaseBuffer(frames, 0);
        }
    }

    m_audioClient->Stop();
    CoUninitialize();
}
#endif

// ── Common: decode loop ───────────────────────────────────────
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
        emit logMessage("错误: avformat_open_input 失败"); m_running = false; return;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        emit logMessage("错误: 无法获取流信息"); m_running = false; return;
    }

    m_audioStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIdx < 0) {
        emit logMessage("错误: 未找到音频流"); m_running = false; return;
    }

    const AVStream *stream = m_fmtCtx->streams[m_audioStreamIdx];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) { emit logMessage("错误: 不支持的音频编码"); m_running = false; return; }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, stream->codecpar);
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        emit logMessage("错误: 无法打开解码器"); m_running = false; return;
    }

    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout inLayout = m_codecCtx->ch_layout;
    swr_alloc_set_opts2(&m_swrCtx,
        &outLayout, AV_SAMPLE_FMT_S16, 44100,
        &inLayout, m_codecCtx->sample_fmt, m_codecCtx->sample_rate, 0, nullptr);
    if (!m_swrCtx || swr_init(m_swrCtx) < 0) {
        emit logMessage("错误: 无法初始化重采样器"); m_running = false; return;
    }

    // ── Platform audio init ──
#ifdef __linux__
    const char *alsaDevice = "default";
    if (snd_pcm_open(&m_pcm, alsaDevice, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0)
        emit logMessage(QString("错误: 无法打开 ALSA 设备 %1").arg(alsaDevice));
    else {
        alsaSetParams(m_pcm);
        emit logMessage("ALSA 设备就绪");
    }
#endif
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *enum_ = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                     __uuidof(IMMDeviceEnumerator), (void**)&enum_);
    if (enum_) {
        IMMDevice *dev = nullptr;
        enum_->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        if (dev) {
            dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_audioClient);
            WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0 };
            if (m_audioClient &&
                SUCCEEDED(m_audioClient->Initialize(
                    AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    500000, 0, &wf, nullptr))) {
                m_audioEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
                if (m_audioEvent)
                    m_audioClient->SetEventHandle((HANDLE)m_audioEvent);
                m_audioClient->GetService(__uuidof(IAudioRenderClient),
                                          (void**)&m_renderClient);
                emit logMessage("WASAPI 初始化成功");
            }
            dev->Release();
        }
        enum_->Release();
    }
    CoUninitialize();

    if (m_renderClient && m_audioEvent) {
        m_audioThread = QThread::create([this] { wasapiPullLoop(); });
        m_audioThread->start();
    } else {
        emit logMessage("错误: WASAPI 初始化失败");
    }
#endif

    emit logMessage(QString("解码器就绪: %1").arg(codec->name));

    // ── Decode loop ──
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int totalWritten = 0;

    while (m_running) {
#ifdef __linux__
        drainToAlsa();
#endif
        if (m_paused) { QThread::msleep(50); continue; }

        int ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF || ret == AVERROR_EXIT) break;
            QThread::msleep(100); av_packet_unref(pkt); continue;
        }

        if (pkt->stream_index != m_audioStreamIdx) { av_packet_unref(pkt); continue; }

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
    avformat_close_input(&m_fmtCtx);

#ifdef __linux__
    if (m_pcm) { snd_pcm_drain(m_pcm); snd_pcm_close(m_pcm); m_pcm = nullptr; }
#endif

    m_running = false;
    emit stopped("eof");
}
