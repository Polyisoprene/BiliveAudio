#include "DanmakuManager.h"
#include "utils/Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QRandomGenerator>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QNetworkProxy>
#include <QSsl>
#include <QtEndian>
#include <brotli/decode.h>

// Bilibili WebSocket protocol constants
static const int HEADER_LENGTH = 16;
static const int PROTOCOL_VERSION_NORMAL = 1;
static const int PROTOCOL_VERSION_ZLIB = 2;
static const int PROTOCOL_VERSION_BROTLI = 3;

static const int OPERATION_HEARTBEAT = 2;
static const int OPERATION_HEARTBEAT_REPLY = 3;
static const int OPERATION_AUTH = 7;
static const int OPERATION_AUTH_REPLY = 8;
static const int OPERATION_MESSAGE = 5;

QMap<QString, QString> DanmakuManager::faceCache;

DanmakuManager::DanmakuManager(BilibiliApi *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_heartbeatTimer = new QTimer(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);

    m_buvid = QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QUuid::createUuid().toString(QUuid::WithoutBraces).left(4)
            + "infoc";

    connect(m_reconnectTimer, &QTimer::timeout, this, &DanmakuManager::tryReconnect);
    connect(m_api, &BilibiliApi::danmuInfoReady, this, &DanmakuManager::onDanmuInfoReady);
    connect(m_api, &BilibiliApi::userFaceReady, this, [this](qint64 uid, const QString &url) {
        faceCache[QString::number(uid)] = url;
    });
}

void DanmakuManager::ensureWebSocket()
{
    if (m_ws) return;
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_ws->setProxy(QNetworkProxy::NoProxy);
    QSslConfiguration ssl = m_ws->sslConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_ws->setSslConfiguration(ssl);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(m_ws, &QWebSocket::errorOccurred, this, &DanmakuManager::onError);
#else
    connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &DanmakuManager::onError);
#endif
    connect(m_ws, &QWebSocket::connected, this, &DanmakuManager::onConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &DanmakuManager::onDisconnected);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &DanmakuManager::onBinaryMessage);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &DanmakuManager::sendHeartbeat);
}

DanmakuManager::~DanmakuManager()
{
    disconnectRoom();
}

void DanmakuManager::connectRoom(qint64 roomId)
{
    m_roomId = roomId;
    m_reconnectRetry = 0;
    emit logMessage(QString("弹幕: 正在获取房间 %1 的服务器信息...").arg(roomId));
    m_api->getDanmuInfo(roomId);
}

void DanmakuManager::onDanmuInfoReady(qint64 roomId, const QStringList &wsUrls, const QString &token)
{
    if (roomId != m_roomId) return;

    if (wsUrls.isEmpty()) {
        LOG_ERROR("No danmu hosts available for room {}", roomId);
        emit logMessage(QString("弹幕: 未获取到房间 %1 的服务器地址").arg(roomId));
        return;
    }

    m_token = token;
    int idx = QRandomGenerator::global()->bounded(static_cast<int>(wsUrls.size()));
    QString wsUrl = wsUrls[idx];
    emit logMessage(QString("弹幕: 连接 %1...").arg(wsUrl));

    ensureWebSocket();
    m_ws->open(QUrl(wsUrl));
}

void DanmakuManager::disconnectRoom()
{
    m_heartbeatTimer->stop();
    m_reconnectTimer->stop();
    m_reconnectRetry = 0;
    if (m_ws) m_ws->close();
    m_connected = false;
    m_roomId = 0;
}

void DanmakuManager::onConnected()
{
    emit logMessage("弹幕: WebSocket 已连接，发送认证包...");

    QJsonObject auth;
    auth["uid"] = m_uid;
    auth["roomid"] = m_roomId;
    auth["protover"] = PROTOCOL_VERSION_BROTLI;
    auth["buvid"] = m_buvid;
    auth["platform"] = "web";
    auth["type"] = 2;
    if (!m_token.isEmpty())
        auth["key"] = m_token;
    QByteArray authBody = QJsonDocument(auth).toJson(QJsonDocument::Compact);
    emit logMessage(QString("弹幕auth JSON(%1B): %2").arg(authBody.size()).arg(QString::fromUtf8(authBody.left(80))));

    int totalLen = HEADER_LENGTH + authBody.size();
    QByteArray packet(totalLen, Qt::Uninitialized);
    qint32 netLen = qToBigEndian<qint32>(totalLen);
    memcpy(packet.data(), &netLen, 4);
    qint16 headerLen = qToBigEndian<qint16>(HEADER_LENGTH);
    memcpy(packet.data() + 4, &headerLen, 2);
    qint16 protoVer = qToBigEndian<qint16>(PROTOCOL_VERSION_NORMAL);
    memcpy(packet.data() + 6, &protoVer, 2);
    qint32 netOp = qToBigEndian<qint32>(OPERATION_AUTH);
    memcpy(packet.data() + 8, &netOp, 4);
    qint32 seq = qToBigEndian<qint32>(1);
    memcpy(packet.data() + 12, &seq, 4);
    if (!authBody.isEmpty())
        memcpy(packet.data() + HEADER_LENGTH, authBody.constData(), authBody.size());

    emit logMessage(QString("弹幕包(%1B): %2").arg(packet.size()).arg(packet.toHex().left(40)));
    m_ws->sendBinaryMessage(packet);
}

