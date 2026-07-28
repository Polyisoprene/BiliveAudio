#include "DanmakuBubble.h"
#include "core/DanmakuManager.h"
#include "utils/Settings.h"
#include <QPainter>
#include <QDateTime>
#include <QPainterPath>
#include <QFontMetrics>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QImageReader>
#include <QBuffer>
#include <QResizeEvent>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QPointer>
#include <QTimer>
#include <QStandardPaths>

static const int kMaxCached = 64;
static QMap<QString, QPixmap> s_memCache;
static QStringList s_cacheOrder;

static void touchCache(const QString &uid)
{
    s_cacheOrder.removeAll(uid);
    s_cacheOrder.append(uid);
    while (s_cacheOrder.size() > kMaxCached) {
        QString old = s_cacheOrder.takeFirst();
        s_memCache.remove(old);
    }
}

static constexpr int kCachePixmapSize = 48;

static QString cacheDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/cache/avatars";
    QDir().mkpath(dir);
    return dir;
}

static QString avatarPath(const QString &uid, const QString &faceUrl)
{
    QString key = QCryptographicHash::hash(
        (uid + "|" + faceUrl).toUtf8(), QCryptographicHash::Md5).toHex();
    return cacheDir() + "/" + key + ".png";
}

static QPixmap loadFromDisk(const QString &path)
{
    QPixmap p;
    p.load(path);
    return p;
}

static void saveToDisk(const QString &path, const QPixmap &pix)
{
    pix.save(path, "PNG");
}

