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
    void sendLiveDanmaku(qint64 roomId, const QString &text);
    void fetchUserFace(qint64 uid);

signals:
    void qrCodeFetched(const QString &url, const QString &key);
    void qrCodePollResult(const QString &status, const QString &cookie, const QString &username);
    void userInfoReady(const UserInfo &info);
    void followedListReady(const QList<FollowedUser> &users, bool hasMore);
    void liveStatusReady(const QMap<qint64, LiveRoom> &rooms);
    void roomInfoReady(qint64 roomId, qint64 uid, const QString &title, qint64 cid);
    void streamUrlReady(qint64 roomId, const QString &url);
    void danmuInfoReady(qint64 roomId, const QStringList &wsUrls, const QString &token);
    void requestError(const QString &context, const QString &error);
    void cookieUpdated(const QString &cookie);
    void userFaceReady(qint64 uid, const QString &url);

private:
    QNetworkAccessManager *m_nam = nullptr;
    QString m_cookie;
    QString m_csrfToken;
    QString m_mixinKey;

    QNetworkReply *get(const QString &url);
    QNetworkReply *post(const QString &url, const QByteArray &body);
    QNetworkReply *postJson(const QString &url, const QJsonObject &json);
    void addCookie(QNetworkRequest &request) const;
    void mergeCookies(QNetworkReply *reply);
    void computeMixinKey(const QString &imgKey, const QString &subKey);
    QString signUrl(const QString &url) const;
};
