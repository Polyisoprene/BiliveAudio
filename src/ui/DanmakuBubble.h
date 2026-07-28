#pragma once
#include <QWidget>
#include <QPixmap>
#include "models/Danmaku.h"

class DanmakuBubble : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuBubble(const Danmaku &dm, QWidget *parent = nullptr);
    ~DanmakuBubble() override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void recalcHeight(int forcedW = -1);
    int calcWidth() const;
    void startDownload();

    Danmaku m_dm;
    QString m_medalText;
    QColor m_bubbleColor;
    int m_avatarSize = 24;
    bool m_downloading = false;
};
