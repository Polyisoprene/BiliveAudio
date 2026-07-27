#pragma once
#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVector>
#include <QMap>
#include <QPixmap>
#include <QNetworkAccessManager>
#include "models/Danmaku.h"

class DanmakuPanel : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuPanel(QWidget *parent = nullptr);

    void addDanmaku(const Danmaku &dm);
    void clear();
    void setConnected(bool connected);

signals:
    void sendDanmakuRequested(const QString &text);

private:
    QString ensureAvatar(const QString &uid, const QString &url);

    QTextEdit *m_display = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QNetworkAccessManager *m_avatarNam = nullptr;
    QMap<QString, QPixmap> m_avatarPixmaps;
    int m_maxLines = 200;
};
