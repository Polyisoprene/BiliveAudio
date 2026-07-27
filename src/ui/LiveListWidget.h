#pragma once
#include <QListWidget>
#include <QVector>
#include "models/LiveRoom.h"

class LiveListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit LiveListWidget(QWidget *parent = nullptr);
    void updateList(const QVector<LiveRoom> &rooms);

signals:
    void roomSelected(qint64 roomId, const QString &username, const QString &title);

private:
    void onItemClicked(QListWidgetItem *item);
};
