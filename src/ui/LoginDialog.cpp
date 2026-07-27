#include "LoginDialog.h"
#include "core/BilibiliApi.h"
#include "utils/Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <qrencode.h>

LoginDialog::LoginDialog(BilibiliApi *api, QWidget *parent)
    : QDialog(parent), m_api(api)
{
    setWindowTitle("Bilibili 登录");
    setFixedSize(320, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(16);

    auto *title = new QLabel("扫码登录 Bilibili");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 16px; font-weight: bold;");
    layout->addWidget(title);

    m_qrLabel = new QLabel;
    m_qrLabel->setFixedSize(260, 260);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setStyleSheet("border: 1px solid #333; background: white;");
    layout->addWidget(m_qrLabel, 0, Qt::AlignCenter);

    m_statusLabel = new QLabel("正在获取二维码...");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto *btnBar = new QHBoxLayout;
    m_refreshBtn = new QPushButton("刷新二维码");
    auto *cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet("background-color: #444;");
    btnBar->addWidget(m_refreshBtn);
    btnBar->addWidget(cancelBtn);
    layout->addLayout(btnBar);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(2000);

    connect(m_refreshBtn, &QPushButton::clicked, this, &LoginDialog::fetchQRCode);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_pollTimer, &QTimer::timeout, this, [this] {
        m_api->pollQRCode(m_qrcodeKey);
    });
    connect(m_api, &BilibiliApi::qrCodeFetched, this, &LoginDialog::onQRCodeFetched);
    connect(m_api, &BilibiliApi::qrCodePollResult, this, &LoginDialog::onPollResult);

    fetchQRCode();
}

void LoginDialog::fetchQRCode()
{
    m_statusLabel->setText("正在获取二维码...");
    m_api->fetchQRCode();
}

void LoginDialog::onQRCodeFetched(const QString &url, const QString &key)
{
    m_qrcodeKey = key;

    // Generate QR code from the URL
    auto *qr = QRcode_encodeString(url.toUtf8().constData(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!qr) {
        m_statusLabel->setText("二维码生成失败，请重试");
        return;
    }

    int scale = 8;
    QImage qrImage(qr->width * scale + 20, qr->width * scale + 20, QImage::Format_RGB32);
    qrImage.fill(Qt::white);
    QPainter painter(&qrImage);
    painter.setBrush(Qt::black);
    painter.setPen(Qt::NoPen);

    for (int y = 0; y < qr->width; y++) {
        for (int x = 0; x < qr->width; x++) {
            if (qr->data[y * qr->width + x] & 0x01)
                painter.drawRect(x * scale + 10, y * scale + 10, scale, scale);
        }
    }
    painter.end();
    QRcode_free(qr);

    m_qrLabel->setPixmap(QPixmap::fromImage(qrImage));
    m_statusLabel->setText("请使用 Bilibili App 扫码");
    m_pollTimer->start();
    LOG_INFO("QR code displayed, polling started");
}

void LoginDialog::onPollResult(const QString &status, const QString &cookie, const QString &username)
{
    if (status == "confirmed") {
        m_pollTimer->stop();
        m_statusLabel->setText("登录成功！");
        LOG_INFO("Login successful: {}", username.toStdString());
        emit loginSuccess(cookie, username);
        accept();
    } else if (status == "expired") {
        m_pollTimer->stop();
        m_statusLabel->setText("二维码已过期，请刷新");
        LOG_WARN("QR code expired");
    } else if (status == "scanned") {
        m_statusLabel->setText("已扫码，请在手机上确认...");
    } else {
        m_statusLabel->setText("请使用 Bilibili App 扫码");
    }
}
