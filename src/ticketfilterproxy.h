#pragma once

#include <QSortFilterProxyModel>
#include <optional>

#include "ticket.h"

// A proxy model sits between the model and the view and hides rows you don't
// want. You get search + filter + sort (requirement 16) without touching
// TicketModel at all.
//
// std::optional here means "no filter set" as distinct from "filter set to
// TicketStatus::Open" — cleaner than reserving a magic -1 value.

class TicketFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TicketFilterProxy(QObject *parent = nullptr);

public slots:
    void setSearchText(const QString &text);
    void setStatusFilter(std::optional<TicketStatus> status);
    void setPriorityFilter(std::optional<TicketPriority> priority);
    void setOverdueOnly(bool enabled);

protected:
    // Called by Qt once per row. Return true to show it, false to hide it.
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString                        m_search;
    std::optional<TicketStatus>    m_status;
    std::optional<TicketPriority>  m_priority;
    bool                           m_overdueOnly = false;
};
