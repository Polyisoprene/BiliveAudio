#pragma once
#include <QObject>
#include <QTimer>
#include <QSet>
#include <QVector>
#include "models/LiveRoom.h"
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
    QVector<LiveRoom> m_liveRooms;
    QSet<qint64> m_prevLiveUids;
    bool m_fetching = false;

    void onLiveFollowed(const QVector<LiveRoom> &rooms);
};
