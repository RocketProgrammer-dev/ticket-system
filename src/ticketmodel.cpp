#include "ticketmodel.h"

#include <QBrush>
#include <QColor>
#include <QFont>

TicketModel::TicketModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int TicketModel::rowCount(const QModelIndex &parent) const
{
    // A table model has no tree structure, so anything with a valid parent
    // has zero children. This guard is required; without it views misbehave.
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_tickets.size());
}

int TicketModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant TicketModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tickets.size())
        return {};

    const Ticket &t = m_tickets.at(index.row());

    // A "role" is the view asking a different question about the same cell.
    // DisplayRole = what text to draw. ForegroundRole = what colour. And so on.
    switch (role) {

    case Qt::DisplayRole:
        switch (index.column()) {
        case Col_Number:   return t.number;
        case Col_Title:    return t.title;
        case Col_Status:   return statusToString(t.status);
        case Col_Priority: return priorityToString(t.priority);
        case Col_CreatedBy: return t.createdBy;
        case Col_Assignee: return t.assignee.isEmpty() ? tr("Unassigned") : t.assignee;
        case Col_DueDate:
            return t.dueDate.isValid() ? t.dueDate.toString("dd.MM.yyyy") : QString("-");
        }
        return {};

    case SortRole:
        // Numeric keys where alphabetical order would be wrong.
        switch (index.column()) {
        case Col_Status:   return static_cast<int>(t.status);
        case Col_Priority: return static_cast<int>(t.priority);
        case Col_DueDate:  return t.dueDate;   // QDate compares chronologically
        default:           return data(index, Qt::DisplayRole);
        }

    case Qt::ForegroundRole:
        // Requirement 15 made visible: late tickets turn red.
        if (t.isOverdue())
            return QBrush(QColor("#c62828"));
        return {};

    case Qt::FontRole:
        if (t.priority == TicketPriority::Critical) {
            QFont f;
            f.setBold(true);
            return f;
        }
        return {};

    case Qt::ToolTipRole:
        if (t.isOverdue())
            return tr("Overdue since %1").arg(t.dueDate.toString("dd.MM.yyyy"));
        return t.title;

    case Qt::TextAlignmentRole:
        if (index.column() == Col_DueDate || index.column() == Col_Priority)
            return QVariant(Qt::AlignCenter);
        return {};
    }

    return {};
}

QVariant TicketModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};

    switch (section) {
    case Col_Number:   return tr("Ticket no");
    case Col_Title:    return tr("Subject");
    case Col_Status:   return tr("Status");
    case Col_Priority: return tr("Priority");
    case Col_CreatedBy: return tr("Opened by");
    case Col_Assignee: return tr("Assigned to");
    case Col_DueDate:  return tr("Due date");
    }
    return {};
}

void TicketModel::setTickets(const QList<Ticket> &tickets)
{
    // beginResetModel/endResetModel tell every attached view: "throw away
    // everything you knew and read me again." Required whenever you replace
    // the whole dataset, otherwise the view keeps stale row counts and crashes.
    beginResetModel();
    m_tickets = tickets;
    endResetModel();
}

const Ticket &TicketModel::ticketAt(int row) const
{
    return m_tickets.at(row);
}

int TicketModel::rowForTicketId(int ticketId) const
{
    for (int i = 0; i < m_tickets.size(); ++i) {
        if (m_tickets.at(i).id == ticketId)
            return i;
    }
    return -1;
}
