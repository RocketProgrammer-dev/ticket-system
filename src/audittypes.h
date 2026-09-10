#pragma once

#include <QString>
#include <QDateTime>
#include <QList>

// Requirement 21.
struct AuditEntry
{
    int       id = 0;
    QString   action;        // "login_failed", "ticket_created", ...
    QString   entityType;
    int       entityId = 0;
    QString   details;       // raw JSON string, shown as-is
    QString   userName;
    QDateTime createdAt;
};

struct AuditPage
{
    QList<AuditEntry> items;
    int page       = 1;
    int totalCount = 0;
    int totalPages = 1;
};
