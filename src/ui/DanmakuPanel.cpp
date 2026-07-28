#include "DanmakuPanel.h"
#include "DanmakuBubble.h"
#include "utils/Settings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QAbstractItemView>
#include <QPropertyAnimation>
#include <QEasingCurve>

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

    m_list = new QListWidget;
    m_list->setStyleSheet("background-color: #1a1a2e; border: none;");
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setMinimumHeight(150);
    layout->addWidget(m_list, 1);

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(30);
    connect(m_flushTimer, &QTimer::timeout, this, &DanmakuPanel::flushBuffer);

    auto *sb = m_list->verticalScrollBar();
    connect(sb, &QScrollBar::valueChanged, this, [this, sb](int) {
        if (m_programmaticScroll) return;
        bool atBottom = sb->value() >= sb->maximum() - 5;
        m_scrollLocked = !atBottom;
        if (!m_scrollLocked && !m_buffer.isEmpty())
            m_flushTimer->start(30);
    });

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
}

void DanmakuPanel::addDanmaku(const Danmaku &dm)
{
    if (m_scrollLocked) {
        m_buffer.enqueue(dm);
        return;
    }
    insertDanmaku(dm);
}

void DanmakuPanel::insertDanmaku(const Danmaku &dm)
{
    m_programmaticScroll = true;
    auto *scroll = m_list->verticalScrollBar();

    while (m_list->count() >= m_maxLines)
        delete m_list->takeItem(0);

    auto *item = new QListWidgetItem(m_list);
    auto *bubble = new DanmakuBubble(dm);
    item->setSizeHint(bubble->sizeHint());
    m_list->setItemWidget(item, bubble);

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

void DanmakuPanel::flushBuffer()
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

void DanmakuPanel::clear()
{
    m_list->clear();
}

void DanmakuPanel::setConnected(bool connected)
{
    m_statusLabel->setText(connected ? "已连接" : "未连接");
    m_statusLabel->setStyleSheet(connected
        ? "color: #4caf50; font-size: 11px;"
        : "color: #888; font-size: 11px;");
    m_sendBtn->setEnabled(connected);
}
