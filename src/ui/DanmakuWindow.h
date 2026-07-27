#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QEvent>
#include <QQueue>
#include "models/Danmaku.h"

class QPropertyAnimation;

class DanmakuWindow : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuWindow(QWidget *parent = nullptr);
    void addDanmaku(const Danmaku &dm);
    void clear() { m_list->clear(); }
    void setConnected(bool connected);

signals:
    void sendDanmakuRequested(const QString &text);
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void onSendClicked();
    void insertDanmaku(const Danmaku &dm);
    void flushBuffer();

    QListWidget *m_list = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPropertyAnimation *m_scrollAnim = nullptr;
    QTimer *m_flushTimer = nullptr;
    QQueue<Danmaku> m_buffer;
    bool m_programmaticScroll = false;
    bool m_scrollLocked = false;
    QPoint m_dragPos;
    int m_maxLines = 200;

    QColor contrastColor(const QColor &bg) const;
};
