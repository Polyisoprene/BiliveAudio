#include "DanmakuWindow.h"
#include "utils/Settings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QPainter>
#include <QPushButton>
#include <QWindow>
#include <QEventLoop>
#include <QTextDocument>
#include <QPainter>
#include <QNetworkReply>

DanmakuWindow::DanmakuWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setWindowTitle("弹幕");
    resize(360, 530);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Header: status + opacity + close button
    auto *header = new QHBoxLayout;
    m_statusLabel = new QLabel("弹幕 - 未连接");
    m_statusLabel->setStyleSheet("color: #888; font-size: 12px;");
    header->addWidget(m_statusLabel);
    header->addStretch();

    auto *opacityLabel = new QLabel("透明度:");
    opacityLabel->setStyleSheet("color: #888; font-size: 12px;");
    header->addWidget(opacityLabel);

    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(10, 100);
    m_opacitySlider->setValue(85);
    m_opacitySlider->setFixedWidth(80);
    header->addWidget(m_opacitySlider);

    auto *closeBtn = new QPushButton("×");
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet("background: transparent; color: #888; font-size: 16px; border: none;");
    header->addWidget(closeBtn);
    layout->addLayout(header);

    m_display = new QTextEdit;
    m_display->setReadOnly(true);
    m_display->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_display->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_display->setFrameShape(QFrame::NoFrame);
    m_display->document()->setMaximumBlockCount(m_maxLines);
    m_display->setStyleSheet("background: transparent; border: none; font-size: 14px;");
    layout->addWidget(m_display);

    // Input bar
    auto *inputBar = new QHBoxLayout;
    m_input = new QLineEdit;
    m_input->setPlaceholderText("输入弹幕...");
    m_input->setStyleSheet("background: #16213e; color: #e0e0e0; border: 1px solid #0f3460; border-radius: 4px; padding: 4px 8px;");
    inputBar->addWidget(m_input);

    auto *sendBtn = new QPushButton("发送");
    sendBtn->setFixedWidth(50);
    inputBar->addWidget(sendBtn);
    layout->addLayout(inputBar);

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int val) {
        m_opacity = val / 100.0;
        update();
    });

    m_avatarNam = new QNetworkAccessManager(this);

    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(sendBtn, &QPushButton::clicked, this, &DanmakuWindow::onSendClicked);
    connect(m_input, &QLineEdit::returnPressed, this, &DanmakuWindow::onSendClicked);

    // Install event filter on all children for drag support
    for (auto *child : findChildren<QWidget *>())
        child->installEventFilter(this);
}

bool DanmakuWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton
            && !qobject_cast<QSlider *>(obj) && !qobject_cast<QPushButton *>(obj)
            && !qobject_cast<QLineEdit *>(obj)) {
            m_dragPos = me->globalPosition().toPoint();
            if (windowHandle())
                windowHandle()->startSystemMove();
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->buttons() & Qt::LeftButton && !windowHandle()
            && !qobject_cast<QSlider *>(obj) && !qobject_cast<QPushButton *>(obj)
            && !qobject_cast<QLineEdit *>(obj)) {
            move(me->globalPosition().toPoint() - m_dragPos);
        }
    }
    return QWidget::eventFilter(obj, event);
}

void DanmakuWindow::onSendClicked()
{
    QString text = m_input->text().trimmed();
    if (!text.isEmpty()) {
        emit sendDanmakuRequested(text);
        m_input->clear();
    }
}

void DanmakuWindow::addDanmaku(const Danmaku &dm)
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
                "<span style='color:#FFD700; font-size:12px;'> ¥%4元</span><br><span style='color:#fff;'>%5</span></div>")
                .arg(dm.color.name(), dm.color.name(), dm.username.toHtmlEscaped(),
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

void DanmakuWindow::setConnected(bool connected)
{
    m_statusLabel->setText(connected ? "弹幕 - 已连接" : "弹幕 - 未连接");
}

void DanmakuWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(0x1a, 0x1a, 0x2e, static_cast<int>(255 * m_opacity)));
    painter.setPen(QPen(QColor("#0f3460"), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
    QWidget::paintEvent(event);
}

QString DanmakuWindow::ensureAvatar(const QString &uid, const QString &url)
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

void DanmakuWindow::mousePressEvent(QMouseEvent *event)
{
    m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
}

void DanmakuWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
        move(event->globalPosition().toPoint() - m_dragPos);
}

void DanmakuWindow::closeEvent(QCloseEvent *event)
{
    emit closed();
    event->accept();
}

QColor DanmakuWindow::contrastColor(const QColor &bg) const
{
    int lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
    return lum > 128 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}