void DanmakuManager::onDisconnected()
{
    m_connected = false;
    m_heartbeatTimer->stop();
    emit disconnected();
    emit logMessage("弹幕: WebSocket 已断开");

    if (m_roomId > 0 && m_reconnectRetry < 3) {
        emit logMessage(QString("弹幕: %1 秒后尝试重连...").arg(m_reconnectRetry + 1));
        m_reconnectTimer->start((m_reconnectRetry + 1) * 2000);
    }
}

void DanmakuManager::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString err = m_ws->errorString();
    LOG_ERROR("Danmu WebSocket error: {}", err.toStdString());
    emit logMessage(QString("弹幕错误: %1").arg(err));
}

void DanmakuManager::tryReconnect()
{
    if (m_roomId <= 0) return;
    m_reconnectRetry++;
    emit logMessage(QString("弹幕: 重连第 %1 次...").arg(m_reconnectRetry));
    m_api->getDanmuInfo(m_roomId);
}

void DanmakuManager::sendPacket(int operation, const QByteArray &body, int headerProtoVer)
{
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) return;

    int totalLen = HEADER_LENGTH + body.size();
    QByteArray packet(totalLen, Qt::Uninitialized);

    qint32 netLen = qToBigEndian<qint32>(totalLen);
    memcpy(packet.data(), &netLen, 4);

    qint16 headerLen = qToBigEndian<qint16>(HEADER_LENGTH);
    memcpy(packet.data() + 4, &headerLen, 2);

    qint16 protoVer = qToBigEndian<qint16>(headerProtoVer);
    memcpy(packet.data() + 6, &protoVer, 2);

    qint32 netOp = qToBigEndian<qint32>(operation);
    memcpy(packet.data() + 8, &netOp, 4);

    qint32 seq = qToBigEndian<qint32>(1);
    memcpy(packet.data() + 12, &seq, 4);

    if (!body.isEmpty())
        memcpy(packet.data() + HEADER_LENGTH, body.constData(), body.size());

    m_ws->sendBinaryMessage(packet);
}

void DanmakuManager::onBinaryMessage(const QByteArray &message)
{
    parsePackets(message);
}

void DanmakuManager::parsePackets(const QByteArray &data)
{
    if (data.size() < HEADER_LENGTH) return;

    int offset = 0;
    while (offset + HEADER_LENGTH <= data.size()) {
        qint32 totalLen = qFromBigEndian<qint32>(data.mid(offset, 4).constData());
        if (totalLen <= 0) break;

        qint16 headerLen = qFromBigEndian<qint16>(data.mid(offset + 4, 2).constData());
        qint16 protoVer = qFromBigEndian<qint16>(data.mid(offset + 6, 2).constData());
        qint32 operation = qFromBigEndian<qint32>(data.mid(offset + 8, 4).constData());
        // qint32 sequence = qFromBigEndian<qint32>(data.mid(offset + 12, 4).constData());

        QByteArray body;
        if (headerLen + offset <= data.size()) {
            body = data.mid(offset + headerLen, totalLen - headerLen);

            // Decompress if needed
            if (protoVer == PROTOCOL_VERSION_ZLIB)
                body = decompressZlib(body);
            else if (protoVer == PROTOCOL_VERSION_BROTLI)
                body = decompressBrotli(body);

            // For zlib/brotli, body may contain multiple concatenated packets
            if (protoVer == PROTOCOL_VERSION_ZLIB || protoVer == PROTOCOL_VERSION_BROTLI) {
                if (body.size() > HEADER_LENGTH) {
                    parsePackets(body);
                    offset += totalLen;
                    continue;
                }
            }
        }

        switch (operation) {
        case OPERATION_AUTH_REPLY:
            m_connected = true;
            m_reconnectRetry = 0;
            m_heartbeatTimer->start(10000);
            emit connected();
            emit logMessage("弹幕: 认证成功，开始接收");
            break;

        case OPERATION_HEARTBEAT_REPLY:
            if (body.size() >= 4) {
                qint32 viewers;
                memcpy(&viewers, body.constData(), sizeof(viewers));
                viewers = qFromBigEndian<qint32>(viewers);
                emit viewerCountChanged(viewers);
            }
            break;

        case OPERATION_MESSAGE: {
            auto doc = QJsonDocument::fromJson(body);
            if (doc.isObject()) {
                auto obj = doc.object();
                QString cmd = obj["cmd"].toString();
                if (cmd == "DANMU_MSG") {
                    handleDanmuMsg(obj["info"].toArray());
                } else if (cmd == "SUPER_CHAT_MESSAGE") {
                    handleSuperChat(obj["data"].toObject());
                } else if (cmd == "SEND_GIFT") {
                    handleGift(obj["data"].toObject());
                }
            }
            break;
        }

        default:
            break;
        }

        offset += totalLen;
    }
}

