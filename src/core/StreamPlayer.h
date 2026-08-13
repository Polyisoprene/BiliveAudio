#pragma once
#include <QObject>
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
    static void wakeup(void *ctx);
    // 必须暴露给元对象系统：mpv 的 wakeup 回调通过 QMetaObject::invokeMethod
    // 在主线程事件循环中调用它（普通私有成员函数无法被 invokeMethod 找到，
    // 会导致所有 mpv 事件——END_FILE/属性变化——永远不被处理）
    Q_INVOKABLE void processEvents();

    mpv_handle *m_mpv = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_volume{80};
    bool m_playing = false;
};
