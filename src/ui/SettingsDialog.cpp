#include "SettingsDialog.h"
#include "utils/Settings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QGroupBox>
#include <QStandardPaths>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("设置");
    setFixedSize(450, 350);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // Log settings group
    auto *logGroup = new QGroupBox("日志");
    auto *logForm = new QFormLayout(logGroup);

    m_logDirEdit = new QLineEdit;
    auto *logBrowseBtn = new QPushButton("浏览...");
    logBrowseBtn->setFixedWidth(60);
    auto *logDirRow = new QHBoxLayout;
    logDirRow->addWidget(m_logDirEdit);
    logDirRow->addWidget(logBrowseBtn);
    logForm->addRow("存储路径:", logDirRow);

    m_retentionSpin = new QSpinBox;
    m_retentionSpin->setRange(1, 90);
    m_retentionSpin->setSuffix(" 天");
    logForm->addRow("保留天数:", m_retentionSpin);

    layout->addWidget(logGroup);

    // Cookie settings group
    auto *cookieGroup = new QGroupBox("Cookie");
    auto *cookieForm = new QFormLayout(cookieGroup);

    m_cookiePathEdit = new QLineEdit;
    m_cookiePathEdit->setReadOnly(true);
    auto *cookieBrowseBtn = new QPushButton("浏览...");
    cookieBrowseBtn->setFixedWidth(60);
    auto *cookieDirRow = new QHBoxLayout;
    cookieDirRow->addWidget(m_cookiePathEdit);
    cookieDirRow->addWidget(cookieBrowseBtn);
    cookieForm->addRow("配置文件:", cookieDirRow);
    layout->addWidget(cookieGroup);

    // Config path info
    m_configPathLabel = new QLabel;
    m_configPathLabel->setStyleSheet("color: #888; font-size: 11px;");
    m_configPathLabel->setWordWrap(true);
    layout->addWidget(m_configPathLabel);

    layout->addStretch();

    auto *btnBar = new QHBoxLayout;
    auto *saveBtn = new QPushButton("保存");
    auto *cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet("background-color: #444;");
    btnBar->addStretch();
    btnBar->addWidget(saveBtn);
    btnBar->addWidget(cancelBtn);
    layout->addLayout(btnBar);

    loadSettings();

    connect(logBrowseBtn, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "选择日志目录",
            m_logDirEdit->text());
        if (!dir.isEmpty())
            m_logDirEdit->setText(dir);
    });

    connect(cookieBrowseBtn, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "选择配置文件目录",
            m_cookiePathEdit->text());
        if (!dir.isEmpty()) {
            m_cookiePathEdit->setText(dir);
        }
    });

    connect(saveBtn, &QPushButton::clicked, this, [this] {
        saveSettings();
        accept();
    });

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void SettingsDialog::loadSettings()
{
    auto &s = Settings::instance();
    m_logDirEdit->setText(s.logDir());
    m_retentionSpin->setValue(s.logRetentionDays());
    m_cookiePathEdit->setText(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + "/biliveaudio.conf");
    m_configPathLabel->setText("重启应用后日志路径修改生效");
}

void SettingsDialog::saveSettings()
{
    auto &s = Settings::instance();
    s.setLogDir(m_logDirEdit->text());
    s.setLogRetentionDays(m_retentionSpin->value());
}
