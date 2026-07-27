#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QMap>
#include "models/UserInfo.h"
#include "models/LiveRoom.h"
#include "models/FollowedUser.h"

class BilibiliApi : public QObject {
    Q_OBJECT
public:
    explicit BilibiliApi(QObject *parent = nullptr);

    void setCookie(const QString &cookie);
    QString cookie() const;

    void fetchQRCode();
    void pollQRCode(const QString &qrcodeKey);
    void getUserInfo();
    void fetchFollowedList(qint64 uid, int page = 1, int pageSize = 50);
    void checkLiveStatus(const QList<qint64> &uids);
    void getRoomInfo(qint64 roomId);
    void getStreamUrl(qint64 roomId, qint64 cid);
    void getDanmuInfo(qint64 roomId);

signals:
    void qrCodeFetched(const QString &url, const QString &key);
    void qrCodePollResult(const QString &status, const QString &cookie, const QString &username);
    void userInfoReady(const UserInfo &info);
    void followedListReady(const QList<FollowedUser> &users, bool hasMore);
    void liveStatusReady(const QMap<qint64, LiveRoom> &rooms);
    void roomInfoReady(qint64 roomId, qint64 uid, const QString &title, qint64 cid);
    void streamUrlReady(qint64 roomId, const QString &url);
    void danmuInfoReady(qint64 roomId, const QStringList &hosts, const QString &token);
    void requestError(const QString &context, const QString &error);

private:
    QNetworkAccessManager *m_nam = nullptr;
    QString m_cookie;
    QString m_csrfToken;

    QNetworkReply *get(const QString &url);
    QNetworkReply *post(const QString &url, const QByteArray &body);
    QNetworkReply *postJson(const QString &url, const QJsonObject &json);
    void addCookie(QNetworkRequest &request) const;
};
