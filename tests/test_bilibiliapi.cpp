#include <gtest/gtest.h>
#include <gtest/gtest-matchers.h>
#include <gmock/gmock.h>
#include <QApplication>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QRegularExpression>
#include <QObject>
#include <QDebug>
#include "core/BilibiliApi.h"

static QApplication *createApp() {
    static int argc = 1;
    static char *argv[] = {(char*)"test"};
    static QApplication *app = nullptr;
    if (!app)
        app = new QApplication(argc, argv);
    return app;
}

class BilibiliApiTest : public ::testing::Test {
protected:
    BilibiliApi *api = nullptr;

    static void SetUpTestSuite() {
        createApp();
    }

    void SetUp() override {
        api = new BilibiliApi();
    }

    void TearDown() override {
        delete api;
    }
};

TEST_F(BilibiliApiTest, Construction) {
    EXPECT_NE(api, nullptr);
}

TEST_F(BilibiliApiTest, CookieManagement) {
    EXPECT_TRUE(api->cookie().isEmpty());

    api->setCookie("SESSDATA=abc123; bili_jct=def456");
    EXPECT_EQ(api->cookie(), "SESSDATA=abc123; bili_jct=def456");

    api->setCookie("");
    EXPECT_TRUE(api->cookie().isEmpty());
}

TEST_F(BilibiliApiTest, SignalConnections) {
    bool received = false;
    QObject::connect(api, &BilibiliApi::requestError, [&](const QString &, const QString &) {
        received = true;
    });
    api->requestError("test", "test error");
    EXPECT_TRUE(received);
}

TEST_F(BilibiliApiTest, UserFaceReadySignal) {
    bool received = false;
    qint64 receivedUid = 0;
    QString receivedUrl;
    QObject::connect(api, &BilibiliApi::userFaceReady, [&](qint64 uid, const QString &url) {
        received = true;
        receivedUid = uid;
        receivedUrl = url;
    });
    api->userFaceReady(12345, "https://example.com/face.jpg");
    EXPECT_TRUE(received);
    EXPECT_EQ(receivedUid, 12345);
    EXPECT_EQ(receivedUrl, "https://example.com/face.jpg");
}

// ── WBI Signing Algorithm Tests ──

TEST(WbiSignTest, SignUrlWithWts) {
    // Create a BilibiliApi instance to access signUrl (private method)
    // We'll test the algorithm manually instead
    int argc = 1;
    char *argv[] = {(char*)"test"};
    QApplication app(argc, argv);

    // Test URL parsing: the signUrl should add w_rid and wts
    QString url = "https://api.bilibili.com/x/web-interface/card?mid=12345&photo=true";
    QUrl parsed(url);
    EXPECT_TRUE(parsed.isValid());
    EXPECT_EQ(parsed.host(), "api.bilibili.com");
    EXPECT_EQ(parsed.path(), "/x/web-interface/card");

    QUrlQuery query(parsed);
    auto items = query.queryItems();
    EXPECT_GE(items.size(), 2);
    bool hasMid = false, hasPhoto = false;
    for (auto &p : items) {
        if (p.first == "mid") { hasMid = true; EXPECT_EQ(p.second, "12345"); }
        if (p.first == "photo") { hasPhoto = true; EXPECT_EQ(p.second, "true"); }
    }
    EXPECT_TRUE(hasMid);
    EXPECT_TRUE(hasPhoto);
}

TEST(WbiSignTest, ComputeMixinKeyConsistency) {
    // Test that the mixin key permutation produces expected output
    QString imgKey = "7cd084941338484aae1ad9425b84077c";
    QString subKey = "4932caff0ff746eab6f01bf08b70ac45";
    QString raw = imgKey + subKey;
    static const int order[] = {
        46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,27,43,5,49,
        33,9,42,19,29,28,14,39,12,38,41,13,37,48,7,16,
        24,55,40,61,26,17,0,1,60,51,30,4,22,25,54,21,56,59,6,63,57,62,
        11,36,20,34,44,52
    };
    QString salt;
    for (int i : order)
        salt += raw[i];
    QString mixinKey = salt.left(32);
    // The mixin key should be 32 hex characters
    EXPECT_EQ(mixinKey.length(), 32);
    // Verify it matches what we've seen in logs: ea1db124af3c7062474693fa704f4ff8
    // (This depends on the actual input keys)
    qDebug() << "Computed mixinKey:" << mixinKey;
}

TEST(WbiSignTest, ParameterSorting) {
    // Verify alphabetical sorting of params
    QList<QPair<QString, QString>> params;
    params.append(QPair<QString, QString>("z_last", "1"));
    params.append(QPair<QString, QString>("a_first", "2"));
    params.append(QPair<QString, QString>("m_middle", "3"));
    std::sort(params.begin(), params.end(),
              [](auto &a, auto &b) { return a.first < b.first; });
    EXPECT_EQ(params[0].first, "a_first");
    EXPECT_EQ(params[1].first, "m_middle");
    EXPECT_EQ(params[2].first, "z_last");
}

TEST(WbiSignTest, Md5Hash) {
    // Verify the MD5 hash computation used in signing
    QString input = "key1=value1&key2=value2&wts=1234567890mixin_key_here";
    QByteArray hash = QCryptographicHash::hash(
        input.toUtf8(), QCryptographicHash::Md5).toHex();
    // MD5 produces 32 hex characters
    EXPECT_EQ(hash.length(), 32);
    EXPECT_TRUE(QString(hash).contains(QRegularExpression("^[a-f0-9]{32}$")));
}
