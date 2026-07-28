#include <gtest/gtest.h>
#include "models/Danmaku.h"

TEST(DanmakuTest, DefaultConstruction) {
    Danmaku dm;
    EXPECT_TRUE(dm.text.isEmpty());
    EXPECT_TRUE(dm.username.isEmpty());
    EXPECT_TRUE(dm.uid.isEmpty());
    EXPECT_FALSE(dm.color.isValid());  // QColor default constructs as invalid
    EXPECT_TRUE(dm.type.isEmpty());    // no default value in the struct
    EXPECT_EQ(dm.medalLevel, 0);
    EXPECT_EQ(dm.price, 0);
    EXPECT_EQ(dm.giftCount, 0);
}

TEST(DanmakuTest, FormattedTime) {
    Danmaku dm;
    dm.timestamp = 0;
    EXPECT_EQ(dm.formattedTime(), QString("08:00:00"));

    dm.timestamp = 3600;  // 1970-01-01 01:00:00 UTC
    EXPECT_EQ(dm.formattedTime(), QString("09:00:00"));
}

TEST(DanmakuTest, TypeAssignment) {
    Danmaku dm;
    dm.type = "danmaku";
    EXPECT_EQ(dm.type, "danmaku");

    dm.type = "sc";
    EXPECT_EQ(dm.type, "sc");

    dm.type = "gift";
    EXPECT_EQ(dm.type, "gift");
}

TEST(DanmakuTest, FaceUrlHandling) {
    Danmaku dm;
    EXPECT_TRUE(dm.faceUrl.isEmpty());
    dm.faceUrl = "https://i0.hdslb.com/bfs/face/test.jpg";
    EXPECT_TRUE(dm.faceUrl.startsWith("http"));

    dm.faceUrl.clear();
    EXPECT_TRUE(dm.faceUrl.isEmpty());
}

TEST(DanmakuTest, FansMedal) {
    Danmaku dm;
    dm.medalName = "雪撬犬";
    dm.medalLevel = 25;
    dm.medalColor = QColor("#DAA520");
    EXPECT_EQ(dm.medalName, "雪撬犬");
    EXPECT_EQ(dm.medalLevel, 25);
    EXPECT_TRUE(dm.medalColor.isValid());
}

TEST(DanmakuTest, SuperChat) {
    Danmaku dm;
    dm.type = "sc";
    dm.price = 50000;  // 50元 (以分为单位)
    dm.text = "测试SC消息";
    dm.username = "测试用户";
    EXPECT_EQ(dm.type, "sc");
    EXPECT_EQ(dm.price, 50000);
}

TEST(DanmakuTest, Gift) {
    Danmaku dm;
    dm.type = "gift";
    dm.giftName = "小电视";
    dm.giftCount = 1;
    dm.text = "送出 小电视 x1";
    EXPECT_EQ(dm.type, "gift");
    EXPECT_EQ(dm.giftCount, 1);
}

TEST(DanmakuTest, CopyByValue) {
    Danmaku a;
    a.text = "测试文本";
    a.uid = "123456";
    a.username = "用户A";
    a.timestamp = 1000;
    Danmaku b = a;
    EXPECT_EQ(b.text, a.text);
    EXPECT_EQ(b.uid, a.uid);
    EXPECT_EQ(b.username, a.username);
    EXPECT_EQ(b.timestamp, a.timestamp);
    // Modify original, copy should be unchanged
    a.text = "修改后的文本";
    EXPECT_NE(b.text, a.text);
}
