#pragma once

#include <QString>
#include <QDate>
#include <QDateTime>
#include <QObject>   // needed for QObject::tr() below

// A plain data struct. No Qt magic here on purpose: this is your business
// object, and it should not know anything about tables, buttons or HTTP.
// Later you will fill these from JSON returned by your REST API.

enum class TicketStatus {
    Open,
    InProgress,
    Waiting,
    Resolved,
    Closed
};

enum class TicketPriority {
    Low,
    Normal,
    High,
    Critical
};

struct Ticket
{
    int            id = 0;          // database primary key
    QString        number;          // human-facing, e.g. "TKT-2026-000123"
    QString        title;
    QString        description;   // full text, shown in the detail dialog
    TicketStatus   status   = TicketStatus::Open;
    TicketPriority priority = TicketPriority::Normal;
    QString        assignee;        // display name; empty = unassigned
    QString        createdBy;       // who opened the ticket
    QDateTime      createdAt;
    QDate          dueDate;         // invalid QDate = no deadline set

    // Requirement 15: overdue tickets are detected automatically.
    // A ticket is late if it has a deadline, the deadline has passed, and
    // it is not finished yet.
    bool isOverdue() const
    {
        if (!dueDate.isValid())
            return false;
        if (status == TicketStatus::Resolved || status == TicketStatus::Closed)
            return false;
        return dueDate < QDate::currentDate();
    }
};

// Free functions rather than members: keeps the struct a pure data holder.
// `inline` lets them live in the header without linker errors.

inline QString statusToString(TicketStatus s)
{
    switch (s) {
    case TicketStatus::Open:       return QObject::tr("Open");
    case TicketStatus::InProgress: return QObject::tr("In progress");
    case TicketStatus::Waiting:    return QObject::tr("Waiting");
    case TicketStatus::Resolved:   return QObject::tr("Resolved");
    case TicketStatus::Closed:     return QObject::tr("Closed");
    }
    return {};
}

inline QString priorityToString(TicketPriority p)
{
    switch (p) {
    case TicketPriority::Low:      return QObject::tr("Low");
    case TicketPriority::Normal:   return QObject::tr("Normal");
    case TicketPriority::High:     return QObject::tr("High");
    case TicketPriority::Critical: return QObject::tr("Critical");
    }
    return {};
}
