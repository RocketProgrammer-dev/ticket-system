#include "csvexporter.h"

#include <QAbstractItemModel>
#include <QFile>
#include <QTextStream>
#include <QObject>

namespace {

// CSV has one tricky rule: a field containing the separator, a quote or a
// newline must be wrapped in double quotes, with inner quotes doubled. Get it
// wrong and a subject containing a comma shifts every later column by one.
QString escapeField(const QString &value, QChar separator)
{
    const bool needsQuoting = value.contains(separator)
                              || value.contains('"')
                              || value.contains('\n')
                              || value.contains('\r');

    if (!needsQuoting)
        return value;

    QString escaped = value;
    escaped.replace('"', "\"\"");
    return '"' + escaped + '"';
}

// Opens the file and writes the UTF-8 BOM. Without those three bytes, Excel
// on Windows reads UTF-8 as Windows-1252 and Turkish characters come out as
// garbage: "Gecikmiş" becomes "GecikmiÅŸ".
bool openForCsv(QFile &file, QTextStream &out, QString *errorMessage)
{
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = QObject::tr("Could not open file: %1").arg(file.errorString());
        return false;
    }

    out.setDevice(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QChar(0xFEFF);
    return true;
}

} // namespace


bool CsvExporter::exportToFile(const QAbstractItemModel *model,
                               const QString &path,
                               QString *errorMessage,
                               const QList<int> &rows,
                               QChar separator)
{
    if (!model) {
        if (errorMessage)
            *errorMessage = QObject::tr("No data to export.");
        return false;
    }

    QFile file(path);
    QTextStream out;
    if (!openForCsv(file, out, errorMessage))
        return false;

    const int cols = model->columnCount();

    QStringList headers;
    headers.reserve(cols);
    for (int c = 0; c < cols; ++c) {
        headers << escapeField(
            model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString(), separator);
    }
    out << headers.join(separator) << "\r\n";

    // An empty `rows` list means every row. Building the full list here keeps
    // the writing loop below identical for both cases.
    QList<int> rowsToWrite = rows;
    if (rowsToWrite.isEmpty()) {
        const int total = model->rowCount();
        rowsToWrite.reserve(total);
        for (int r = 0; r < total; ++r)
            rowsToWrite.append(r);
    }

    for (int r : rowsToWrite) {
        if (r < 0 || r >= model->rowCount())
            continue;   // stale selection, skip rather than crash

        QStringList fields;
        fields.reserve(cols);
        for (int c = 0; c < cols; ++c) {
            const QModelIndex idx = model->index(r, c);
            fields << escapeField(model->data(idx, Qt::DisplayRole).toString(), separator);
        }
        out << fields.join(separator) << "\r\n";
    }

    out.flush();
    file.close();

    if (file.error() != QFile::NoError) {
        if (errorMessage)
            *errorMessage = QObject::tr("Write failed: %1").arg(file.errorString());
        return false;
    }
    return true;
}


