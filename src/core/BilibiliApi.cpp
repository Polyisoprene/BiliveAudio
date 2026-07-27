#include "BilibiliApi.h"
#include "utils/Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QNetworkCookie>

static const char *BASE_PASSPORT = "https://passport.bilibili.com";
static const char *BASE_API = "https://api.bilibili.com";
static const char *BASE_LIVE = "https://api.live.bilibili.com";

BilibiliApi::BilibiliApi(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

void BilibiliApi::setCookie(const QString &cookie)
{
    m_cookie = cookie;
    // Extract csrf from bili_jct
    int idx = m_cookie.indexOf("bili_jct=");
    if (idx >= 0) {
        int start = idx + 9;
        int end = m_cookie.indexOf(';', start);
        m_csrfToken = m_cookie.mid(start, end > start ? end - start : -1);
    }
}

QString BilibiliApi::cookie() const { return m_cookie; }

void BilibiliApi::addCookie(QNetworkRequest &request) const
{
    if (!m_cookie.isEmpty())
        request.setRawHeader("Cookie", m_cookie.toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Referer", "https://live.bilibili.com/");
}

QNetworkReply *BilibiliApi::get(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    addCookie(request);
    return m_nam->get(request);
}

QNetworkReply *BilibiliApi::post(const QString &url, const QByteArray &body)
{
    QNetworkRequest request{QUrl(url)};
    addCookie(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    return m_nam->post(request, body);
}

QNetworkReply *BilibiliApi::postJson(const QString &url, const QJsonObject &json)
{
    QNetworkRequest request{QUrl(url)};
    addCookie(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return m_nam->post(request, QJsonDocument(json).toJson());
}

// === QR Code Login ===

void BilibiliApi::fetchQRCode()
{
    QString url = QString("%1/x/passport-login/web/qrcode/generate").arg(BASE_PASSPORT);
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("fetchQRCode", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto obj = doc.object()["data"].toObject();
        QString url = obj["url"].toString();
        QString key = obj["qrcode_key"].toString();

        LOG_INFO("QR code fetched, key={}, url={}", key.toStdString(), url.toStdString());
        emit qrCodeFetched(url, key);
    });
}

void BilibiliApi::pollQRCode(const QString &qrcodeKey)
{
    QString url = QString("%1/x/passport-login/web/qrcode/poll?qrcode_key=%2")
                      .arg(BASE_PASSPORT, qrcodeKey);
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("pollQRCode", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto data = doc.object()["data"].toObject();
        int dataCode = data["code"].toInt();
        QString url = data["url"].toString();

        if (dataCode == 0 && !url.isEmpty()) {
            // Login confirmed - extract cookies from raw headers
            QStringList cookies;
            auto rawHeaders = reply->rawHeaderPairs();
            for (const auto &h : rawHeaders) {
                if (QString(h.first).toLower() == "set-cookie") {
                    QString cookieLine = QString::fromUtf8(h.second);
                    QStringList parts = cookieLine.split(';');
                    if (!parts.isEmpty())
                        cookies << parts.first().trimmed();
                }
            }
            QString cookieStr = cookies.join("; ");
            setCookie(cookieStr);
            LOG_INFO("Login success, cookie length={}", cookieStr.size());
            emit qrCodePollResult("confirmed", cookieStr, {});
        } else if (dataCode == 86038) {
            emit qrCodePollResult("expired", {}, {});
        } else if (dataCode == 86091) {
            emit qrCodePollResult("scanned", {}, {});
        } else {
            emit qrCodePollResult("waiting", {}, {});
        }
    });
}

// === User Info ===

void BilibiliApi::getUserInfo()
{
    auto *reply = get(QString("%1/x/web-interface/nav").arg(BASE_API));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("getUserInfo", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto data = doc.object()["data"].toObject();
        UserInfo info;
        info.isLoggedIn = data["isLogin"].toBool();
        info.uid = data["mid"].toVariant().toLongLong();
        info.username = data["uname"].toString();
        info.avatarUrl = data["face"].toString();
        emit userInfoReady(info);
    });
}

// === Followed List ===

void BilibiliApi::fetchFollowedList(qint64 uid, int page, int pageSize)
{
    QString url = QString("%1/x/relation/followings?vmid=%2&pn=%3&ps=%4")
                      .arg(BASE_API).arg(uid).arg(page).arg(pageSize);
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("fetchFollowedList", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto data = doc.object()["data"].toObject();
        QList<FollowedUser> users;
        for (const auto &v : data["list"].toArray()) {
            auto o = v.toObject();
            FollowedUser u;
            u.uid = o["mid"].toVariant().toLongLong();
            u.username = o["uname"].toString();
            u.avatarUrl = o["face"].toString();
            users << u;
        }
        bool hasMore = data["has_more"].toBool();
        emit followedListReady(users, hasMore);
    });
}

// === Live Status ===

void BilibiliApi::checkLiveStatus(const QList<qint64> &uids)
{
    QJsonArray arr;
    for (auto uid : uids)
        arr.append(uid);
    QJsonObject body;
    body["uids"] = arr;
    auto *reply = postJson(
        QString("%1/room/v1/Room/get_status_info_by_uids").arg(BASE_LIVE), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("checkLiveStatus", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto data = doc.object()["data"].toObject();
        QMap<qint64, LiveRoom> rooms;
        for (auto it = data.begin(); it != data.end(); ++it) {
            auto o = it.value().toObject();
            LiveRoom room;
            room.roomId = o["room_id"].toVariant().toLongLong();
            room.uid = o["uid"].toVariant().toLongLong();
            room.username = o["uname"].toString();
            room.title = o["title"].toString();
            room.coverUrl = o["cover"].toString();
            room.viewerCount = o["live_status"].toInt() == 1 ? o["online"].toVariant().toLongLong() : 0;
            room.isLive = o["live_status"].toInt() == 1;
            rooms[room.uid] = room;
        }
        emit liveStatusReady(rooms);
    });
}

// === Room Info ===

void BilibiliApi::getRoomInfo(qint64 roomId)
{
    QString url = QString("%1/room/v1/Room/room_init?id=%2").arg(BASE_LIVE).arg(roomId);
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, roomId] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("getRoomInfo", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto data = doc.object()["data"].toObject();
        qint64 uid = data["uid"].toVariant().toLongLong();
        qint64 cid = data["room_id"].toVariant().toLongLong();
        emit roomInfoReady(roomId, uid, QString(), cid);
    });
}

// === Stream URL ===

void BilibiliApi::getStreamUrl(qint64 roomId, qint64 cid)
{
    QString url = QString("%1/room/v1/Room/playUrl?cid=%2&platform=web&qn=0")
                      .arg(BASE_LIVE).arg(roomId);
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, roomId] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("getStreamUrl", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto data = doc.object()["data"].toObject();
        auto durl = data["durl"].toArray();
        QString url;
        if (!durl.isEmpty())
            url = durl.first().toObject()["url"].toString();
        emit streamUrlReady(roomId, url);
    });
}

// === Danmu Info (WebSocket) ===

void BilibiliApi::getDanmuInfo(qint64 roomId)
{
    QString url = QString("%1/xlive/web-room/v1/index/getDanmuInfo?id=%2&type=0")
                      .arg(BASE_LIVE).arg(roomId);
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, roomId] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("getDanmuInfo", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto data = doc.object()["data"].toObject();
        QStringList hosts;
        for (const auto &h : data["host_list"].toArray())
            hosts << h.toObject()["host"].toString();
        QString token = data["token"].toString();
        emit danmuInfoReady(roomId, hosts, token);
    });
}
