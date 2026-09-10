#pragma once

#include <QString>
#include <QList>

#include "userrole.h"

// Requirement 19 data types. Deliberately separate from UserSummary in
// user.h: that one is the trimmed version for an assignment dropdown, this
// one is the full administrative record. Same table, different purposes —
// and keeping them apart stops admin-only fields leaking into ordinary
// screens by accident.

struct AdminUser
{
    int      id = 0;
    QString  username;
    QString  fullName;
    QString  email;
    UserRole role = UserRole::Staff;
    int      departmentId = 0;   // 0 = none
    bool     isActive = true;
};

struct DepartmentInfo
{
    int     id = 0;
    QString name;
    bool    isActive = true;
};

struct CategoryInfo
{
    int     id = 0;
    QString name;
    int     departmentId = 0;
    QString departmentName;
    bool    isActive = true;
};
