#include "BilibiliApi.h"
#include "utils/Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDateTime>
#include <QNetworkProxy>
#include <QProcess>

static const char *BASE_PASSPORT = "https://passport.bilibili.com";
static const char *BASE_API = "https://api.bilibili.com";
static const char *BASE_LIVE = "https://api.live.bilibili.com";

BilibiliApi::BilibiliApi(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_nam->setProxy(QNetworkProxy::NoProxy);
    m_nam->setCookieJar(nullptr);
}

void BilibiliApi::setCookie(const QString &cookie)
{
    m_cookie = cookie;
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
    QNetworkRequest request{QUrl::fromEncoded(url.toUtf8())};
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

void BilibiliApi::mergeCookies(QNetworkReply *reply)
{
    auto rawHeaders = reply->rawHeaderPairs();
    QStringList newCookies;
    for (const auto &h : rawHeaders) {
        if (QString(h.first).toLower() == "set-cookie") {
            QString cookieLine = QString::fromUtf8(h.second);
            QStringList parts = cookieLine.split(';');
            if (!parts.isEmpty())
                newCookies << parts.first().trimmed();
        }
    }
    if (newCookies.isEmpty()) return;

    // Merge with existing cookie
    for (auto &nc : newCookies) {
        QString name = nc.section('=', 0, 0);
        if (name.isEmpty()) continue;
        int existing = m_cookie.indexOf(name + "=");
        if (existing >= 0) {
            int end = m_cookie.indexOf("; ", existing);
            m_cookie.replace(existing, (end > 0 ? end - existing : m_cookie.size() - existing), nc);
        } else {
            if (!m_cookie.isEmpty()) m_cookie += "; ";
            m_cookie += nc;
        }
    }
    // Re-extract csrf token
    int idx = m_cookie.indexOf("bili_jct=");
    if (idx >= 0) {
        int start = idx + 9;
        int end = m_cookie.indexOf(';', start);
        m_csrfToken = m_cookie.mid(start, end > start ? end - start : -1);
    }
    LOG_INFO("mergeCookies: cookie size now {}", m_cookie.size());

    // Save to settings
    emit cookieUpdated(m_cookie);
}

void BilibiliApi::computeMixinKey(const QString &imgKey, const QString &subKey)
{
    QString raw = imgKey + subKey;
    static const int order[] = {
        46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,27,43,5,49,
        33,9,42,19,29,28,14,39,12,38,41,13,37,48,7,16,
        24,55,40,61,26,17,0,1,60,51,30,4,22,25,54,21,56,59,6,63,57,62,
        11,36,20,34,44,52
    };
    QString salt;
    for (int i : order)
        salt += raw[i];
    m_mixinKey = salt.left(32);
    LOG_INFO("w_rid mixin key computed");
}

QString BilibiliApi::signUrl(const QString &url) const
{
    if (m_mixinKey.isEmpty()) return url;

    QUrl parsed(url);
    QUrlQuery query(parsed);

    qint64 ts = QDateTime::currentSecsSinceEpoch();
    QList<QPair<QString, QString>> params = query.queryItems();
    params.append({"wts", QString("%1%2").arg(ts).arg(m_mixinKey)});
    std::sort(params.begin(), params.end(), [](auto &a, auto &b) { return a.first < b.first; });

    QStringList parts;
    for (auto &p : params)
        parts << QString("%1=%2").arg(p.first, p.second);
    QString wrid = QCryptographicHash::hash(parts.join('&').toUtf8(), QCryptographicHash::Md5).toHex();

    QUrlQuery finalQuery;
    finalQuery.setQuery(query.toString());
    finalQuery.addQueryItem("w_rid", wrid);
    finalQuery.addQueryItem("wts", QString::number(ts));

    QUrl result(parsed);
    result.setQuery(finalQuery);
    return result.toString(QUrl::FullyEncoded);
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
        QString confirmUrl = data["url"].toString();

        if (dataCode == 0 && !confirmUrl.isEmpty()) {
            // DDTV-style: extract cookies from the confirm URL query string
            QUrl confirmQUrl(confirmUrl);
            QUrlQuery confirmQuery(confirmQUrl);
            QStringList cookieParts;
            for (const auto &p : confirmQuery.queryItems()) {
                // Skip non-cookie params like "gourl"
                if (p.first == "gourl") continue;
                cookieParts << QString("%1=%2").arg(p.first, p.second);
            }
            QString cookieStr = cookieParts.join("; ");
            setCookie(cookieStr);
            LOG_INFO("Login success from URL params, cookie size={}", cookieStr.size());
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

        // Extract wbi_img keys for w_rid signing
        auto wbi = data["wbi_img"].toObject();
        QString imgUrl = wbi["img_url"].toString();
        QString subUrl = wbi["sub_url"].toString();
        if (!imgUrl.isEmpty() && !subUrl.isEmpty()) {
            QRegularExpression re("([a-z0-9]+)(?=\\.png)");
            auto imgMatch = re.match(imgUrl);
            auto subMatch = re.match(subUrl);
            if (imgMatch.hasMatch() && subMatch.hasMatch()) {
                computeMixinKey(imgMatch.captured(1), subMatch.captured(1));
            }
        }

        // Merge any Set-Cookie headers from nav response
        mergeCookies(reply);
    });
}

