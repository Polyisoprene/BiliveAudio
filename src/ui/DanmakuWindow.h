#pragma once
#include <QWidget>
#include <QTextEdit>
#include <QSlider>
#include <QLabel>
#include "models/Danmaku.h"

class DanmakuWindow : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuWindow(QWidget *parent = nullptr);
    void addDanmaku(const Danmaku &dm);
    void setConnected(bool connected);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTextEdit *m_display = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_statusLabel = nullptr;
    int m_maxLines = 200;

    QColor contrastColor(const QColor &bg) const;
};
