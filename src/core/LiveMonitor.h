#pragma once
#include <QObject>
#include <QTimer>
#include <QSet>
#include <QMap>
#include <QVector>
#include "models/LiveRoom.h"
#include "models/FollowedUser.h"
#include "BilibiliApi.h"

class LiveMonitor : public QObject {
    Q_OBJECT
public:
    explicit LiveMonitor(BilibiliApi *api, QObject *parent = nullptr);

    void start(int intervalSec = 30);
    void stop();
    void refreshNow();
    void setUid(qint64 uid);

    const QVector<LiveRoom> &liveRooms() const { return m_liveRooms; }

signals:
    void liveListUpdated(const QVector<LiveRoom> &rooms);
    void newLiveStarted(const LiveRoom &room);

private:
    BilibiliApi *m_api = nullptr;
    QTimer *m_pollTimer = nullptr;
    qint64 m_uid = 0;
    QVector<FollowedUser> m_followed;
    QVector<LiveRoom> m_liveRooms;
    QSet<qint64> m_prevLiveUids;
    int m_followedPage = 1;
    bool m_fetching = false;

    void fetchFollowedList();
    void onFollowedList(const QList<FollowedUser> &users, bool hasMore);
    void onLiveStatus(const QMap<qint64, LiveRoom> &rooms);
};
