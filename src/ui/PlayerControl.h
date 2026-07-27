#pragma once
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>

class PlayerControl : public QWidget {
    Q_OBJECT
public:
    explicit PlayerControl(QWidget *parent = nullptr);
    void setPlaying(bool playing);
    bool isPlaying() const { return m_isPlaying; }
    void setVolume(int percent);
    void setRoomInfo(const QString &username, const QString &title);

signals:
    void playPauseClicked();
    void volumeChanged(int percent);
    void stopClicked();

private:
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QLabel *m_roomInfoLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_isPlaying = false;
};
