#pragma once
#include <QString>

struct LiveRoom {
    qint64 roomId = 0;
    qint64 uid = 0;
    QString username;
    QString title;
    QString coverUrl;
    QString streamUrl;
    qint64 viewerCount = 0;
    bool isLive = false;
};
