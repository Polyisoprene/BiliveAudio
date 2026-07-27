#include "DanmakuWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QPainter>

DanmakuWindow::DanmakuWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setWindowTitle("弹幕");
    resize(360, 500);
    setAttribute(Qt::WA_TranslucentBackground, false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

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
    layout->addLayout(header);

    m_display = new QTextEdit;
    m_display->setReadOnly(true);
    m_display->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_display->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_display->setFrameShape(QFrame::NoFrame);
    m_display->document()->setMaximumBlockCount(m_maxLines);
    m_display->setStyleSheet("background: transparent; border: none; font-size: 14px;");
    layout->addWidget(m_display);

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int val) {
        setWindowOpacity(val / 100.0);
        m_display->setStyleSheet(
            QString("background: transparent; border: none; font-size: 14px;"));
    });

    setWindowOpacity(m_opacitySlider->value() / 100.0);
}

void DanmakuWindow::addDanmaku(const Danmaku &dm)
{
    QColor displayColor = dm.color;
    if (dm.color.isValid()) {
        // Compute contrast against background (#1a1a2e)
        QColor bg(0x1a, 0x1a, 0x2e);
        double bgLum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
        double fgLum = 0.299 * dm.color.red() + 0.587 * dm.color.green() + 0.114 * dm.color.blue();
        if (qAbs(bgLum - fgLum) < 80) {
            // Too close to background — brighten or use white
            displayColor = displayColor.lighter(200);
        }
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
    painter.setBrush(QColor(0x1a, 0x1a, 0x2e, 220));
    painter.setPen(QPen(QColor("#0f3460"), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
    QWidget::paintEvent(event);
}

QColor DanmakuWindow::contrastColor(const QColor &bg) const
{
    int lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
    return lum > 128 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}
