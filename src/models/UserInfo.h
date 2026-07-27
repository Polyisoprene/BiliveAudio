#pragma once
#include <QString>

struct UserInfo {
    qint64 uid = 0;
    QString username;
    QString avatarUrl;
    bool isLoggedIn = false;
};
