#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QCheckBox>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    QLineEdit *m_logDirEdit = nullptr;
    QLineEdit *m_cookiePathEdit = nullptr;
    QLineEdit *m_cacheDirEdit = nullptr;
    QCheckBox *m_imageModeCheck = nullptr;
    QSpinBox *m_retentionSpin = nullptr;
    QLabel *m_configPathLabel = nullptr;

    void loadSettings();
    void saveSettings();
};