// === Live Followed (dynamic portal) ===

void BilibiliApi::fetchLiveFollowed()
{
    QString url = signUrl(QString("%1/x/polymer/web-dynamic/v1/portal?up_list_more=1&web_location=0.0")
                             .arg(BASE_API));
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("fetchLiveFollowed", reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto items = doc.object()["data"].toObject()["live_users"].toObject()["items"].toArray();
        QVector<LiveRoom> rooms;
        for (const auto &v : items) {
            auto o = v.toObject();
            LiveRoom room;
            room.roomId = o["room_id"].toString().toLongLong();
            room.uid = o["mid"].toVariant().toLongLong();
            room.username = o["uname"].toString();
            room.title = o["title"].toString();
            room.coverUrl = o["face"].toString();
            room.isLive = true;
            room.viewerCount = 0;
            rooms << room;
        }
        LOG_INFO("fetchLiveFollowed: {} live users", rooms.size());
        emit liveFollowedReady(rooms);
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
                      .arg(BASE_LIVE).arg(cid);
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
    QString rawUrl = QString("%1/xlive/web-room/v1/index/getDanmuInfo?id=%2&type=0&web_location=444.8")
                      .arg(BASE_LIVE).arg(roomId);
    QString url = signUrl(rawUrl);
    LOG_INFO("getDanmuInfo URL: {}", url.toStdString());

    // Use dedicated QNetworkAccessManager to avoid shared state issues
    auto *nam = new QNetworkAccessManager(this);
    nam->setProxy(QNetworkProxy::NoProxy);
    nam->setCookieJar(nullptr);
    QNetworkRequest request{QUrl::fromEncoded(url.toUtf8())};
    request.setRawHeader("Cookie", m_cookie.toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    request.setRawHeader("Referer", "https://live.bilibili.com/");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, nam, reply, roomId]() {
        nam->deleteLater();
        reply->deleteLater();
        QByteArray output = reply->readAll();
        auto doc = QJsonDocument::fromJson(output);
        int code = doc.object()["code"].toInt(-1);
        LOG_INFO("getDanmuInfo dedicated NAM code={}", code);
        if (code == 0) {
            auto data = doc.object()["data"].toObject();
            QStringList wsUrls;
            for (const auto &h : data["host_list"].toArray())
                wsUrls << QString("wss://%1/sub").arg(h.toObject()["host"].toString());
            QString token = data["token"].toString();
            emit danmuInfoReady(roomId, wsUrls, token);
        } else {
            emit requestError("getDanmuInfo",
                QString("code=%1 msg=%2").arg(code).arg(doc.object()["message"].toString()));
        }
    });
}

void BilibiliApi::sendLiveDanmaku(qint64 roomId, const QString &text)
{
    if (m_csrfToken.isEmpty()) {
        emit requestError("sendDanmaku", "无 CSRF token，请重新登录");
        return;
    }

    qint64 ts = QDateTime::currentSecsSinceEpoch();
    QByteArray body;
    QUrlQuery params;
    params.addQueryItem("color", "16777215");
    params.addQueryItem("fontsize", "25");
    params.addQueryItem("mode", "1");
    params.addQueryItem("msg", text);
    params.addQueryItem("rnd", QString::number(ts));
    params.addQueryItem("roomid", QString::number(roomId));
    params.addQueryItem("csrf_token", m_csrfToken);
    params.addQueryItem("csrf", m_csrfToken);
    body = params.toString(QUrl::FullyEncoded).toUtf8();

    auto *nam = new QNetworkAccessManager(this);
    nam->setProxy(QNetworkProxy::NoProxy);
    nam->setCookieJar(nullptr);
    QNetworkRequest request{QUrl(QString("%1/msg/send").arg(BASE_LIVE))};
    request.setRawHeader("Cookie", m_cookie.toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    request.setRawHeader("Referer", "https://live.bilibili.com/");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    auto *reply = nam->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, nam, reply] {
        nam->deleteLater();
        reply->deleteLater();
        QByteArray output = reply->readAll();
        auto doc = QJsonDocument::fromJson(output);
        auto obj = doc.object();
        int code = obj["code"].toInt(-1);
        LOG_INFO("sendDanmaku response: code={} msg={}", code, obj["message"].toString().toStdString());
        if (reply->error() != QNetworkReply::NoError)
            emit requestError("sendDanmaku", reply->errorString());
        else if (code != 0)
            emit requestError("sendDanmaku", QString("code=%1 %2").arg(code).arg(obj["message"].toString()));
    });
}

void BilibiliApi::fetchUserFace(qint64 uid)
{
    auto *reply = get(QString("%1/x/space/acc/info?mid=%2").arg(BASE_API).arg(uid));
    connect(reply, &QNetworkReply::finished, this, [this, reply, uid] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto data = doc.object()["data"].toObject();
        QString face = data["face"].toString();
        if (!face.isEmpty())
            emit userFaceReady(uid, face);
    });
}
