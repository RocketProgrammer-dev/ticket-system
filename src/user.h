#pragma once

#include <QString>
#include <QList>

#include "userrole.h"

// Just enough about a user to populate an "assign to" dropdown. Deliberately
// not the full user record — the client has no business holding password
// hashes or email addresses it never displays.
struct UserSummary
{
    int      id = 0;
    QString  fullName;
    UserRole role = UserRole::Staff;
};

// Requirement 17: the numbers behind the dashboard. Kept as a plain struct so
// the dashboard widget doesn't have to know the JSON came from anywhere.
struct DashboardStats
{
    int total = 0;
    int open = 0;
    int inProgress = 0;
    int waiting = 0;
    int resolved = 0;
    int closed = 0;

    int overdue = 0;
    int unassigned = 0;

    int low = 0;
    int normal = 0;
    int high = 0;
    int critical = 0;

    // Open ticket count per assignee, for the workload table.
    QList<QPair<QString, int>> workload;
};
