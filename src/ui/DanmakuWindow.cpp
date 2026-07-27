#include "DanmakuWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QPainter>
#include <QPushButton>
#include <QWindow>

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
    QColor displayColor = dm.color;
    if (dm.color.isValid() && dm.color != QColor(0, 0, 0)) {
        int h, s, l, a;
        displayColor.getHsl(&h, &s, &l, &a);
        if (l < 160) {
            if (h < 0) h = 0;
            if (s < 50) s = 100;
            displayColor.setHsl(h, s, 200, a);
        }
    } else {
        displayColor = QColor("#ffffff");
    }

    QString html = QString("<span style='color:%1'>[%2]</span> "
                           "<b>%3</b>: %4")
                       .arg(displayColor.name(), dm.formattedTime(),
                            dm.username.toHtmlEscaped(),
                            dm.text.toHtmlEscaped());
    m_display->append(html);

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
    painter.setBrush(QColor(0x1a, 0x1a, 0x2e, static_cast<int>(240 * m_opacity)));
    painter.setPen(QPen(QColor("#0f3460"), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
    QWidget::paintEvent(event);
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
