#pragma once
#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QMap>
#include "models/Danmaku.h"
#include "BilibiliApi.h"

class DanmakuManager : public QObject {
    Q_OBJECT
public:
    explicit DanmakuManager(BilibiliApi *api, QObject *parent = nullptr);
    ~DanmakuManager() override;

    void connectRoom(qint64 roomId);
    void disconnectRoom();
    void sendDanmaku(const QString &text);
    void setUid(qint64 uid);
    bool isConnected() const { return m_connected; }

signals:
    void danmakuReceived(const Danmaku &dm);
    void viewerCountChanged(int count);
    void connected();
    void disconnected();
    void sendError(const QString &msg);
    void logMessage(const QString &msg);

private:
    BilibiliApi *m_api = nullptr;
    QWebSocket *m_ws = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    qint64 m_roomId = 0;
    QString m_token;
    QString m_host;
    QString m_buvid;
    qint64 m_uid = 0;
    bool m_connected = false;
    int m_reconnectRetry = 0;

    void onDanmuInfoReady(qint64 roomId, const QStringList &wsUrls, const QString &token);
    void onConnected();
    void onDisconnected();
    void onBinaryMessage(const QByteArray &message);
    void onError(QAbstractSocket::SocketError error);
    void tryReconnect();
    void ensureWebSocket();

    void sendHeartbeat();
    void sendPacket(int operation, const QByteArray &body = {}, int headerProtoVer = 1);
    QByteArray decompressZlib(const QByteArray &data);
    QByteArray decompressBrotli(const QByteArray &data);
    void parsePackets(const QByteArray &data);
    void handleDanmuMsg(const QJsonArray &info);
};
