#pragma once

#include <QString>
#include <QDateTime>

// Requirement 14.
struct Notification
{
    int       id = 0;
    int       ticketId = 0;   // 0 = not tied to a ticket
    QString   message;
    bool      isRead = false;
    QDateTime createdAt;
};
