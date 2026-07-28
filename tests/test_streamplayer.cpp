#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QObject>
#include "core/StreamPlayer.h"

class StreamPlayerTest : public ::testing::Test {
protected:
    static QApplication *app;
    StreamPlayer *player = nullptr;

    static void SetUpTestSuite() {
        int argc = 1;
        char *argv[] = {(char*)"test"};
        if (!app)
            app = new QApplication(argc, argv);
    }

    void SetUp() override {
        player = new StreamPlayer();
    }

    void TearDown() override {
        delete player;
    }
};

QApplication *StreamPlayerTest::app = nullptr;

TEST_F(StreamPlayerTest, Construction) {
    EXPECT_NE(player, nullptr);
    EXPECT_FALSE(player->isPlaying());
}

TEST_F(StreamPlayerTest, DefaultVolume) {
    EXPECT_EQ(player->volume(), 80);
}

TEST_F(StreamPlayerTest, VolumeRange) {
    player->setVolume(50);
    EXPECT_EQ(player->volume(), 50);

    player->setVolume(0);
    EXPECT_EQ(player->volume(), 0);

    player->setVolume(100);
    EXPECT_EQ(player->volume(), 100);
}

TEST_F(StreamPlayerTest, VolumeClamping) {
    player->setVolume(-10);
    EXPECT_GE(player->volume(), 0);

    player->setVolume(200);
    EXPECT_LE(player->volume(), 100);
}

TEST_F(StreamPlayerTest, StopWhenNotPlaying) {
    // Should not crash
    player->stop();
    EXPECT_FALSE(player->isPlaying());
}

TEST_F(StreamPlayerTest, LogMessageSignal) {
    bool received = false;
    QString receivedMsg;
    QObject::connect(player, &StreamPlayer::logMessage, [&](const QString &msg) {
        received = true;
        receivedMsg = msg;
    });
    QTimer::singleShot(0, player, [this] {
        player->logMessage("test log");
    });
    QCoreApplication::processEvents();
    EXPECT_TRUE(received);
    EXPECT_EQ(receivedMsg, "test log");
}

TEST_F(StreamPlayerTest, ErrorSignal) {
    bool received = false;
    QObject::connect(player, &StreamPlayer::error, [&](const QString &) {
        received = true;
    });
    QTimer::singleShot(0, player, [this] {
        player->error("test error");
    });
    QCoreApplication::processEvents();
    EXPECT_TRUE(received);
}

TEST_F(StreamPlayerTest, PauseResume) {
    // Should not crash when not playing
    player->pause();
    player->resume();
    SUCCEED();
}

TEST_F(StreamPlayerTest, DoubleStop) {
    // Calling stop twice should not crash
    player->stop();
    player->stop();
    SUCCEED();
}
