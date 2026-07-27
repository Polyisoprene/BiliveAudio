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
    QString type; // "danmaku", "sc", "gift"
    QString faceUrl;
    QString medalName;
    int medalLevel = 0;
    QColor medalColor;
    qint64 price = 0;
    QString giftName;
    int giftCount = 0;

    QString formattedTime() const {
        return QDateTime::fromSecsSinceEpoch(timestamp).toString("HH:mm:ss");
    }
};
