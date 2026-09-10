#pragma once

// Requirement 3. Its own header because three unrelated classes need it now
// (login, main window, detail dialog) and none of them should have to include
// the others to get it.

enum class UserRole {
    Staff,        // sees public comments only
    Technician,   // can also write internal notes and log time
    Manager       // everything, plus dashboard and administration
};

inline bool canWriteInternalNotes(UserRole role)
{
    return role == UserRole::Technician || role == UserRole::Manager;
}

// Requirements 6 and 7: only technical staff and managers may reassign a
// ticket or change its status and priority. Requesters can comment, not steer.
inline bool canManageTickets(UserRole role)
{
    return role == UserRole::Technician || role == UserRole::Manager;
}

inline bool canAccessReports(UserRole role)
{
    return role == UserRole::Manager;
}
