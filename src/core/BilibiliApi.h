#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QMap>
#include <QTimer>
#include "models/UserInfo.h"
#include "models/LiveRoom.h"

class BilibiliApi : public QObject {
    Q_OBJECT
public:
    explicit BilibiliApi(QObject *parent = nullptr);

    void setCookie(const QString &cookie);
    QString cookie() const;

    void fetchQRCode();
    void pollQRCode(const QString &qrcodeKey);
    void getUserInfo();
    void fetchLiveFollowed();
    void getRoomInfo(qint64 roomId);
    void getStreamUrl(qint64 roomId, qint64 cid);
    void getDanmuInfo(qint64 roomId);
    void sendLiveDanmaku(qint64 roomId, const QString &text);
    void fetchUserFace(qint64 uid);
    void cancelFaceRetries();
    int faceRetriesSize() const { return m_faceRetries.size(); }
    bool hasPendingDanmuInfo() const { return m_danmuInfoReply != nullptr; }

signals:
    void qrCodeFetched(const QString &url, const QString &key);
    void qrCodePollResult(const QString &status, const QString &cookie, const QString &username);
    void userInfoReady(const UserInfo &info);
    void liveFollowedReady(const QVector<LiveRoom> &rooms);
    void roomInfoReady(qint64 roomId, qint64 uid, const QString &title, qint64 cid);
    void streamUrlReady(qint64 roomId, const QString &url);
    void danmuInfoReady(qint64 roomId, const QStringList &wsUrls, const QString &token);
    void requestError(const QString &context, const QString &error);
    void cookieUpdated(const QString &cookie);
    void userFaceReady(qint64 uid, const QString &url);

private:
    struct FaceRetry {
        int retryCount = 0;
        QTimer *timer = nullptr;
    };
    QMap<qint64, FaceRetry> m_faceRetries;
    void doFetchUserFace(qint64 uid, int retryCount);
    void scheduleRetry(qint64 uid, int retryCount);

    QNetworkReply *m_danmuInfoReply = nullptr;
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
    // 新版扫码登录：poll确认后url只携带一次性ticket，
    // 需请求crossDomain链接，从重定向链每跳的Set-Cookie头换取正式Cookie
    void exchangeTicket(const QString &crossDomainUrl, int hop, const QStringList &collectedCookies);
};
