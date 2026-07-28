#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QSettings>
#include "utils/Settings.h"

class SettingsTest : public ::testing::Test {
protected:
    static QTemporaryDir *tempDir;

    static void SetUpTestSuite() {
        tempDir = new QTemporaryDir();
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir->path());
    }

    static void TearDownTestSuite() {
        delete tempDir;
    }
};

QTemporaryDir *SettingsTest::tempDir = nullptr;

TEST(SettingsBasicTest, SingletonInstance) {
    Settings &s1 = Settings::instance();
    Settings &s2 = Settings::instance();
    EXPECT_EQ(&s1, &s2);
}

TEST(SettingsBasicTest, VolumeRoundTrip) {
    Settings &s = Settings::instance();
    int original = s.volume();
    s.setVolume(42);
    EXPECT_EQ(s.volume(), 42);
    s.setVolume(original);
}

TEST(SettingsBasicTest, CookieRoundTrip) {
    Settings &s = Settings::instance();
    QString original = s.cookie();
    s.setCookie("test_cookie_value");
    EXPECT_EQ(s.cookie(), "test_cookie_value");
    s.setCookie(original);
}

TEST(SettingsBasicTest, WindowGeometryRoundTrip) {
    Settings &s = Settings::instance();
    s.setWindowGeometry(100, 200, 800, 600);
    EXPECT_EQ(s.windowX(), 100);
    EXPECT_EQ(s.windowY(), 200);
    EXPECT_EQ(s.windowWidth(), 800);
    EXPECT_EQ(s.windowHeight(), 600);
}

TEST(SettingsBasicTest, LastRoomIdRoundTrip) {
    Settings &s = Settings::instance();
    s.setLastRoomId(27183290);
    EXPECT_EQ(s.lastRoomId(), 27183290);
}

TEST(SettingsBasicTest, DanmakuImageMode) {
    Settings &s = Settings::instance();
    bool original = s.danmakuImageMode();
    s.setDanmakuImageMode(!original);
    EXPECT_EQ(s.danmakuImageMode(), !original);
    s.setDanmakuImageMode(original);
    EXPECT_EQ(s.danmakuImageMode(), original);
}

TEST(SettingsBasicTest, LogDirRoundTrip) {
    Settings &s = Settings::instance();
    s.setLogDir("/tmp/bilive_test_logs");
    EXPECT_EQ(s.logDir(), "/tmp/bilive_test_logs");
}

TEST(SettingsBasicTest, LogRetentionDays) {
    Settings &s = Settings::instance();
    s.setLogRetentionDays(3);
    EXPECT_EQ(s.logRetentionDays(), 3);
    s.setLogRetentionDays(0);  // Should clamp to 1
    EXPECT_GE(s.logRetentionDays(), 1);
}
