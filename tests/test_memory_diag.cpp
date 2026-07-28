#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QImage>
#include <QFont>
#include <QFontMetrics>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <fstream>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <malloc.h>
#include "core/BilibiliApi.h"
#include "core/DanmakuManager.h"
#include "core/StreamPlayer.h"

// ═══════════════════════════════════════════
// Test fixture with QGuiApplication
// ═══════════════════════════════════════════
class MemDiag : public ::testing::Test {
protected:
    static QApplication *app;

    static void SetUpTestSuite() {
        int argc = 1;
        static char *argv[] = {(char*)"test"};
        if (!app)
            app = new QApplication(argc, argv);
    }
};
QApplication *MemDiag::app = nullptr;

// ═══════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════
static long getRssKB() {
    std::ifstream f("/proc/self/statm");
    long pages = 0, total = 0;
    if (f >> total >> pages) return pages * 4;
    return 0;
}

static void printMemStats(const char *label) {
    struct mallinfo2 mi = mallinfo2();
    long rss = getRssKB();
    std::cout << "\n  ── " << label << " ──" << std::endl;
    std::cout << "  RSS: " << rss << " KB" << std::endl;
    std::cout << "  Heap: arena=" << (mi.arena/1024) << "KB"
              << " used=" << (mi.uordblks/1024) << "KB"
              << " free=" << (mi.fordblks/1024) << "KB"
              << " chunks=" << mi.ordblks
              << std::endl;
}

// ═══════════════════════════════════════════
// 1. Qt Font Cache — CJK text rendering
// ═══════════════════════════════════════════
TEST_F(MemDiag, FontCacheCjkText) {
    printMemStats("Baseline");
    QImage img(500, 100, QImage::Format_ARGB32);
    QPainter painter(&img);
    QFont font("sans-serif", 12);
    painter.setFont(font);

    // 500 unique CJK characters
    for (int cp = 0x4E00; cp < 0x4E00 + 500; cp++)
        painter.drawText(0, 20, QString(QChar(cp)));

    // Plus long Chinese text
    for (int i = 0; i < 100; i++)
        painter.drawText(0, 40, "今晚早睡了吗弹幕姬测试内存增长情况");
    painter.end();
    printMemStats("After 500 CJK chars + 100 long texts");
}

// ═══════════════════════════════════════════
// 2. QListWidget stress (create/delete cycles)
// ═══════════════════════════════════════════
TEST_F(MemDiag, ListWidgetStress) {
    printMemStats("Before list widget");
    for (int cycle = 0; cycle < 3; cycle++) {
        auto *list = new QListWidget();
        list->setMinimumWidth(300);
        for (int i = 0; i < 100; i++) {
            auto *item = new QListWidgetItem(list);
            auto *label = new QLabel(
                QString("用户%1 测试弹幕消息").arg(i));
            item->setSizeHint(label->sizeHint());
            list->setItemWidget(item, label);
        }
        while (list->count() > 20) delete list->takeItem(0);
        for (int i = 0; i < 200; i++) {
            while (list->count() >= 20) delete list->takeItem(0);
            auto *item = new QListWidgetItem(list);
            auto *label = new QLabel(
                QString("新用户%1 弹幕内容%2").arg(i).arg(cycle));
            item->setSizeHint(label->sizeHint());
            list->setItemWidget(item, label);
        }
        delete list;
    }
    printMemStats("After 3× list create/delete cycles");
}

// ═══════════════════════════════════════════
// 3. Network requests
// ═══════════════════════════════════════════
TEST_F(MemDiag, NetworkRequests) {
    printMemStats("Before network");
    for (int batch = 0; batch < 5; batch++) {
        auto *nam = new QNetworkAccessManager();
        std::vector<QNetworkReply*> replies;
        for (int i = 0; i < 20; i++) {
            QNetworkRequest req(QUrl("http://192.0.2.1/test"));
            req.setTransferTimeout(200);
            replies.push_back(nam->get(req));
        }
        int completed = 0;
        for (auto *r : replies) {
            QObject::connect(r, &QNetworkReply::finished, [&]() {
                r->deleteLater(); completed++;
            });
        }
        auto start = QDateTime::currentMSecsSinceEpoch();
        while (completed < (int)replies.size() &&
               QDateTime::currentMSecsSinceEpoch() - start < 3000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        nam->deleteLater();
    }
    printMemStats("After 5×20 network requests");
}

// ═══════════════════════════════════════════
// 4. Heap fragmentation
// ═══════════════════════════════════════════
TEST_F(MemDiag, HeapFragmentation) {
    printMemStats("Before frag");
    for (int round = 0; round < 10; round++) {
        std::vector<void*> ptrs;
        for (int i = 0; i < 1000; i++)
            ptrs.push_back(malloc((i % 512 + 1) * 4));
        for (auto *p : ptrs) free(p);
        std::vector<void*> ptrs2;
        for (int i = 0; i < 500; i++)
            ptrs2.push_back(malloc((i % 256 + 1) * 8));
        for (auto *p : ptrs2) free(p);
    }
    printMemStats("After frag (10 rounds)");
    malloc_trim(0);
    printMemStats("After malloc_trim");
}

// ═══════════════════════════════════════════
// 5. Simulated real usage
// ═══════════════════════════════════════════
TEST_F(MemDiag, SimulatedUsage) {
    printMemStats("Start");
    for (int i = 0; i < 50; i++) {
        auto *api = new BilibiliApi();
        api->setCookie("test");
        delete api;
    }
    printMemStats("After 50× BilibiliApi");
    for (int i = 0; i < 20; i++) {
        auto *sp = new StreamPlayer();
        sp->setVolume(i); sp->stop(); delete sp;
    }
    printMemStats("After 20× StreamPlayer");
    for (int i = 0; i < DanmakuManager::kFaceCacheMax + 100; i++)
        DanmakuManager::addToFaceCache(
            QString::number(i), "https://example.com/face.jpg");
    DanmakuManager::faceCache.clear();
    DanmakuManager::faceCacheOrder.clear();
    printMemStats("After faceCache fill+clear");
    malloc_trim(0);
    printMemStats("Final (malloc_trim)");
}
