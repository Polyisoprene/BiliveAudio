#include <gtest/gtest.h>
#include <gtest/gtest-matchers.h>
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QWidget>
#include <fstream>
#include <string>
#include <vector>
#include "core/BilibiliApi.h"
#include "core/DanmakuManager.h"
#include "core/StreamPlayer.h"

// ── Helper: read current RSS from /proc/self/statm ──
static long getRssKB() {
    std::ifstream f("/proc/self/statm");
    long pages = 0, total = 0;
    if (f >> total >> pages) {
        return pages * 4;  // page size 4KB on x86_64
    }
    return 0;
}

// ── Fixture with QApplication ──
class MemTestEnv : public ::testing::Test {
protected:
    static QApplication *app;

    static void SetUpTestSuite() {
        int argc = 1;
        static char *argv[] = {(char*)"test"};
        if (!app) {
            app = new QApplication(argc, argv);
        }
    }

    static void TearDownTestSuite() {
        // Don't delete app — other tests may need it
    }
};

QApplication *MemTestEnv::app = nullptr;

// ═══════════════════════════════════════════
// 1. FaceCache LRU bounded growth
// ═══════════════════════════════════════════
TEST(MemoryFaceCacheTest, LruLimit) {
    // Fill faceCache beyond its limit
    for (int i = 1; i <= DanmakuManager::kFaceCacheMax + 500; i++) {
        DanmakuManager::addToFaceCache(
            QString::number(i),
            QString("https://example.com/face/%1.jpg").arg(i));
    }
    // Should never exceed kFaceCacheMax
    EXPECT_LE(DanmakuManager::faceCache.size(), DanmakuManager::kFaceCacheMax);
    EXPECT_LE(DanmakuManager::faceCacheOrder.size(), DanmakuManager::kFaceCacheMax);

    // The oldest entries should have been evicted
    EXPECT_FALSE(DanmakuManager::faceCache.contains("1"));
    EXPECT_FALSE(DanmakuManager::faceCache.contains("2"));

    // The newest entries should still be there
    QString lastKey = QString::number(DanmakuManager::kFaceCacheMax + 500);
    EXPECT_TRUE(DanmakuManager::faceCache.contains(lastKey));

    // Cleanup
    DanmakuManager::faceCache.clear();
    DanmakuManager::faceCacheOrder.clear();
}

TEST(MemoryFaceCacheTest, LookupRoundTrip) {
    DanmakuManager::addToFaceCache("test_user_1",
        "https://example.com/face/test.jpg");

    QString url = DanmakuManager::lookupFaceUrl("test_user_1");
    EXPECT_EQ(url, "https://example.com/face/test.jpg");

    // Non-existent user returns empty
    EXPECT_TRUE(DanmakuManager::lookupFaceUrl("no_such_user").isEmpty());

    DanmakuManager::faceCache.clear();
    DanmakuManager::faceCacheOrder.clear();
}

// ═══════════════════════════════════════════
// 2. No memory leak on repeated create/delete
// ═══════════════════════════════════════════
TEST(MemoryLeakTest, RepeatedBilibiliApiCreate) {
    long rssBefore = getRssKB();
    ASSERT_GT(rssBefore, 0L);

    const int iterations = 100;
    std::vector<BilibiliApi*> apis;
    apis.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        apis.push_back(new BilibiliApi());
    }
    for (auto *api : apis) {
        delete api;
    }
    apis.clear();

    long rssAfter = getRssKB();
    long growth = rssAfter - rssBefore;
    // Allow up to 4MB growth (Qt has per-process overhead on first allocations)
    EXPECT_LT(growth, 4096L)
        << "Creating and destroying 100 BilibiliApi objects leaked "
        << growth << " KB";
}

// ═══════════════════════════════════════════
// 3. StreamPlayer volume changes don't allocate unboundedly
// ═══════════════════════════════════════════
TEST(MemoryLeakTest, RepeatedVolumeChanges) {
    auto *player = new StreamPlayer();
    long rssBefore = getRssKB();

    for (int i = 0; i < 1000; i++) {
        player->setVolume(i % 101);
        EXPECT_EQ(player->volume(), i % 101);
    }

    player->stop();
    delete player;

    long rssAfter = getRssKB();
    long growth = rssAfter - rssBefore;
    EXPECT_LT(growth, 1024L)
        << "1000 volume changes leaked " << growth << " KB";
}
