#include "LiveMonitor.h"
#include "utils/Logger.h"

LiveMonitor::LiveMonitor(BilibiliApi *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_pollTimer = new QTimer(this);

    connect(m_api, &BilibiliApi::liveFollowedReady, this, &LiveMonitor::onLiveFollowed);
    connect(m_pollTimer, &QTimer::timeout, this, &LiveMonitor::refreshNow);
    // fetchLiveFollowed 失败时只发 requestError 而不触发 liveFollowedReady，
    // 若不在此复位 m_fetching，轮询会永久停摆（开播监控失效直到重启）
    connect(m_api, &BilibiliApi::requestError, this, [this](const QString &ctx, const QString &) {
        if (ctx == "fetchLiveFollowed")
            m_fetching = false;
    });
}

void LiveMonitor::start(int intervalSec)
{
    m_pollTimer->start(intervalSec * 1000);
    refreshNow();
    LOG_INFO("LiveMonitor started, interval={}s", intervalSec);
}

void LiveMonitor::stop()
{
    m_pollTimer->stop();
}

void LiveMonitor::setUid(qint64 uid)
{
    m_uid = uid;
}

void LiveMonitor::refreshNow()
{
    if (m_fetching) return;
    m_fetching = true;
    m_api->fetchLiveFollowed();
}

void LiveMonitor::onLiveFollowed(const QVector<LiveRoom> &rooms)
{
    m_fetching = false;
    m_liveRooms = rooms;

    QSet<qint64> currentLiveUids;
    for (auto &r : rooms)
        currentLiveUids.insert(r.uid);

    // Detect new live sessions
    for (auto uid : currentLiveUids) {
        if (!m_prevLiveUids.contains(uid)) {
            auto it = std::find_if(rooms.begin(), rooms.end(), [uid](auto &r) { return r.uid == uid; });
            if (it != rooms.end()) {
                emit newLiveStarted(*it);
                LOG_INFO("New live detected: {} - {}", it->username.toStdString(), it->title.toStdString());
            }
        }
    }

    m_prevLiveUids = currentLiveUids;
    emit liveListUpdated(m_liveRooms);
}
