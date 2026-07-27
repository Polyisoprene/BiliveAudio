#include "LiveListWidget.h"
#include "utils/Logger.h"

LiveListWidget::LiveListWidget(QWidget *parent)
    : QListWidget(parent)
{
    connect(this, &QListWidget::itemClicked, this, &LiveListWidget::onItemClicked);
}

void LiveListWidget::updateList(const QVector<LiveRoom> &rooms)
{
    clear();
    for (auto &room : rooms) {
        auto *item = new QListWidgetItem;
        QString text = QString("🔴 %1\n%2").arg(room.username, room.title);
        item->setText(text);
        item->setData(Qt::UserRole, room.roomId);
        item->setData(Qt::UserRole + 1, room.username);
        item->setData(Qt::UserRole + 2, room.title);
        addItem(item);
    }
    LOG_INFO("Live list updated: {} rooms", rooms.size());
}

void LiveListWidget::onItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    qint64 roomId = item->data(Qt::UserRole).toLongLong();
    QString username = item->data(Qt::UserRole + 1).toString();
    QString title = item->data(Qt::UserRole + 2).toString();
    emit roomSelected(roomId, username, title);
}
