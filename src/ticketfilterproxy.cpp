#include "ticketfilterproxy.h"
#include "ticketmodel.h"

TicketFilterProxy::TicketFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // Sort using the numeric keys the model exposes, not the visible text.
    setSortRole(TicketModel::SortRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void TicketFilterProxy::setSearchText(const QString &text)
{
    m_search = text.trimmed();
    invalidateFilter();   // "re-run filterAcceptsRow on every row"
}

void TicketFilterProxy::setStatusFilter(std::optional<TicketStatus> status)
{
    m_status = status;
    invalidateFilter();
}

void TicketFilterProxy::setPriorityFilter(std::optional<TicketPriority> priority)
{
    m_priority = priority;
    invalidateFilter();
}

void TicketFilterProxy::setOverdueOnly(bool enabled)
{
    m_overdueOnly = enabled;
    invalidateFilter();
}

bool TicketFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent)

    // We need the actual Ticket, not its display strings, so reach through to
    // the concrete model. qobject_cast returns nullptr if the type is wrong,
    // which is why we check rather than using a C-style cast.
    const auto *model = qobject_cast<const TicketModel *>(sourceModel());
    if (!model)
        return true;

    const Ticket &t = model->ticketAt(sourceRow);

    if (m_status.has_value() && t.status != m_status.value())
        return false;

    if (m_priority.has_value() && t.priority != m_priority.value())
        return false;

    if (m_overdueOnly && !t.isOverdue())
        return false;

    if (!m_search.isEmpty()) {
        const bool matches =
            t.number.contains(m_search, Qt::CaseInsensitive) ||
            t.title.contains(m_search, Qt::CaseInsensitive) ||
            t.assignee.contains(m_search, Qt::CaseInsensitive) ||
            t.createdBy.contains(m_search, Qt::CaseInsensitive);
        if (!matches)
            return false;
    }

    return true;
}
