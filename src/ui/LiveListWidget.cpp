#include "LiveListWidget.h"
#include "utils/Logger.h"
#include <QSet>

LiveListWidget::LiveListWidget(QWidget *parent)
    : QListWidget(parent)
{
    connect(this, &QListWidget::itemClicked, this, &LiveListWidget::onItemClicked);
}

void LiveListWidget::updateList(const QVector<LiveRoom> &rooms)
{
    LOG_INFO("LiveList updateList: {} rooms, existing {} items", rooms.size(), count());
    clear();
    QSet<qint64> seen;
    for (auto &room : rooms) {
        auto *item = new QListWidgetItem;
        QString text = QString("🔴 %1\n%2").arg(room.username, room.title);
        item->setText(text);
        item->setData(Qt::UserRole, room.roomId);
        item->setData(Qt::UserRole + 1, room.username);
        item->setData(Qt::UserRole + 2, room.title);
        addItem(item);
        if (seen.contains(room.roomId))
            LOG_WARN("LiveList DUPLICATE roomId={} {}", room.roomId, room.username.toStdString());
        seen.insert(room.roomId);
    }
    LOG_INFO("LiveList updated: {} unique rooms", seen.size());
}

void LiveListWidget::onItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    qint64 roomId = item->data(Qt::UserRole).toLongLong();
    QString username = item->data(Qt::UserRole + 1).toString();
    QString title = item->data(Qt::UserRole + 2).toString();
    emit roomSelected(roomId, username, title);
}
