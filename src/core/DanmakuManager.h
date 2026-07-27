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
    bool isConnected() const { return m_connected; }

signals:
    void danmakuReceived(const Danmaku &dm);
    void viewerCountChanged(int count);
    void connected();
    void disconnected();
    void sendError(const QString &msg);

private:
    BilibiliApi *m_api = nullptr;
    QWebSocket *m_ws = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    qint64 m_roomId = 0;
    QString m_token;
    QString m_host;
    bool m_connected = false;

    void onDanmuInfoReady(qint64 roomId, const QStringList &hosts, const QString &token);
    void onConnected();
    void onDisconnected();
    void onBinaryMessage(const QByteArray &message);
    void onError(QAbstractSocket::SocketError error);

    void sendHeartbeat();
    void sendPacket(int operation, const QByteArray &body = {});
    QByteArray decompressZlib(const QByteArray &data);
    QByteArray decompressBrotli(const QByteArray &data);
    void parsePackets(const QByteArray &data);
    void handleDanmuMsg(const QJsonArray &info);
};
