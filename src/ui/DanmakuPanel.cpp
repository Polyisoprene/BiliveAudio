#include "DanmakuPanel.h"
#include "utils/Settings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QEventLoop>
#include <QPainter>
#include <QTextDocument>
#include <QNetworkReply>

DanmakuPanel::DanmakuPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel("弹幕");
    title->setStyleSheet("font-weight: bold;");
    m_statusLabel = new QLabel("未连接");
    m_statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    header->addWidget(title);
    header->addWidget(m_statusLabel);
    header->addStretch();
    layout->addLayout(header);

    m_display = new QTextEdit;
    m_display->setReadOnly(true);
    m_display->setStyleSheet("background-color: #1a1a2e;");
    m_display->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_display->setMinimumHeight(150);
    layout->addWidget(m_display, 1);

    auto *inputBar = new QHBoxLayout;
    m_input = new QLineEdit;
    m_input->setPlaceholderText("输入弹幕...");
    m_sendBtn = new QPushButton("发送");
    m_sendBtn->setEnabled(false);
    inputBar->addWidget(m_input);
    inputBar->addWidget(m_sendBtn);
    layout->addLayout(inputBar);

    connect(m_sendBtn, &QPushButton::clicked, this, [this] {
        QString text = m_input->text().trimmed();
        if (!text.isEmpty()) {
            emit sendDanmakuRequested(text);
            m_input->clear();
        }
    });

    connect(m_input, &QLineEdit::returnPressed, m_sendBtn, &QPushButton::click);

    m_display->document()->setMaximumBlockCount(m_maxLines);

    m_avatarNam = new QNetworkAccessManager(this);
}

