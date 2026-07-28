#include "DanmakuWindow.h"
#include "DanmakuBubble.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QPainter>
#include <QPushButton>
#include <QWindow>
#include <QScrollBar>
#include <QTimer>
#include <QAbstractItemView>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QEasingCurve>

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

    // Header: status + close button
    auto *header = new QHBoxLayout;
    m_statusLabel = new QLabel("弹幕 - 未连接");
    m_statusLabel->setStyleSheet("color: #888; font-size: 12px;");
    header->addWidget(m_statusLabel);
    header->addStretch();

    auto *closeBtn = new QPushButton("×");
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet("background: transparent; color: #888; font-size: 16px; border: none;");
    header->addWidget(closeBtn);
    layout->addLayout(header);

    m_list = new QListWidget;
    m_list->setStyleSheet("background: rgba(26,26,46,220); border: none;");
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(m_list, 1);

    auto *sb = m_list->verticalScrollBar();
    connect(sb, &QScrollBar::valueChanged, this, [this, sb](int) {
        if (m_programmaticScroll) return;
        bool atBottom = sb->value() >= sb->maximum() - 5;
        m_scrollLocked = !atBottom;
        if (!m_scrollLocked && !m_buffer.isEmpty())
            m_flushTimer->start(30);
    });
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(30);
    connect(m_flushTimer, &QTimer::timeout, this, &DanmakuWindow::flushBuffer);

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
            && !qobject_cast<QPushButton *>(obj) && !qobject_cast<QLineEdit *>(obj)) {
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
    if (m_scrollLocked) {
        m_buffer.enqueue(dm);
        return;
    }
    insertDanmaku(dm);
}

void DanmakuWindow::insertDanmaku(const Danmaku &dm)
{
    m_programmaticScroll = true;
    auto *scroll = m_list->verticalScrollBar();

    auto *item = new QListWidgetItem(m_list);
    auto *bubble = new DanmakuBubble(dm);
    item->setSizeHint(bubble->sizeHint());
    m_list->setItemWidget(item, bubble);
    m_list->addItem(item);

    while (m_list->count() > m_maxLines)
        delete m_list->takeItem(0);

    if (m_scrollAnim) {
        m_scrollAnim->setEndValue(scroll->maximum());
        return;
    }
    m_scrollAnim = new QPropertyAnimation(scroll, "value", this);
    m_scrollAnim->setDuration(200);
    m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_scrollAnim->setStartValue(scroll->value());
    m_scrollAnim->setEndValue(scroll->maximum());
    connect(m_scrollAnim, &QPropertyAnimation::finished, this, [this, scroll]() {
        m_programmaticScroll = true;
        scroll->setValue(scroll->maximum());
        m_programmaticScroll = false;
        m_scrollAnim->deleteLater();
        m_scrollAnim = nullptr;
    });
    m_scrollAnim->start();
}

void DanmakuWindow::flushBuffer()
{
    if (m_buffer.isEmpty() || m_scrollLocked) {
        m_flushTimer->stop();
        return;
    }
    Danmaku dm = m_buffer.dequeue();
    insertDanmaku(dm);
    if (m_buffer.isEmpty())
        m_flushTimer->stop();
}

void DanmakuWindow::setConnected(bool connected)
{
    m_statusLabel->setText(connected ? "弹幕 - 已连接" : "弹幕 - 未连接");
}

void DanmakuWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(0x1a, 0x1a, 0x2e));
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
