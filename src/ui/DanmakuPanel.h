#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QQueue>
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
    void insertDanmaku(const Danmaku &dm);
    void flushBuffer();

    QListWidget *m_list = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer *m_flushTimer = nullptr;
    QQueue<Danmaku> m_buffer;
    bool m_programmaticScroll = false;
    bool m_scrollLocked = false;
    int m_maxLines = 20;
};
