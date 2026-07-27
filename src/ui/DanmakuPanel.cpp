#include "DanmakuPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>

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
}

void DanmakuPanel::addDanmaku(const Danmaku &dm)
{
    QString html = QString("<span style='color:%1'>[%2]</span> "
                           "<b>%3</b>: %4")
                       .arg(dm.color.name(), dm.formattedTime(),
                            dm.username.toHtmlEscaped(),
                            dm.text.toHtmlEscaped());
    m_display->append(html);

    auto *sb = m_display->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void DanmakuPanel::clear()
{
    m_display->clear();
}

void DanmakuPanel::setConnected(bool connected)
{
    m_statusLabel->setText(connected ? "已连接" : "未连接");
    m_statusLabel->setStyleSheet(connected
        ? "color: #4caf50; font-size: 11px;"
        : "color: #888; font-size: 11px;");
    m_sendBtn->setEnabled(connected);
}
