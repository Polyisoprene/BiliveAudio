#pragma once
#include <QWidget>
#include <QTextEdit>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QEvent>
#include <QMap>
#include <QPixmap>
#include <QNetworkAccessManager>
#include "models/Danmaku.h"

class DanmakuWindow : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuWindow(QWidget *parent = nullptr);
    void addDanmaku(const Danmaku &dm);
    void clear() { m_display->clear(); }
    void setConnected(bool connected);

signals:
    void sendDanmakuRequested(const QString &text);
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void onSendClicked();
    QString ensureAvatar(const QString &uid, const QString &url);

    QTextEdit *m_display = nullptr;
    QLineEdit *m_input = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPoint m_dragPos;
    double m_opacity = 0.85;
    int m_maxLines = 200;
    QNetworkAccessManager *m_avatarNam = nullptr;
    QMap<QString, QPixmap> m_avatarPixmaps;

    QColor contrastColor(const QColor &bg) const;
};
