#pragma once

#include <QDialog>
#include <QList>

#include "notification.h"

class QListWidget;
class QPushButton;
class ApiClient;

class NotificationsDialog : public QDialog
{
    Q_OBJECT

public:
    NotificationsDialog(const QList<Notification> &items,
                        ApiClient *api, QWidget *parent = nullptr);

signals:
    // The dialog doesn't know how to open a ticket; the main window does.
    void ticketRequested(int ticketId);

private slots:
    void onItemActivated();
    void onMarkAllRead();

private:
    void populate();

    ApiClient          *m_api;   // not owned
    QList<Notification> m_items;
    QListWidget        *m_list;
    QPushButton        *m_markAllButton;
};
