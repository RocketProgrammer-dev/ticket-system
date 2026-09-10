#pragma once

#include <QString>
#include <QDateTime>
#include <QObject>

#include "userrole.h"   // UserRole moved here so login can use it too

// Requirement 9 and 10: comments, with public ones separated from internal
// notes. The distinction is one bool here, but on the server it must also be a
// filter in the query — a staff user's GET /api/tickets/5/comments should
// never return internal rows. Hiding them in the UI is not enough.
struct Comment
{
    int       id = 0;
    int       ticketId = 0;
    QString   author;
    QString   text;
    QDateTime createdAt;
    bool      isInternal = false;
};
