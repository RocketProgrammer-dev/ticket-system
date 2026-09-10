#pragma once

#include <QString>
#include <optional>

#include "ticket.h"

// Everything that goes into GET /api/tickets, in one object.
//
// Bundling the parameters means adding a filter later changes one struct
// rather than a function signature that half a dozen call sites pass through.

struct TicketQuery
{
    int page     = 1;
    int pageSize = 25;

    QString search;
    std::optional<TicketStatus>   status;
    std::optional<TicketPriority> priority;
    bool overdueOnly = false;

    QString sortBy;              // "number", "title", "priority", "duedate", ...
    bool    sortDescending = false;
};

// What comes back: the page itself plus enough information to draw the
// controls ("Page 2 of 7, 168 tickets").
struct TicketPage
{
    QList<Ticket> items;
    int page       = 1;
    int pageSize   = 25;
    int totalCount = 0;
    int totalPages = 1;
};
