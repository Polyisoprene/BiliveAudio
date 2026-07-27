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
    if (m_fetching) return;
    m_fetching = true;
    m_followed.clear();
    m_followedPage = 1;
    m_api->fetchFollowedList(m_uid, m_followedPage, 50);
}

void LiveMonitor::onFollowedList(const QList<FollowedUser> &users, bool hasMore)
{
    if (!m_fetching) return;
    m_followed.append(QVector<FollowedUser>(users.begin(), users.end()));
    LOG_INFO("LiveMonitor: page {} got {} users, total {}, hasMore={}",
             m_followedPage, users.size(), m_followed.size(), hasMore);

    if (hasMore) {
        m_followedPage++;
        m_api->fetchFollowedList(m_uid, m_followedPage, 50);
        return;
    }

    QList<qint64> uids;
    for (auto &u : m_followed)
        uids << u.uid;

    if (!uids.isEmpty())
        m_api->checkLiveStatus(uids);
}

void LiveMonitor::onLiveStatus(const QMap<qint64, LiveRoom> &rooms)
{
    m_fetching = false;
    int liveCount = 0;
    for (auto it = rooms.begin(); it != rooms.end(); ++it)
        if (it.value().isLive) liveCount++;
    LOG_INFO("LiveMonitor: status for {} users, {} live", rooms.size(), liveCount);
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
