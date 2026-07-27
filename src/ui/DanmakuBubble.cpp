#include "DanmakuBubble.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QResizeEvent>

static QMap<QString, QPixmap> s_avatarCache;

DanmakuBubble::DanmakuBubble(const Danmaku &dm, QWidget *parent)
    : QWidget(parent), m_dm(dm)
{
    m_avatarKey = QString("a://%1").arg(dm.uid);

    if (dm.medalLevel > 0 && !dm.medalName.isEmpty())
        m_medalText = QString("%1 %2").arg(dm.medalName).arg(dm.medalLevel);

    if (dm.type == "sc")
        m_bubbleColor = QColor("#FFB800");
    else if (dm.type == "gift")
        m_bubbleColor = QColor("#FF69B4");
    else
        m_bubbleColor = QColor("#95EC69");

    int w = calcWidth();
    setFixedWidth(w);
    recalcHeight(w);
}

int DanmakuBubble::calcWidth() const
{
    // Fixed width ~floating window - equal margins
    return 340;
}

void DanmakuBubble::recalcHeight(int forcedW)
{
    int w = forcedW > 0 ? forcedW : width();
    if (w < 50 || w > 400) w = calcWidth();

    int margin = 6;
    int ax = margin + 8;
    int as = m_avatarSize;
    int tx = ax + as + 8;
    int availW = qMax(1, w - tx - 10);

    int charsPerLine = qMax(1, availW / 14);
    int lines = 1;
    if (!m_dm.text.isEmpty())
        lines = qMax(1, (m_dm.text.length() + charsPerLine - 1) / charsPerLine);

    int h = 8 + as + 4 + lines * 22 + 22;
    setFixedHeight(h);
}

QSize DanmakuBubble::sizeHint() const
{
    return QSize(width(), height());
}

void DanmakuBubble::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recalcHeight();
}

void DanmakuBubble::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int margin = 6;
    int r = 8;

    QPainterPath bubble;
    bubble.addRoundedRect(margin, 0, w - margin * 2, h - 6, r, r);
    p.fillPath(bubble, m_bubbleColor);

    int ax = margin + 8;
    int ay = 10;
    int as = m_avatarSize;

    // Avatar (cached)
    if (s_avatarCache.contains(m_dm.uid) && !s_avatarCache[m_dm.uid].isNull()) {
        QPixmap av = s_avatarCache[m_dm.uid];
        QPainterPath clip;
        clip.addEllipse(ax, ay, as, as);
        p.setClipPath(clip);
        p.drawPixmap(ax, ay, as, as, av);
        p.setClipping(false);
    } else {
        if (!m_dm.faceUrl.isEmpty() && !s_avatarCache.contains(m_dm.uid)) {
            s_avatarCache[m_dm.uid] = QPixmap();
            auto *nam = new QNetworkAccessManager(this);
            QNetworkRequest req(QUrl(m_dm.faceUrl));
            req.setRawHeader("User-Agent", "Mozilla/5.0");
            req.setRawHeader("Referer", "https://live.bilibili.com/");
            auto *reply = nam->get(req);
            connect(reply, &QNetworkReply::finished, this, [reply, nam, uid = m_dm.uid]() {
                reply->deleteLater();
                nam->deleteLater();
                QPixmap av;
                if (reply->error() == QNetworkReply::NoError)
                    av.loadFromData(reply->readAll());
                if (!av.isNull())
                    s_avatarCache[uid] = av;
            });
        }
        p.setBrush(m_dm.color.isValid() ? m_dm.color : QColor("#888"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(ax, ay, as, as);
    }

    int tx = ax + as + 8;
    int ty = ay;
    int origTx = ax;  // text content starts at avatar column

    // Medal badge
    if (!m_medalText.isEmpty()) {
        QColor mc = m_dm.medalColor.isValid() ? m_dm.medalColor : QColor("#DAA520");
        QFont mf("sans-serif", 9);
        p.setFont(mf);
        QFontMetrics mfm(mf);
        int mw = mfm.horizontalAdvance(m_medalText) + 8;
        p.setBrush(mc);
        p.setPen(Qt::NoPen);
        QRect mr(tx, ty, mw, 14);
        p.drawRoundedRect(mr, 3, 3);
        p.setPen(Qt::white);
        p.drawText(mr, Qt::AlignCenter, m_medalText);
        tx += mw + 4;
    }

    // Username
    p.setPen(QColor("#000000"));
    QFont uf("sans-serif", 11, QFont::Bold);
    p.setFont(uf);
    p.drawText(tx, ty + 12, m_dm.username);
    int unameW = QFontMetrics(uf).horizontalAdvance(m_dm.username) + 4;

    // SC price (drawn AFTER username, not overlapping)
    if (m_dm.type == "sc") {
        tx += unameW;
        p.setPen(QColor("#B8860B"));
        p.setFont(QFont("sans-serif", 10));
        p.drawText(tx, ty + 12, QString(" ¥%1元").arg(m_dm.price / 1000.0, 0, 'f', 1));
    }
    QFont tf("sans-serif", 9);
    p.setFont(tf);
    p.setPen(QColor("#999999"));
    int ttx = w - margin - 8 - QFontMetrics(tf).horizontalAdvance(m_dm.formattedTime());
    if (m_dm.type != "gift")
        p.drawText(ttx, ty + 12, m_dm.formattedTime());

    // SC price
    if (m_dm.type == "sc") {
        p.setPen(QColor("#B8860B"));
        p.setFont(QFont("sans-serif", 10));
        p.drawText(tx, ty + 12, QString(" ¥%1元").arg(m_dm.price / 1000.0, 0, 'f', 1));
    }

    // Message text
    p.setPen(QColor("#333333"));
    QFont mf2("sans-serif", 12);
    p.setFont(mf2);
    QRect textRect(origTx, ay + as + 4, w - origTx - 10, h - ay - as - 16);
    p.drawText(textRect, Qt::AlignLeft | Qt::TextWordWrap, m_dm.text);
}
