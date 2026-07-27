#pragma once
#include <QString>
#include <QColor>
#include <QDateTime>

struct Danmaku {
    QString text;
    QString username;
    QString uid;
    QColor color;
    qint64 timestamp;
    QString type; // "danmaku", "gift", "sc", "enter", "welcome"

    QString formattedTime() const {
        return QDateTime::fromSecsSinceEpoch(timestamp).toString("HH:mm:ss");
    }
};
