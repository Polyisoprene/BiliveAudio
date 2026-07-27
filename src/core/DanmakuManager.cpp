#include "DanmakuManager.h"
#include "utils/Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtEndian>
#include <brotli/decode.h>

// Bilibili WebSocket protocol constants
static const int HEADER_LENGTH = 16;
static const int PROTOCOL_VERSION_NORMAL = 0;
static const int PROTOCOL_VERSION_ZLIB = 2;
static const int PROTOCOL_VERSION_BROTLI = 3;

static const int OPERATION_HEARTBEAT = 2;
static const int OPERATION_HEARTBEAT_REPLY = 3;
static const int OPERATION_AUTH = 7;
static const int OPERATION_AUTH_REPLY = 8;
static const int OPERATION_MESSAGE = 5;

DanmakuManager::DanmakuManager(BilibiliApi *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_heartbeatTimer = new QTimer(this);

    connect(m_ws, &QWebSocket::connected, this, &DanmakuManager::onConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &DanmakuManager::onDisconnected);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &DanmakuManager::onBinaryMessage);
    connect(m_ws, &QWebSocket::errorOccurred, this, &DanmakuManager::onError);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &DanmakuManager::sendHeartbeat);

    connect(m_api, &BilibiliApi::danmuInfoReady, this, &DanmakuManager::onDanmuInfoReady);
}

DanmakuManager::~DanmakuManager()
{
    disconnectRoom();
}

void DanmakuManager::connectRoom(qint64 roomId)
{
    m_roomId = roomId;
    m_api->getDanmuInfo(roomId);
    LOG_INFO("Connecting to danmu room: {}", roomId);
}

void DanmakuManager::onDanmuInfoReady(qint64 roomId, const QStringList &hosts, const QString &token)
{
    if (roomId != m_roomId) return;

    if (hosts.isEmpty()) {
        LOG_ERROR("No danmu hosts available for room {}", roomId);
        return;
    }

    m_token = token;
    m_host = hosts.first();
    QString wsUrl = QString("wss://%1/sub").arg(m_host);
    LOG_INFO("Connecting to danmu WS: {}", wsUrl.toStdString());
    m_ws->open(QUrl(wsUrl));
}

void DanmakuManager::disconnectRoom()
{
    m_heartbeatTimer->stop();
    m_ws->close();
    m_connected = false;
    m_roomId = 0;
    LOG_INFO("Disconnected from danmu room");
}

void DanmakuManager::onConnected()
{
    LOG_INFO("Danmu WebSocket connected");

    // Send auth packet
    QJsonObject auth;
    auth["uid"] = 0;
    auth["roomid"] = m_roomId;
    auth["protover"] = 2;
    auth["platform"] = "web";
    auth["type"] = 2;
    if (!m_token.isEmpty())
        auth["token"] = m_token;
    sendPacket(OPERATION_AUTH, QJsonDocument(auth).toJson(QJsonDocument::Compact));
}

void DanmakuManager::onDisconnected()
{
    m_connected = false;
    m_heartbeatTimer->stop();
    emit disconnected();
    LOG_INFO("Danmu WebSocket disconnected");
}

void DanmakuManager::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    LOG_ERROR("Danmu WebSocket error: {}", m_ws->errorString().toStdString());
}

void DanmakuManager::sendPacket(int operation, const QByteArray &body)
{
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) return;

    int totalLen = HEADER_LENGTH + body.size();
    QByteArray packet(totalLen, Qt::Uninitialized);

    qint32 netLen = qToBigEndian<qint32>(totalLen);
    memcpy(packet.data(), &netLen, 4);

    qint16 headerLen = qToBigEndian<qint16>(HEADER_LENGTH);
    memcpy(packet.data() + 4, &headerLen, 2);

    qint16 protoVer = qToBigEndian<qint16>(PROTOCOL_VERSION_NORMAL);
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
            m_heartbeatTimer->start(30000);
            emit connected();
            LOG_INFO("Danmu auth successful");
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
                    auto info = obj["info"].toArray();
                    handleDanmuMsg(info);
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
    // info[1] = text
    // info[2][0] = uid, info[2][1] = username
    // info[0][3] = color
    if (info.size() < 3) return;

    Danmaku dm;
    dm.text = info[1].toString();
    dm.username = info[2].toArray()[1].toString();
    dm.uid = QString::number(info[2].toArray()[0].toVariant().toLongLong());
    auto colorVal = info[0].toArray()[3].toInt();
    dm.color = QColor(colorVal);
    dm.timestamp = QDateTime::currentSecsSinceEpoch();
    dm.type = "danmaku";
    emit danmakuReceived(dm);
}

void DanmakuManager::sendHeartbeat()
{
    sendPacket(OPERATION_HEARTBEAT);
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

    // TODO: implement send via BilibiliApi
    LOG_INFO("Would send danmaku: {}", text.toStdString());
}