static QPixmap scaledForCache(const QPixmap &src)
{
    if (src.isNull()) return src;
    return src.scaled(kCachePixmapSize, kCachePixmapSize,
                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

DanmakuBubble::DanmakuBubble(const Danmaku &dm, QWidget *parent)
    : QWidget(parent), m_dm(dm)
{
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

    // Deferred download: don't block the constructor or paintEvent
    if (!m_dm.faceUrl.isEmpty() && !s_memCache.contains(m_dm.uid))
        QTimer::singleShot(0, this, &DanmakuBubble::startDownload);
}

DanmakuBubble::~DanmakuBubble() = default;

static QNetworkAccessManager *s_avatarNam()
{
    static auto *nam = new QNetworkAccessManager;
    static qint64 created = QDateTime::currentSecsSinceEpoch();
    qint64 now = QDateTime::currentSecsSinceEpoch();
    // Recreate every 10 minutes to clear accumulated connections and internal buffers
    if (now - created > 600) {
        created = now;
        auto *old = nam;
        QTimer::singleShot(0, [old]() {
            old->deleteLater();
        });
        nam = new QNetworkAccessManager;
    }
    nam->setProxy(QNetworkProxy::NoProxy);
    return nam;
}

void DanmakuBubble::startDownload()
{
    if (m_downloading) return;
    m_downloading = true;

    auto *nam = s_avatarNam();
    QNetworkRequest req{QUrl(m_dm.faceUrl)};
    req.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    req.setRawHeader("Referer", "https://live.bilibili.com/");
    req.setTransferTimeout(15000);
    auto *reply = nam->get(req);
    QPointer<DanmakuBubble> self(this);
    connect(reply, &QNetworkReply::finished, this, [reply, uid = m_dm.uid, origUrl = m_dm.faceUrl, self]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QByteArray data = reply->readAll();

        QPixmap av;
        av.loadFromData(data);
        if (av.isNull()) {
            QBuffer buf(&data);
            QImageReader reader(&buf);
            QImage img = reader.read();
            if (!img.isNull())
                av = QPixmap::fromImage(img);
        }

        if (av.isNull() && origUrl.endsWith(".webp", Qt::CaseInsensitive)) {
            QString jpgUrl = origUrl;
            jpgUrl.replace(jpgUrl.lastIndexOf('.'), 5, ".jpg");
            auto *nam2 = new QNetworkAccessManager(self ? self->parent() : nullptr);
            QNetworkRequest req2{QUrl(jpgUrl)};
            req2.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
            req2.setRawHeader("Referer", "https://live.bilibili.com/");
            auto *reply2 = nam2->get(req2);
            connect(reply2, &QNetworkReply::finished, self.data(), [reply2, nam2, uid, self]() {
                reply2->deleteLater();
                nam2->deleteLater();
                if (reply2->error() != QNetworkReply::NoError) return;
                QPixmap av2;
                av2.loadFromData(reply2->readAll());
                if (av2.isNull()) return;
                av2 = scaledForCache(av2);
                s_memCache[uid] = av2;
                touchCache(uid);
                if (!self.isNull())
                    self->update();
            });
            return;
        }

        if (av.isNull()) return;
        av = scaledForCache(av);
        s_memCache[uid] = av;
        touchCache(uid);
        if (!self.isNull())
            self->update();
    });
}

int DanmakuBubble::calcWidth() const
{
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

    int w = width(), h = height(), margin = 6, r = 8;
    QPainterPath bubble;
    bubble.addRoundedRect(margin, 0, w - margin * 2, h - 6, r, r);
    p.fillPath(bubble, m_bubbleColor);

    int ax = margin + 8, ay = 10, as = m_avatarSize;

    bool imageMode = Settings::instance().danmakuImageMode();

    QPixmap avatar;
    bool haveAvatar = false;

    if (imageMode) {
        if (!haveAvatar && m_dm.faceUrl.isEmpty() && !m_dm.uid.isEmpty()) {
            QString cached = DanmakuManager::lookupFaceUrl(m_dm.uid);
            if (!cached.isEmpty())
                const_cast<DanmakuBubble *>(this)->m_dm.faceUrl = cached;
        }
        if (!haveAvatar && !m_dm.faceUrl.isEmpty()) {
            QString path = avatarPath(m_dm.uid, m_dm.faceUrl);
            avatar = loadFromDisk(path);
            if (!avatar.isNull()) {
                s_memCache[m_dm.uid] = scaledForCache(avatar);
                touchCache(m_dm.uid);
                haveAvatar = true;
            }
        }
        if (s_memCache.contains(m_dm.uid) && !s_memCache[m_dm.uid].isNull()) {
            avatar = s_memCache[m_dm.uid];
            haveAvatar = true;
            touchCache(m_dm.uid);
        }
    }

    if (haveAvatar) {
        QPainterPath clip;
        clip.addEllipse(ax, ay, as, as);
        p.setClipPath(clip);
        p.drawPixmap(ax, ay, as, as, avatar);
        p.setClipping(false);
    } else {
        p.setBrush(m_dm.color.isValid() ? m_dm.color : QColor("#888"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(ax, ay, as, as);
    }

    int tx = ax + as + 8, ty = ay, origTx = ax;

    if (!m_medalText.isEmpty()) {
        QColor mc = m_dm.medalColor.isValid() ? m_dm.medalColor : QColor("#DAA520");
        QFont mf("sans-serif", 9);
        p.setFont(mf);
        int mw = QFontMetrics(mf).horizontalAdvance(m_medalText) + 8;
        p.setBrush(mc);
        p.setPen(Qt::NoPen);
        QRect mr(tx, ty, mw, 14);
        p.drawRoundedRect(mr, 3, 3);
        p.setPen(Qt::white);
        p.drawText(mr, Qt::AlignCenter, m_medalText);
        tx += mw + 4;
    }

    p.setPen(QColor("#000000"));
    QFont uf("sans-serif", 11, QFont::Bold);
    p.setFont(uf);
    p.drawText(tx, ty + 12, m_dm.username);
    int unameW = QFontMetrics(uf).horizontalAdvance(m_dm.username) + 4;

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

    if (m_dm.type == "sc") {
        p.setPen(QColor("#B8860B"));
        p.setFont(QFont("sans-serif", 10));
        p.drawText(tx, ty + 12, QString(" ¥%1元").arg(m_dm.price / 1000.0, 0, 'f', 1));
    }

    p.setPen(QColor("#333333"));
    QFont mf2("sans-serif", 12);
    p.setFont(mf2);
    QRect textRect(origTx, ay + as + 4, w - origTx - 10, h - ay - as - 16);
    p.drawText(textRect, Qt::AlignLeft | Qt::TextWordWrap, m_dm.text);
}
