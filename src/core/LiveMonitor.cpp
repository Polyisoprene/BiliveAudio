#include "LiveMonitor.h"
#include "utils/Logger.h"

LiveMonitor::LiveMonitor(BilibiliApi *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_pollTimer = new QTimer(this);

    connect(m_api, &BilibiliApi::followedListReady, this, &LiveMonitor::onFollowedList);
    connect(m_api, &BilibiliApi::liveStatusReady, this, &LiveMonitor::onLiveStatus);
    connect(m_pollTimer, &QTimer::timeout, this, &LiveMonitor::refreshNow);
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
    if (m_uid > 0)
        fetchFollowedList();
}

void LiveMonitor::fetchFollowedList()
{
    m_api->fetchFollowedList(m_uid, 1, 50);
}

void LiveMonitor::onFollowedList(const QList<FollowedUser> &users, bool hasMore)
{
    m_followed = QVector<FollowedUser>(users.begin(), users.end());

    QList<qint64> uids;
    for (auto &u : m_followed)
        uids << u.uid;

    if (!uids.isEmpty())
        m_api->checkLiveStatus(uids);

    if (hasMore) {
        // In a real implementation, fetch more pages
    }
}

void LiveMonitor::onLiveStatus(const QMap<qint64, LiveRoom> &rooms)
{
    m_liveRooms.clear();
    QSet<qint64> currentLiveUids;

    for (auto it = rooms.begin(); it != rooms.end(); ++it) {
        if (it.value().isLive) {
            m_liveRooms << it.value();
            currentLiveUids.insert(it.key());
        }
    }

    // Detect new live sessions
    for (auto uid : currentLiveUids) {
        if (!m_prevLiveUids.contains(uid)) {
            auto room = rooms[uid];
            emit newLiveStarted(room);
            LOG_INFO("New live detected: {} - {}", room.username.toStdString(), room.title.toStdString());
        }
    }

    m_prevLiveUids = currentLiveUids;
    emit liveListUpdated(m_liveRooms);
}