bool CsvExporter::exportTicketDetail(const Ticket &ticket,
                                     const QList<Comment> &comments,
                                     const QList<Attachment> &attachments,
                                     const QList<TimeEntry> &timeEntries,
                                     int totalMinutes,
                                     const QString &path,
                                     QString *errorMessage,
                                     QChar separator)
{
    QFile file(path);
    QTextStream out;
    if (!openForCsv(file, out, errorMessage))
        return false;

    auto writeRow = [&](const QString &a, const QString &b) {
        out << escapeField(a, separator) << separator
            << escapeField(b, separator) << "\r\n";
    };

    // Section 1: the ticket itself, as field/value pairs.
    writeRow(QObject::tr("Field"), QObject::tr("Value"));
    writeRow(QObject::tr("Ticket number"), ticket.number);
    writeRow(QObject::tr("Subject"),       ticket.title);
    writeRow(QObject::tr("Status"),        statusToString(ticket.status));
    writeRow(QObject::tr("Priority"),      priorityToString(ticket.priority));
    writeRow(QObject::tr("Opened by"), ticket.createdBy);
    writeRow(QObject::tr("Assigned to"),
             ticket.assignee.isEmpty() ? QObject::tr("Unassigned") : ticket.assignee);
    writeRow(QObject::tr("Due date"),
             ticket.dueDate.isValid() ? ticket.dueDate.toString("dd.MM.yyyy")
                                      : QObject::tr("None"));
    writeRow(QObject::tr("Overdue"),
             ticket.isOverdue() ? QObject::tr("Yes") : QObject::tr("No"));
    writeRow(QObject::tr("Total time spent"), minutesToText(totalMinutes));
    writeRow(QObject::tr("Description"), ticket.description);

    // Blank line separates the two sections. Spreadsheet programs handle this
    // fine; they simply show an empty row.
    out << "\r\n";

    // Section 2: comments, as a proper table with its own header.
    out << escapeField(QObject::tr("Date"), separator) << separator
        << escapeField(QObject::tr("Author"), separator) << separator
        << escapeField(QObject::tr("Type"), separator) << separator
        << escapeField(QObject::tr("Comment"), separator) << "\r\n";

    if (comments.isEmpty()) {
        out << escapeField(QObject::tr("(no comments)"), separator) << "\r\n";
    } else {
        for (const Comment &c : comments) {
            out << escapeField(c.createdAt.toString("dd.MM.yyyy HH:mm"), separator) << separator
                << escapeField(c.author, separator) << separator
                << escapeField(c.isInternal ? QObject::tr("Internal note")
                                            : QObject::tr("Comment"), separator) << separator
                << escapeField(c.text, separator) << "\r\n";
        }
    }

    // Section 3: time entries. Empty for a staff user, because the server
    // never sends them the breakdown — only the total, which is already in
    // the summary above.
    out << "\r\n";

    out << escapeField(QObject::tr("Work date"), separator) << separator
        << escapeField(QObject::tr("Person"), separator) << separator
        << escapeField(QObject::tr("Duration"), separator) << separator
        << escapeField(QObject::tr("Note"), separator) << "\r\n";

    if (timeEntries.isEmpty()) {
        out << escapeField(QObject::tr("(no time entries)"), separator) << "\r\n";
    } else {
        for (const TimeEntry &t : timeEntries) {
            out << escapeField(t.workDate.toString("dd.MM.yyyy"), separator) << separator
                << escapeField(t.userName, separator) << separator
                << escapeField(minutesToText(t.minutes), separator) << separator
                << escapeField(t.note, separator) << "\r\n";
        }
    }

    // Section 4: attachments. Only the metadata — a CSV cannot carry the
    // files themselves, but a record that says a screenshot existed, who
    // uploaded it and when, is what makes the export a usable audit trail.
    out << "\r\n";

    out << escapeField(QObject::tr("Attachment"), separator) << separator
        << escapeField(QObject::tr("Size"), separator) << separator
        << escapeField(QObject::tr("Uploaded by"), separator) << separator
        << escapeField(QObject::tr("Date"), separator) << "\r\n";

    if (attachments.isEmpty()) {
        out << escapeField(QObject::tr("(no attachments)"), separator) << "\r\n";
    } else {
        for (const Attachment &a : attachments) {
            out << escapeField(a.name, separator) << separator
                << escapeField(formatFileSize(a.sizeBytes), separator) << separator
                << escapeField(a.uploadedBy, separator) << separator
                << escapeField(a.uploadedAt.toString("dd.MM.yyyy HH:mm"), separator)
                << "\r\n";
        }
    }

    out.flush();
    file.close();

    if (file.error() != QFile::NoError) {
        if (errorMessage)
            *errorMessage = QObject::tr("Write failed: %1").arg(file.errorString());
        return false;
    }
    return true;
}
