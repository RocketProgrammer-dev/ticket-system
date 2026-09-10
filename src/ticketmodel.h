#pragma once

#include <QAbstractTableModel>
#include <QList>

#include "ticket.h"

// A model is the bridge between your data (QList<Ticket>) and any view that
// wants to show it. You implement four functions and Qt handles scrolling,
// selection, resizing and repainting for free.
//
// The big payoff: when you replace the sample data with API results, you call
// setTickets() and nothing else in the UI changes.

class TicketModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Column order shown in the table. Using an enum instead of bare numbers
    // means `index.column() == Col_Priority` reads clearly and survives
    // reordering.
    enum Column {
        Col_Number = 0,
        Col_Title,
        Col_Status,
        Col_Priority,
        Col_CreatedBy,
        Col_Assignee,
        Col_DueDate,
        ColumnCount        // handy trick: always equals the number above it
    };

    // Custom role the sort proxy uses. Sorting priorities alphabetically would
    // put "Critical" before "Low" before "Normal" — meaningless. So we hand the
    // proxy a numeric sort key through this role instead of the display text.
    static constexpr int SortRole = Qt::UserRole + 1;

    explicit TicketModel(QObject *parent = nullptr);

    // --- The four functions every table model must provide ---------------
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // --- Your own API ------------------------------------------------------
    void setTickets(const QList<Ticket> &tickets);
    const Ticket &ticketAt(int row) const;

    // Returns -1 if the ticket isn't in the current list — for example when a
    // notification points at a ticket a filter is currently hiding.
    int rowForTicketId(int ticketId) const;

private:
    QList<Ticket> m_tickets;
};