void DanmakuPanel::addDanmaku(const Danmaku &dm)
{
    bool imageMode = Settings::instance().danmakuImageMode();

    if (imageMode) {
        QColor dc = dm.color;
        if (!dc.isValid() || dc == QColor(0, 0, 0)) dc = QColor("#ffffff");

        QString avatarSrc = ensureAvatar(dm.uid, dm.faceUrl);
        QString avatarHtml = avatarSrc.isEmpty()
            ? QString("<span style='color:%1; font-size:18px;'>●</span> ").arg(dc.name())
            : QString("<img src='%1' width='22' height='22'> ").arg(avatarSrc);

        QString medalBadge;
        if (dm.medalLevel > 0 && !dm.medalName.isEmpty()) {
            QColor mc = dm.medalColor.isValid() ? dm.medalColor : QColor("#DAA520");
            medalBadge = QString("<span style='background:%1; color:white; font-size:9px; padding:0 4px; margin-right:4px;'>%2 %3</span>")
                .arg(mc.name(), dm.medalName, QString::number(dm.medalLevel));
        }

        QString bubbleHtml;
        if (dm.type == "sc") {
            bubbleHtml = QString(
                "<table style='margin:4px 0;'><tr><td bgcolor='#FFB800' style='padding:6px 10px;'>"
                "%1%2<b style='color:black;'>%3</b>"
                "<span style='color:#B8860B; font-size:11px;'>&nbsp;¥%4元</span><br>"
                "<span style='color:#333;'>%5</span></td></tr></table>"
            ).arg(avatarHtml, medalBadge, dm.username.toHtmlEscaped(),
                  QString::number(dm.price / 1000.0, 'f', 1), dm.text.toHtmlEscaped());
        } else if (dm.type == "gift") {
            bubbleHtml = QString(
                "<table style='margin:4px 0;'><tr><td bgcolor='#FF69B4' style='padding:6px 10px;'>"
                "%1%2<b style='color:white;'>%3</b>"
                "<span style='color:#ccc; font-size:10px;'>&nbsp;%4</span><br>"
                "<span style='color:#fff;'>%5</span></td></tr></table>"
            ).arg(avatarHtml, medalBadge, dm.username.toHtmlEscaped(),
                  dm.formattedTime(), dm.text.toHtmlEscaped());
        } else {
            bubbleHtml = QString(
                "<table style='margin:4px 0;'><tr><td bgcolor='#95EC69' style='padding:6px 10px;'>"
                "%1%2<b style='color:black;'>%3</b>"
                "<span style='color:#999; font-size:10px;'>&nbsp;%4</span><br>"
                "<span style='color:#333;'>%5</span></td></tr></table>"
            ).arg(avatarHtml, medalBadge, dm.username.toHtmlEscaped(),
                  dm.formattedTime(), dm.text.toHtmlEscaped());
        }
        m_display->append(bubbleHtml);
    } else {
        // Text mode — simple HTML
        QColor dc = dm.color;
        if (dc.isValid() && dc != QColor(0, 0, 0)) {
            int h, s, l, a;
            dc.getHsl(&h, &s, &l, &a);
            if (l < 160) { if (h < 0) h = 0; if (s < 50) s = 100; dc.setHsl(h, s, 200, a); }
        } else { dc = QColor("#ffffff"); }

        QString medalBadge;
        if (dm.medalLevel > 0 && !dm.medalName.isEmpty()) {
            QColor mc = dm.medalColor.isValid() ? dm.medalColor : QColor("#DAA520");
            medalBadge = QString(
                "<span style='background:%1; color:white; font-size:10px; border-radius:3px; padding:0 4px; margin-right:4px;'>%2 %3</span>"
            ).arg(mc.name(), dm.medalName, QString::number(dm.medalLevel));
        }

        QString html;
        if (dm.type == "sc") {
            html = QString("<div style='background:%1; border-radius:8px; padding:8px; margin-bottom:4px;'>"
                "<span style='color:%2; font-size:16px;'>●</span> <b style='color:%2;'>%3</b>"
                "<span style='color:#FFD700; font-size:12px;'> ¥%4元</span><br><span style='color:#fff;'>%5</span>"
                "</div>").arg(dm.color.name(), dm.color.name(), dm.username.toHtmlEscaped(),
                QString::number(dm.price / 1000.0, 'f', 1), dm.text.toHtmlEscaped());
        } else if (dm.type == "gift") {
            html = QString("<div style='margin-bottom:2px;'><span style='color:#FF69B4; font-size:16px;'>●</span> "
                "<b style='color:#FF69B4;'>%1</b><span style='color:#666; font-size:10px;'>&nbsp;%2</span>"
                "<br><span style='color:#e0e0e0; display:inline-block; padding-left:20px;'>%3</span></div>")
                .arg(dm.username.toHtmlEscaped(), dm.formattedTime(), dm.text.toHtmlEscaped());
        } else {
            html = QString("<div style='margin-bottom:2px;'><span style='color:%1; font-size:16px;'>●</span> %2"
                "<b style='color:%1;'>%3</b><span style='color:#666; font-size:10px;'>&nbsp;%4</span><br>"
                "<span style='color:#e0e0e0; display:inline-block; padding-left:20px;'>%5</span></div>")
                .arg(dc.name(), medalBadge, dm.username.toHtmlEscaped(), dm.formattedTime(), dm.text.toHtmlEscaped());
        }
        m_display->append(html);
    }

    auto *sb = m_display->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void DanmakuPanel::clear()
{
    m_display->clear();
}

QString DanmakuPanel::ensureAvatar(const QString &uid, const QString &url)
{
    if (uid.isEmpty() || url.isEmpty()) return {};
    QString key = QString("a://%1").arg(uid);
    if (m_avatarPixmaps.contains(uid)) return key;

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "Mozilla/5.0");
    req.setRawHeader("Referer", "https://live.bilibili.com/");
    auto *reply = m_avatarNam->get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QPixmap pix;
    if (reply->error() == QNetworkReply::NoError)
        pix.loadFromData(reply->readAll());
    reply->deleteLater();

    if (!pix.isNull()) {
        m_avatarPixmaps[uid] = pix;
        m_display->document()->addResource(QTextDocument::ImageResource, QUrl(key), pix);
        return key;
    }
    return {};
}

void DanmakuPanel::setConnected(bool connected)
{
    m_statusLabel->setText(connected ? "已连接" : "未连接");
    m_statusLabel->setStyleSheet(connected
        ? "color: #4caf50; font-size: 11px;"
        : "color: #888; font-size: 11px;");
    m_sendBtn->setEnabled(connected);
}
