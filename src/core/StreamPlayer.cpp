#include "StreamPlayer.h"
#include "utils/Logger.h"

#ifdef __linux__
#include <alsa/asoundlib.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")
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

#ifdef _WIN32
    if (m_audioThread) {
        m_audioThread->quit();
        m_audioThread->wait(2000);
        m_audioThread = nullptr;
    }
#endif

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

static int interruptCb(void *ctx)
{
    return *static_cast<std::atomic<bool>*>(ctx) ? 0 : 1;
}

// ── Windows: WASAPI event-driven pull ──────────────────────
#ifdef _WIN32
void StreamPlayer::wasapiPullLoop()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator *enum_ = nullptr;
    IMMDevice *dev = nullptr;
    IAudioClient *client = nullptr;
    IAudioRenderClient *render = nullptr;
    HANDLE evt = nullptr;

    auto cleanup = [&] {
        if (client) { client->Release(); client = nullptr; }
        if (render) { render->Release(); render = nullptr; }
        if (dev) { dev->Release(); dev = nullptr; }
        if (enum_) { enum_->Release(); enum_ = nullptr; }
        if (evt) { CloseHandle(evt); evt = nullptr; }
    };

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&enum_);
    if (FAILED(hr) || !enum_) { emit logMessage("WASAPI: MMDeviceEnumerator 失败"); CoUninitialize(); return; }

    hr = enum_->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (FAILED(hr) || !dev) { emit logMessage("WASAPI: 无默认音频设备"); cleanup(); CoUninitialize(); return; }

    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client);
    if (FAILED(hr) || !client) { emit logMessage("WASAPI: IAudioClient 激活失败"); cleanup(); CoUninitialize(); return; }

    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0 };
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                            500000, 0, &wf, nullptr);
    if (FAILED(hr)) { emit logMessage(QString("WASAPI: Initialize 失败 %1").arg(hr)); cleanup(); CoUninitialize(); return; }

    evt = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!evt) { emit logMessage("WASAPI: CreateEvent 失败"); cleanup(); CoUninitialize(); return; }
    client->SetEventHandle(evt);

    hr = client->GetService(__uuidof(IAudioRenderClient), (void**)&render);
    if (FAILED(hr) || !render) { emit logMessage("WASAPI: GetService 失败"); cleanup(); CoUninitialize(); return; }

    UINT32 bufSize;
    client->GetBufferSize(&bufSize);

    emit logMessage("WASAPI 初始化成功");

    // Initial fill
    BYTE *data = nullptr;
    if (SUCCEEDED(render->GetBuffer(bufSize, &data))) {
        int16_t *dst = reinterpret_cast<int16_t *>(data);
        int total = bufSize * 2;
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
        render->ReleaseBuffer(bufSize, 0);
    }

    client->Start();

    while (m_running) {
        DWORD ret = WaitForSingleObject(evt, 100);
        if (ret == WAIT_TIMEOUT) continue;
        if (ret != WAIT_OBJECT_0 || !m_running) break;

        UINT32 padding;
        if (FAILED(client->GetCurrentPadding(&padding))) break;
        UINT32 frames = bufSize - padding;
        if (frames == 0) continue;

        if (SUCCEEDED(render->GetBuffer(frames, &data))) {
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
            render->ReleaseBuffer(frames, 0);
        }
    }

    client->Stop();
    cleanup();
    CoUninitialize();
    emit logMessage("WASAPI 线程结束");
}
#endif

// ── decodeLoop ── all resources are RAII locals
void StreamPlayer::decodeLoop()
{
    emit logMessage("decodeLoop() 线程开始");

    // Helpers for consistent error exit
    auto failExit = [&](const QString &msg) {
        emit logMessage(msg);
        emit stopped("error");
        m_running = false;
    };

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
    if (ret != 0 || !rawFmt) { failExit("错误: avformat_open_input 失败"); return; }
    rawFmt->interrupt_callback.callback = interruptCb;
    rawFmt->interrupt_callback.opaque = &m_running;
    std::unique_ptr<AVFormatContext, AvFmtDeleter> fmtCtx(rawFmt);

    if (avformat_find_stream_info(fmtCtx.get(), nullptr) < 0) { failExit("错误: 无法获取流信息"); return; }

    int audioIdx = av_find_best_stream(fmtCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIdx < 0) { failExit("错误: 未找到音频流"); return; }

    const AVStream *stream = fmtCtx->streams[audioIdx];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) { failExit("错误: 不支持的音频编码"); return; }

    AVCodecContext *rawCodec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(rawCodec, stream->codecpar);
    if (avcodec_open2(rawCodec, codec, nullptr) < 0) {
        avcodec_free_context(&rawCodec);
        failExit("错误: 无法打开解码器"); return;
    }
    std::unique_ptr<AVCodecContext, AvCodecDeleter> codecCtx(rawCodec);

    // ── swresample → 44100 s16 stereo ──
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext *rawSwr = nullptr;
    swr_alloc_set_opts2(&rawSwr,
        &outLayout, AV_SAMPLE_FMT_S16, 44100,
        &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate,
        0, nullptr);
    if (!rawSwr || swr_init(rawSwr) < 0) {
        if (rawSwr) swr_free(&rawSwr);
        failExit("错误: 无法初始化重采样器"); return;
    }
    std::unique_ptr<SwrContext, SwrDeleter> swrCtx(rawSwr);

    // ── Linux: ALSA ──
