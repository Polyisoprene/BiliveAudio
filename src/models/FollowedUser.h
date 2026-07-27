#pragma once
#include <QString>

struct FollowedUser {
    qint64 uid = 0;
    QString username;
    QString avatarUrl;
    qint64 liveRoomId = 0;
    bool isLive = false;
};
