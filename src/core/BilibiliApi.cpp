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
    LOG_INFO("w_rid mixin key computed: {}", m_mixinKey.toStdString());
}

QString BilibiliApi::signUrl(const QString &url) const
{
    if (m_mixinKey.isEmpty()) return url;

    QUrl parsed(url);
    QUrlQuery query(parsed);

    qint64 ts = QDateTime::currentSecsSinceEpoch();
    QList<QPair<QString, QString>> params = query.queryItems();
    params.append({"wts", QString::number(ts)});
    std::sort(params.begin(), params.end(), [](auto &a, auto &b) { return a.first < b.first; });

    QStringList parts;
    for (auto &p : params)
        parts << QString("%1=%2").arg(p.first, p.second);
    QString wrid = QCryptographicHash::hash(
        (parts.join('&') + m_mixinKey).toUtf8(),
        QCryptographicHash::Md5).toHex();

    QUrlQuery finalQuery;
    finalQuery.setQuery(query.toString());
    finalQuery.addQueryItem("w_rid", wrid);
    finalQuery.addQueryItem("wts", QString::number(ts));

    QUrl result(parsed);
    result.setQuery(finalQuery);
    QString signedUrl = result.toString(QUrl::FullyEncoded);
    LOG_DEBUG("signUrl: {}", signedUrl.toStdString());
    return signedUrl;
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

        if (dataCode == 0) {
            if (!confirmUrl.isEmpty() && confirmUrl.contains("ticket=")) {
                // 新版流程：url中不再直接携带Cookie，只有一个一次性ticket，
                // 需要请求crossDomain链接，从重定向链上每跳响应的Set-Cookie头中换取正式Cookie
                // （参考 DDTV/Core/Account/Kernel/ByQRCode.cs）
                LOG_INFO("扫码登录确认，通过ticket换取Cookie: {}", confirmUrl.toStdString());
                exchangeTicket(confirmUrl, 0, {});
            } else if (!confirmUrl.isEmpty() && confirmUrl.contains('?')) {
                // 旧版流程回退：Cookie直接以querystring参数的形式拼接在url中
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
            } else {
                // 解析失败防护：记录日志而不是静默失败
                LOG_WARN("扫码登录确认成功，但url中既无ticket也无querystring，B站返回格式可能已变更: {}",
                         confirmUrl.toStdString());
                emit requestError("pollQRCode", "扫码登录已确认，但无法解析Cookie，B站返回格式可能已变更");
            }
        } else if (dataCode == 86038) {
            emit qrCodePollResult("expired", {}, {});
        } else if (dataCode == 86090) {
            emit qrCodePollResult("scanned", {}, {});
        } else {
            emit qrCodePollResult("waiting", {}, {});
        }
    });
}

// === QR login: ticket → cookie exchange (new flow) ===

