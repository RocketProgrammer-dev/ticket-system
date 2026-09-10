#pragma once

#include <QString>
#include <QList>

#include "ticket.h"
#include "comment.h"
#include "attachment.h"
#include "timeentry.h"

class QAbstractItemModel;

namespace CsvExporter {

// Exports rows of a table model. Pass the PROXY model so the output matches
// what the user is looking at (filtered and sorted).
//
// `rows` limits the export to specific row numbers. An empty list means
// "everything" — that default keeps the common case simple.
bool exportToFile(const QAbstractItemModel *model,
                  const QString &path,
                  QString *errorMessage,
                  const QList<int> &rows = {},
                  QChar separator = ',');

// A single ticket with its comments. Shaped differently on purpose: one
// record is not a table, so this writes field/value pairs followed by a
// comment block, rather than one wide unreadable row.
bool exportTicketDetail(const Ticket &ticket,
                        const QList<Comment> &comments,
                        const QList<Attachment> &attachments,
                        const QList<TimeEntry> &timeEntries,
                        int totalMinutes,
                        const QString &path,
                        QString *errorMessage,
                        QChar separator = ',');

} // namespace CsvExporter
