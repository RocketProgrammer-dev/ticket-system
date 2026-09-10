#include "notificationsdialog.h"
#include "apiclient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QFont>
#include <QBrush>
#include <QColor>

NotificationsDialog::NotificationsDialog(const QList<Notification> &items,
                                         ApiClient *api, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
    , m_items(items)
{
    setWindowTitle(tr("Notifications"));
    resize(520, 420);

    m_list = new QListWidget(this);
    m_list->setWordWrap(true);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &NotificationsDialog::onItemActivated);

    m_markAllButton = new QPushButton(tr("Mark all as read"), this);
    connect(m_markAllButton, &QPushButton::clicked,
            this, &NotificationsDialog::onMarkAllRead);

    auto *hint = new QLabel(tr("Double-click a notification to open its ticket."), this);
    hint->setStyleSheet("color: #757575;");

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_markAllButton);
    bottom->addStretch();
    bottom->addWidget(buttons);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_list, 1);
    layout->addWidget(hint);
    layout->addLayout(bottom);

    populate();
}

void NotificationsDialog::populate()
{
    m_list->clear();

    if (m_items.isEmpty()) {
        auto *empty = new QListWidgetItem(tr("Nothing new."), m_list);
        empty->setForeground(QBrush(QColor("#9e9e9e")));
        m_markAllButton->setEnabled(false);
        return;
    }

    for (const Notification &n : m_items) {
        auto *item = new QListWidgetItem(
            n.createdAt.toString("dd.MM.yyyy HH:mm") + "\n" + n.message, m_list);

        // Unread entries are bold, the way every mail client does it. The
        // convention is doing the work here — no legend required.
        if (!n.isRead) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        } else {
            item->setForeground(QBrush(QColor("#757575")));
        }

        // Store the ids on the item so the handler doesn't need to index back
        // into m_items — safer if the list is ever filtered or sorted.
        item->setData(Qt::UserRole,     n.id);
        item->setData(Qt::UserRole + 1, n.ticketId);
    }
}

void NotificationsDialog::onItemActivated()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;

    const int notificationId = item->data(Qt::UserRole).toInt();
    const int ticketId       = item->data(Qt::UserRole + 1).toInt();

    if (notificationId > 0)
        m_api->markNotificationRead(notificationId);

    if (ticketId > 0) {
        emit ticketRequested(ticketId);
        accept();   // close so the ticket window isn't buried behind this one
    }
}

void NotificationsDialog::onMarkAllRead()
{
    m_api->markAllNotificationsRead();
    accept();
}