void BilibiliApi::exchangeTicket(const QString &crossDomainUrl, int hop, const QStringList &collectedCookies)
{
    if (hop > 8) {
        LOG_WARN("exchangeTicket: 重定向超过8跳，放弃换取Cookie");
        emit requestError("exchangeTicket", "ticket换取Cookie时重定向次数过多");
        return;
    }

    QNetworkRequest request{QUrl::fromEncoded(crossDomainUrl.toUtf8())};
    request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Referer", "https://passport.bilibili.com");
    // 手动跟随重定向：自动跟随只能看到最终响应的头，重定向链上每一跳的Set-Cookie都会丢失
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);

    auto *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, hop, crossDomainUrl, collectedCookies]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_WARN("exchangeTicket: hop={} 请求失败: {}", hop, reply->errorString().toStdString());
            emit requestError("exchangeTicket", reply->errorString());
            return;
        }

        // 收集本跳响应的Set-Cookie（只取name=value部分）
        QStringList cookies = collectedCookies;
        for (const auto &h : reply->rawHeaderPairs()) {
            if (QString(h.first).toLower() == "set-cookie") {
                QString line = QString::fromUtf8(h.second).section(';', 0, 0).trimmed();
                if (!line.section('=', 0, 0).trimmed().isEmpty())
                    cookies << line;
            }
        }

        // 跟随Location继续下一跳
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray location = reply->rawHeader("Location");
        if (status >= 300 && status < 400 && !location.isEmpty()) {
            QUrl next = QUrl::fromEncoded(crossDomainUrl.toUtf8()).resolved(QUrl::fromEncoded(location));
            LOG_INFO("exchangeTicket: hop={} HTTP {} 继续跳转 -> {}", hop, status, next.toString().toStdString());
            exchangeTicket(next.toString(QUrl::FullyEncoded), hop + 1, cookies);
            return;
        }

        // 重定向链结束：按Cookie名去重（保留最后一个）
        // 不按Domain过滤：crossDomain是biligame域名，Set-Cookie的Domain不一定是.bilibili.com，过滤会误丢
        QMap<QString, QString> cookieMap;
        for (const QString &line : cookies)
            cookieMap[line.section('=', 0, 0)] = line.section('=', 1, -1);
        QStringList cookieParts;
        for (auto it = cookieMap.begin(); it != cookieMap.end(); ++it)
            cookieParts << QString("%1=%2").arg(it.key(), it.value());
        QString cookieStr = cookieParts.join("; ");

        if (cookieStr.isEmpty()) {
            LOG_WARN("exchangeTicket: 未从Set-Cookie换取到任何Cookie，B站ticket流程可能已变更");
            emit requestError("exchangeTicket", "ticket换取Cookie失败，未获取到任何Cookie");
            return;
        }
        LOG_INFO("exchangeTicket: 换取到{}个Cookie", cookieMap.size());
        setCookie(cookieStr);
        emit qrCodePollResult("confirmed", cookieStr, {});
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

    // Cancel any in-flight request from previous room
    if (m_danmuInfoReply) {
        m_danmuInfoReply->abort();
        m_danmuInfoReply->deleteLater();
        m_danmuInfoReply = nullptr;
    }

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
    m_danmuInfoReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, nam, reply, roomId]() {
        if (m_danmuInfoReply == reply)
            m_danmuInfoReply = nullptr;
        QByteArray output = reply->readAll();
        reply->deleteLater();
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

static constexpr int kFaceRetryMax = 3;

void BilibiliApi::fetchUserFace(qint64 uid)
{
    doFetchUserFace(uid, 0);
}

void BilibiliApi::doFetchUserFace(qint64 uid, int retryCount)
{
    QString url = QString("%1/x/web-interface/card?mid=%2&photo=true&web_location=0.0").arg(BASE_API).arg(uid);
    auto *nam = new QNetworkAccessManager(this);
    nam->setProxy(QNetworkProxy::NoProxy);
    nam->setCookieJar(nullptr);
    QNetworkRequest request{QUrl::fromEncoded(url.toUtf8())};
    request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Referer", "https://live.bilibili.com/");
    auto *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, uid, retryCount] {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (retryCount < kFaceRetryMax)
                scheduleRetry(uid, retryCount + 1);
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        int code = doc.object()["code"].toInt(-1);
        if (code != 0) {
            if (retryCount < kFaceRetryMax)
                scheduleRetry(uid, retryCount + 1);
            else
                emit userFaceReady(uid, {});
            return;
        }
        QString face = doc.object()["data"].toObject()["card"].toObject()["face"].toString();
        if (face.isEmpty()) {
            emit userFaceReady(uid, {});
        } else {
            emit userFaceReady(uid, face);
        }
    });
}

void BilibiliApi::cancelFaceRetries()
{
    for (auto &entry : m_faceRetries) {
        if (entry.timer) {
            entry.timer->stop();
            delete entry.timer;
        }
    }
    m_faceRetries.clear();
}

void BilibiliApi::scheduleRetry(qint64 uid, int retryCount)
{
    int delay = 1000 * (1 << retryCount);
    auto &entry = m_faceRetries[uid];
    if (entry.timer) {
        entry.timer->stop();
        delete entry.timer;
    }
    entry.retryCount = retryCount;
    entry.timer = new QTimer(this);
    entry.timer->setSingleShot(true);
    connect(entry.timer, &QTimer::timeout, this, [this, uid]() {
        auto it = m_faceRetries.find(uid);
        if (it == m_faceRetries.end()) return;
        int rc = it->retryCount;
        it->timer->deleteLater();
        m_faceRetries.erase(it);
        doFetchUserFace(uid, rc);
    });
    entry.timer->start(delay);
}
