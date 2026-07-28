#pragma once
#include <QWidget>
#include <QPixmap>
#include <QCache>
#include "models/Danmaku.h"

class DanmakuBubble : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuBubble(const Danmaku &dm, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void recalcHeight(int forcedW = -1);
    int calcWidth() const;
    static QString cacheDir();
    static QString avatarPath(const QString &uid, const QString &faceUrl);
    static QPixmap loadFromDisk(const QString &path);
    static void saveToDisk(const QString &path, const QPixmap &pix);

    Danmaku m_dm;
    QString m_avatarKey;
    QString m_medalText;
    QColor m_bubbleColor;
    int m_avatarSize = 24;
};