#ifdef __linux__
    snd_pcm_t *rawPcm = nullptr;
    if (snd_pcm_open(&rawPcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0) {
        failExit("错误: 无法打开 ALSA 设备"); return;
    }
    auto pcmDeleter = [](snd_pcm_t *p) { if (p) { snd_pcm_drain(p); snd_pcm_close(p); } };
    std::unique_ptr<snd_pcm_t, decltype(pcmDeleter)> pcm(rawPcm, pcmDeleter);
    {
        snd_pcm_hw_params_t *hw; snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(pcm.get(), hw);
        snd_pcm_hw_params_set_access(pcm.get(), hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm.get(), hw, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(pcm.get(), hw, 2);
        unsigned rate = 44100; snd_pcm_hw_params_set_rate_near(pcm.get(), hw, &rate, nullptr);
        snd_pcm_hw_params(pcm.get(), hw);
        snd_pcm_sw_params_t *sw; snd_pcm_sw_params_alloca(&sw);
        snd_pcm_sw_params_current(pcm.get(), sw);
        snd_pcm_uframes_t boundary; snd_pcm_sw_params_get_boundary(sw, &boundary);
        snd_pcm_sw_params_set_start_threshold(pcm.get(), sw, 1);
        snd_pcm_sw_params_set_stop_threshold(pcm.get(), sw, boundary);
        snd_pcm_sw_params(pcm.get(), sw);
        snd_pcm_start(pcm.get());
    }
    emit logMessage("ALSA 设备就绪");
#endif

    // ── Windows: start WASAPI thread ──
#ifdef _WIN32
    m_audioThread = QThread::create([this] { wasapiPullLoop(); });
    m_audioThread->start();
    emit logMessage("WASAPI 音频线程已启动");
#endif

    emit logMessage(QString("解码器就绪: %1").arg(codec->name));

    // ── Decode loop ──
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int totalWritten = 0;
    int dropLogged = 0;

#ifdef __linux__
    auto drainAlsa = [&]() {
        if (!pcm) return;
        snd_pcm_state_t state = snd_pcm_state(pcm.get());
        if (state != SND_PCM_STATE_RUNNING && state != SND_PCM_STATE_PREPARED) {
            snd_pcm_prepare(pcm.get()); snd_pcm_start(pcm.get()); return;
        }
        snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm.get());
        if (avail <= 0) return;
        int wp = m_wp.load(std::memory_order_acquire);
        int rp = m_rp.load(std::memory_order_relaxed);
        int bufAvail = (wp - rp) & kBufMask;
        if (bufAvail <= 0) return;
        int frames = std::min(static_cast<int>(avail), bufAvail / 2);
        float vol = m_volume / 100.0f;
        auto *src = reinterpret_cast<const int16_t *>(m_buf);
        int written = 0;
        snd_pcm_sframes_t w;
        if (vol < 1.0f) {
            alignas(16) int16_t tmp[4096];
            int total = frames * 2, pos = 0;
            while (pos < total) {
                int chunk = std::min(total - pos, 4096);
                for (int i = 0; i < chunk; i++)
                    tmp[i] = static_cast<int16_t>(src[(rp + pos + i) & kBufMask] * vol);
                w = snd_pcm_writei(pcm.get(), tmp, chunk / 2);
                if (w < 0) goto alsa_err;
                pos += w * 2;
            }
            written = pos;
        } else {
            w = snd_pcm_writei(pcm.get(), src + rp, frames);
            if (w > 0) written = w * 2; else goto alsa_err;
        }
        m_rp.store((rp + written) & kBufMask, std::memory_order_release);
        return;
    alsa_err:
        if (w == -EAGAIN) return;
        if (w == -EPIPE || w == -EBADFD) {
            snd_pcm_prepare(pcm.get()); snd_pcm_start(pcm.get());
        }
    };
#endif

    while (m_running) {
#ifdef __linux__
        drainAlsa();
#endif
        if (m_paused) { QThread::msleep(50); continue; }

        ret = av_read_frame(fmtCtx.get(), pkt);
        if (ret < 0) {
            if (ret == AVERROR_EXIT) break;
            if (ret == AVERROR_EOF) {
                for (int i = 0; m_running && i < 30; i++) QThread::msleep(100);
                if (!m_running) break;
                av_packet_unref(pkt); continue;
            }
            QThread::msleep(100); av_packet_unref(pkt); continue;
        }

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
                } else if (!dropLogged++) {
                    emit logMessage("警告: ring buffer 满, 丢弃音频数据");
                }
            }
            av_freep(&out[0]);
            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    emit logMessage("decodeLoop() 线程结束");

    // emit stopped only if not manually stopped
    if (m_running.exchange(false))
        emit stopped("eof");
}
