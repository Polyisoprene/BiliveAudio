#include "PlayerControl.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QStyle>

PlayerControl::PlayerControl(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_roomInfoLabel = new QLabel("未在播放");
    m_roomInfoLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
    layout->addWidget(m_roomInfoLabel);

    auto *controlBar = new QHBoxLayout;

    m_playBtn = new QPushButton;
    m_playBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playBtn->setFixedSize(36, 36);
    controlBar->addWidget(m_playBtn);

    m_statusLabel = new QLabel("停止");
    controlBar->addWidget(m_statusLabel);

    controlBar->addStretch();

    auto *volLabel = new QLabel("音量:");
    controlBar->addWidget(volLabel);

    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedWidth(120);
    controlBar->addWidget(m_volumeSlider);

    layout->addLayout(controlBar);

    connect(m_playBtn, &QPushButton::clicked, this, [this] {
        emit playPauseClicked();
    });

    connect(m_volumeSlider, &QSlider::valueChanged, this, &PlayerControl::volumeChanged);
}

void PlayerControl::setPlaying(bool playing)
{
    m_isPlaying = playing;
    m_playBtn->setIcon(style()->standardIcon(
        playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    m_statusLabel->setText(playing ? "播放中" : "已暂停");
}

void PlayerControl::setVolume(int percent)
{
    m_volumeSlider->setValue(percent);
}

void PlayerControl::setRoomInfo(const QString &username, const QString &title)
{
    m_roomInfoLabel->setText(username.isEmpty()
        ? "未在播放"
        : QString("%1 - %2").arg(username, title));
}