void DanmakuManager::handleDanmuMsg(const QJsonArray &info)
{
    if (info.size() < 3) return;

    auto userInfo = info[2].toArray();
    Danmaku dm;
    dm.text = info[1].toString();
    dm.username = userInfo[1].toString();
    dm.uid = QString::number(userInfo[0].toVariant().toLongLong());
    dm.color = QColor(info[0].toArray()[3].toInt());
    dm.timestamp = QDateTime::currentSecsSinceEpoch();
    dm.type = "danmaku";

    // Avatar URL from info[2][7]
    if (userInfo.size() > 7)
        dm.faceUrl = userInfo[7].toString();

    // Fetch face via API if not in WebSocket data
    if (dm.faceUrl.isEmpty() && !dm.uid.isEmpty()) {
        if (faceCache.contains(dm.uid))
            dm.faceUrl = faceCache[dm.uid];
        else
            m_api->fetchUserFace(dm.uid.toLongLong());
    }

    // Fans medal from info[3] — can be an object or array
    if (info.size() > 3) {
        if (info[3].isObject()) {
            auto medal = info[3].toObject();
            dm.medalName = medal["medal_name"].toString();
            dm.medalLevel = medal["medal_level"].toInt();
            dm.medalColor = QColor::fromRgb(medal["medal_color"].toInt(12632256));
        } else if (info[3].isArray()) {
            auto arr = info[3].toArray();
            if (arr.size() >= 2) {
                dm.medalLevel = arr[0].toInt();
                dm.medalName = arr[1].toString();
                if (arr.size() >= 5)
                    dm.medalColor = QColor::fromRgb(arr[4].toInt(12632256));
            }
        }
    }

    emit danmakuReceived(dm);
}

void DanmakuManager::handleSuperChat(const QJsonObject &data)
{
    Danmaku dm;
    dm.text = data["message"].toString();
    dm.username = data["user_info"].toObject()["uname"].toString();
    dm.uid = QString::number(data["uid"].toVariant().toLongLong());
    dm.price = static_cast<qint64>(data["price"].toDouble() * 1000);
    dm.color = QColor(data["background_price_color"].toString());
    dm.faceUrl = data["user_info"].toObject()["face"].toString();
    dm.timestamp = data["start_time"].toVariant().toLongLong();
    dm.type = "sc";
    emit danmakuReceived(dm);
}

void DanmakuManager::handleGift(const QJsonObject &data)
{
    Danmaku dm;
    dm.giftName = data["giftName"].toString();
    dm.giftCount = data["num"].toInt();
    dm.username = data["uname"].toString();
    dm.uid = QString::number(data["uid"].toVariant().toLongLong());
    dm.faceUrl = data["face"].toString();
    dm.timestamp = QDateTime::currentSecsSinceEpoch();
    dm.type = "gift";
    dm.text = QString("送出 %1 x%2").arg(dm.giftName).arg(dm.giftCount);
    emit danmakuReceived(dm);
}

void DanmakuManager::sendHeartbeat()
{
    sendPacket(OPERATION_HEARTBEAT, "[object Object]");
}

QByteArray DanmakuManager::decompressZlib(const QByteArray &data)
{
    return qUncompress(data);
}

QByteArray DanmakuManager::decompressBrotli(const QByteArray &data)
{
    BrotliDecoderState *state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state) return {};

    size_t availableIn = data.size();
    const uint8_t *nextIn = reinterpret_cast<const uint8_t *>(data.constData());

    QByteArray result;
    result.resize(data.size() * 4);
    size_t availableOut = result.size();
    uint8_t *nextOut = reinterpret_cast<uint8_t *>(result.data());

    BrotliDecoderResult res = BrotliDecoderDecompressStream(
        state, &availableIn, &nextIn, &availableOut, &nextOut, nullptr);

    while (res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
        size_t offset = nextOut - reinterpret_cast<uint8_t *>(result.data());
        result.resize(result.size() * 2);
        nextOut = reinterpret_cast<uint8_t *>(result.data()) + offset;
        availableOut = result.size() - offset;
        res = BrotliDecoderDecompressStream(
            state, &availableIn, &nextIn, &availableOut, &nextOut, nullptr);
    }

    BrotliDecoderDestroyInstance(state);

    if (res == BROTLI_DECODER_RESULT_SUCCESS) {
        result.resize(result.size() - availableOut);
        return result;
    }
    return {};
}

void DanmakuManager::sendDanmaku(const QString &text)
{
    if (!m_connected || m_roomId == 0) {
        emit sendError("未连接到弹幕服务器");
        return;
    }

    if (m_uid == 0) {
        emit sendError("需要登录才能发送弹幕");
        return;
    }

    m_api->sendLiveDanmaku(m_roomId, text);
}

void DanmakuManager::setUid(qint64 uid)
{
    m_uid = uid;
}
