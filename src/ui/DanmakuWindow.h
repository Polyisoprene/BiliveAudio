#pragma once
#include <QWidget>
#include <QTextEdit>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QEvent>
#include "models/Danmaku.h"

class DanmakuWindow : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuWindow(QWidget *parent = nullptr);
    void addDanmaku(const Danmaku &dm);
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

    QTextEdit *m_display = nullptr;
    QLineEdit *m_input = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPoint m_dragPos;
    double m_opacity = 0.85;
    int m_maxLines = 200;

    QColor contrastColor(const QColor &bg) const;
};
