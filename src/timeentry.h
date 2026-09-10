#pragma once

#include <QString>
#include <QDate>
#include <QObject>

// Requirement 12.
struct TimeEntry
{
    int     id = 0;
    QString userName;
    int     minutes = 0;
    QDate   workDate;
    QString note;
};

// "195" means nothing at a glance; "3h 15m" does. Used in the table and the
// running total, so it lives here rather than being written twice.
inline QString minutesToText(int minutes)
{
    if (minutes <= 0)
        return QObject::tr("0m");

    const int hours = minutes / 60;
    const int mins  = minutes % 60;

    if (hours == 0)
        return QObject::tr("%1m").arg(mins);
    if (mins == 0)
        return QObject::tr("%1h").arg(hours);

    return QObject::tr("%1h %2m").arg(hours).arg(mins);
}
