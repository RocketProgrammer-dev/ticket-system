#include "ticketdetaildialog.h"
#include "commentstore.h"
#include "csvexporter.h"
#include "apiclient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QTextEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDateEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QLineEdit>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QPushButton>
#include <QBrush>
#include <QColor>
#include <QFont>

TicketDetailDialog::TicketDetailDialog(const Ticket &ticket, UserRole role,
                                       CommentStore *store, ApiClient *api,
                                       QWidget *parent)
    : QDialog(parent)
    , m_ticket(ticket)
    , m_role(role)
    , m_store(store)
    , m_api(api)
{
    setWindowTitle(tr("%1 — %2").arg(ticket.number, ticket.title));
    resize(620, 560);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(buildHeaderSection());
    // Two tabs rather than one long scrolling column. Adding a History tab
    // later (requirement 13's read side) is then a one-line change.
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildCommentSection(), tr("Comments"));

    // Staff can see the total time on their ticket but not the individual
    // entries, so the tab only appears for people who can log time.
    if (canManageTickets(m_role))
        tabs->addTab(buildTimeSection(), tr("Time spent"));

    // Requirement 11. Everyone who can see the ticket can see and add files —
    // a requester attaching a screenshot is the main use case.
    tabs->addTab(buildAttachmentSection(), tr("Attachments"));

    layout->addWidget(tabs, 1);

    // A standard button box gives you correct button order per platform
    // (OK/Cancel is reversed on some systems) without thinking about it.
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    // ActionRole puts it on the left, away from Close, and means clicking it
    // does NOT close the dialog — which is what you want for an export.
    auto *exportButton = buttons->addButton(tr("Export this ticket"),
                                            QDialogButtonBox::ActionRole);
    connect(exportButton, &QPushButton::clicked,
            this, &TicketDetailDialog::onExportTicket);

    if (canManageTickets(m_role)) {
        m_saveButton = buttons->addButton(tr("Save changes"),
                                          QDialogButtonBox::ApplyRole);
        connect(m_saveButton, &QPushButton::clicked,
                this, &TicketDetailDialog::onSaveChanges);

        connect(m_api, &ApiClient::usersReceived,
                this, &TicketDetailDialog::onUsersReceived);
        connect(m_api, &ApiClient::ticketUpdated,
                this, &TicketDetailDialog::onTicketUpdated);

        m_api->fetchUsers();
    }

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Redraw if the store changes — including from somewhere else entirely,
    // which is what a background refresh from the server will look like.
    connect(m_store, &CommentStore::commentsChanged,
            this, &TicketDetailDialog::onCommentsChanged);

    // Ask the server for this ticket's comments. The list will be empty for a
    // moment and fill in when the reply lands — that's normal for async work.
    m_store->ensureLoaded(m_ticket.id);
    refreshCommentList();

    connect(m_api, &ApiClient::attachmentsReceived,
            this, &TicketDetailDialog::onAttachmentsReceived);

    connect(m_api, &ApiClient::attachmentUploaded, this, [this](int ticketId) {
        if (ticketId == m_ticket.id)
            m_api->fetchAttachments(ticketId);
    });

    m_api->fetchAttachments(m_ticket.id);

    if (canManageTickets(m_role)) {
        connect(m_api, &ApiClient::timeEntriesReceived,
                this, &TicketDetailDialog::onTimeEntriesReceived);
        connect(m_api, &ApiClient::timeEntryAdded,
                this, &TicketDetailDialog::onTimeEntryAdded);

        m_api->fetchTimeEntries(m_ticket.id);
    }
}

void TicketDetailDialog::onCommentsChanged(int ticketId)
{
    // The store shouts about every ticket; ignore the ones that aren't ours.
    if (ticketId == m_ticket.id)
        refreshCommentList();
}

QWidget *TicketDetailDialog::buildHeaderSection()
{
    auto *box  = new QGroupBox(tr("Ticket details"), this);
    auto *form = new QFormLayout(box);

    form->addRow(tr("Number:"),  new QLabel(m_ticket.number, box));
    form->addRow(tr("Subject:"), new QLabel(m_ticket.title, box));

    // Who opened it, and when. A technician reading a vague ticket needs to
    // know who to go and ask.
    const QString openedBy = m_ticket.createdAt.isValid()
        ? tr("%1  (%2)").arg(m_ticket.createdBy,
                             m_ticket.createdAt.toString("dd.MM.yyyy HH:mm"))
        : m_ticket.createdBy;

    form->addRow(tr("Opened by:"), new QLabel(openedBy, box));

    if (canManageTickets(m_role)) {
        // Requirement 7: status and priority are editable.
        m_statusCombo = new QComboBox(box);
        for (auto st : { TicketStatus::Open, TicketStatus::InProgress,
                         TicketStatus::Waiting, TicketStatus::Resolved,
                         TicketStatus::Closed }) {
            m_statusCombo->addItem(statusToString(st), static_cast<int>(st));
        }
        m_statusCombo->setCurrentIndex(static_cast<int>(m_ticket.status));

        m_priorityCombo = new QComboBox(box);
        for (auto pr : { TicketPriority::Low, TicketPriority::Normal,
                         TicketPriority::High, TicketPriority::Critical }) {
            m_priorityCombo->addItem(priorityToString(pr), static_cast<int>(pr));
        }
        m_priorityCombo->setCurrentIndex(static_cast<int>(m_ticket.priority));

        // Requirement 6. Filled in when /api/users replies; until then it has
        // only the "Unassigned" entry.
        m_assigneeCombo = new QComboBox(box);
        m_assigneeCombo->addItem(tr("Unassigned"), 0);

        form->addRow(tr("Status:"),   m_statusCombo);
        form->addRow(tr("Priority:"), m_priorityCombo);
        form->addRow(tr("Assigned:"), m_assigneeCombo);
    } else {
        // Staff see the same information, just not editable.
        form->addRow(tr("Status:"),   new QLabel(statusToString(m_ticket.status), box));
        form->addRow(tr("Priority:"), new QLabel(priorityToString(m_ticket.priority), box));
        form->addRow(tr("Assigned:"),
                     new QLabel(m_ticket.assignee.isEmpty() ? tr("Unassigned")
                                                            : m_ticket.assignee, box));
    }

    auto *dueLabel = new QLabel(
        m_ticket.dueDate.isValid() ? m_ticket.dueDate.toString("dd.MM.yyyy") : tr("None"), box);
    if (m_ticket.isOverdue()) {
        dueLabel->setText(dueLabel->text() + tr("  (overdue)"));
        dueLabel->setStyleSheet("color: #c62828; font-weight: bold;");
    }
    form->addRow(tr("Due date:"), dueLabel);

    // The description can be several paragraphs, so it needs a scrollable,
    // wrapping widget rather than a QLabel. QTextBrowser is read-only by
    // default — a read-only QTextEdit would work too, but this one won't
    // accidentally become editable if someone changes a flag later.
    auto *description = new QTextBrowser(box);
    description->setPlainText(m_ticket.description.isEmpty()
                                  ? tr("(no description)")
                                  : m_ticket.description);
    description->setMaximumHeight(110);

    if (m_ticket.description.isEmpty())
        description->setStyleSheet("color: #9e9e9e; font-style: italic;");

    form->addRow(tr("Description:"), description);

    return box;
}

QWidget *TicketDetailDialog::buildCommentSection()
{
    auto *box    = new QGroupBox(tr("Comments"), this);
    auto *layout = new QVBoxLayout(box);

    // QListWidget instead of a model here on purpose. For a short, screen-local
    // list that nothing else reads, the convenience class is the right call —
    // it stores its own items and saves you a whole model class. The big ticket
    // table earned a real model because filtering, sorting and the API all
    // needed to share it. Knowing when NOT to build the heavy version matters
    // as much as knowing how.
    m_commentList = new QListWidget(box);
    m_commentList->setWordWrap(true);
    m_commentList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_commentList, 1);

    m_commentEdit = new QTextEdit(box);
    m_commentEdit->setPlaceholderText(tr("Write a comment..."));
    m_commentEdit->setMaximumHeight(90);
    layout->addWidget(m_commentEdit);

    auto *row = new QHBoxLayout;

    m_internalCheck = new QCheckBox(tr("Internal note (not visible to the requester)"), box);
    // Requirement 3 + 10: only technical staff and managers write internal
    // notes. The server must reject them from anyone else regardless — this
    // checkbox is a courtesy, not a control.
    m_internalCheck->setVisible(canWriteInternalNotes(m_role));

    m_addButton = new QPushButton(tr("Add comment"), box);
    connect(m_addButton, &QPushButton::clicked, this, &TicketDetailDialog::onAddComment);

    row->addWidget(m_internalCheck);
    row->addStretch();
    row->addWidget(m_addButton);
    layout->addLayout(row);

    return box;
}

void TicketDetailDialog::refreshCommentList()
{
    m_commentList->clear();

    // Read fresh from the store every time, rather than keeping a local copy
    // that can drift out of date.
    const QList<Comment> comments = m_store->commentsFor(m_ticket.id);

    if (comments.isEmpty()) {
        auto *empty = new QListWidgetItem(tr("No comments yet."), m_commentList);
        empty->setForeground(QBrush(QColor("#9e9e9e")));
        QFont f = empty->font();
        f.setItalic(true);
        empty->setFont(f);
        return;
    }

    for (const Comment &c : comments) {
        // Staff never see internal notes. Belt and braces: the server should
        // not have sent them in the first place.
        if (c.isInternal && !canWriteInternalNotes(m_role))
            continue;

        const QString header = c.isInternal
            ? tr("%1 · %2 · INTERNAL NOTE")
                  .arg(c.author, c.createdAt.toString("dd.MM.yyyy HH:mm"))
            : tr("%1 · %2")
                  .arg(c.author, c.createdAt.toString("dd.MM.yyyy HH:mm"));

        auto *item = new QListWidgetItem(header + "\n" + c.text, m_commentList);

        if (c.isInternal) {
            // Visually unmistakable, because posting an internal note where a
            // customer can read it is the expensive mistake this feature exists
            // to prevent.
            item->setBackground(QBrush(QColor("#fff8e1")));
            item->setForeground(QBrush(QColor("#795548")));
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
        }
    }

    m_commentList->scrollToBottom();
}

void TicketDetailDialog::onAddComment()
{
    const QString text = m_commentEdit->toPlainText().trimmed();
    if (text.isEmpty())
        return;

    const bool internal = m_internalCheck->isVisible() && m_internalCheck->isChecked();

    // Write through the store, which POSTs it, waits for the server, then
    // re-reads the list. The author name comes from the token server-side,
    // so the client no longer supplies it.
    m_store->addComment(m_ticket.id, text, internal);

    m_commentEdit->clear();
    m_internalCheck->setChecked(false);
}

void TicketDetailDialog::onExportTicket()
{
    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    // Ticket numbers contain no path-hostile characters, so they make a safe
    // and recognisable default filename.
    const QString suggested = defaultDir + "/" + m_ticket.number + ".csv";

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export ticket"), suggested, tr("CSV files (*.csv)"));

    if (path.isEmpty())
        return;

    // Export exactly the comments this user is allowed to see. A staff user's
    // export must not contain internal notes they cannot read on screen.
    QList<Comment> visible;
    for (const Comment &c : m_store->commentsFor(m_ticket.id)) {
        if (c.isInternal && !canWriteInternalNotes(m_role))
            continue;
        visible.append(c);
    }

    QString error;
    // Staff never receive individual time entries from the server, so their
    // export simply has none — the permission rule is upheld by the data
    // never arriving, not by a check here.
    if (CsvExporter::exportTicketDetail(m_ticket, visible, m_attachments,
                                        m_timeEntries, m_totalMinutes,
                                        path, &error))
        QMessageBox::information(this, tr("Export"), tr("Ticket exported."));
    else
        QMessageBox::warning(this, tr("Export failed"), error);
}


void TicketDetailDialog::onUsersReceived(const QList<UserSummary> &users)
{
    if (!m_assigneeCombo)
        return;

    m_assigneeCombo->clear();
    m_assigneeCombo->addItem(tr("Unassigned"), 0);

    for (const UserSummary &u : users) {
        m_assigneeCombo->addItem(u.fullName, u.id);

        // The ticket carries the assignee's NAME, not their id, so match on
        // that. Storing the id in the Ticket struct would be cleaner — worth
        // doing if you extend the API later.
        if (!m_ticket.assignee.isEmpty() && u.fullName == m_ticket.assignee)
            m_assigneeCombo->setCurrentIndex(m_assigneeCombo->count() - 1);
    }
}

void TicketDetailDialog::onSaveChanges()
{
    if (!m_statusCombo || !m_priorityCombo)
        return;

    const auto status = static_cast<TicketStatus>(
        m_statusCombo->currentData().toInt());
    const auto priority = static_cast<TicketPriority>(
        m_priorityCombo->currentData().toInt());

    // 0 means unassign; -1 would mean "leave alone", which we never send from
    // here because the dropdown always has a definite value.
    const int assigneeId = m_assigneeCombo
        ? m_assigneeCombo->currentData().toInt()
        : -1;

    m_saveButton->setEnabled(false);
    m_saveButton->setText(tr("Saving..."));

    m_api->updateTicket(m_ticket.id, status, priority, assigneeId);
}

void TicketDetailDialog::onTicketUpdated(int ticketId)
{
    if (ticketId != m_ticket.id)
        return;

    m_saveButton->setEnabled(true);
    m_saveButton->setText(tr("Save changes"));

    // Close so the caller refreshes the list and the row shows the new values.
    // The history rows were written server-side as part of the same request.
    accept();
}


QWidget *TicketDetailDialog::buildTimeSection()
{
    auto *page   = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    m_totalLabel = new QLabel(tr("Total: 0m"), page);
    QFont totalFont = m_totalLabel->font();
    totalFont.setBold(true);
    m_totalLabel->setFont(totalFont);
    layout->addWidget(m_totalLabel);

    m_timeTable = new QTableWidget(0, 4, page);
    m_timeTable->setHorizontalHeaderLabels({
        tr("Date"), tr("Person"), tr("Duration"), tr("Note")
    });
    m_timeTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_timeTable->verticalHeader()->setVisible(false);
    m_timeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_timeTable, 1);

    // --- Entry form -------------------------------------------------------
    // Hours and minutes as two spin boxes rather than one "minutes" field.
    // Nobody thinks in "195 minutes"; they think "3 hours 15".
    m_hoursSpin = new QSpinBox(page);
    m_hoursSpin->setRange(0, 23);
    m_hoursSpin->setSuffix(tr(" h"));

    m_minutesSpin = new QSpinBox(page);
    m_minutesSpin->setRange(0, 59);
    m_minutesSpin->setSingleStep(15);   // most entries land on quarter hours
    m_minutesSpin->setSuffix(tr(" m"));
    m_minutesSpin->setValue(30);

    m_workDateEdit = new QDateEdit(QDate::currentDate(), page);
    m_workDateEdit->setCalendarPopup(true);
    m_workDateEdit->setDisplayFormat("dd.MM.yyyy");
    m_workDateEdit->setMaximumDate(QDate::currentDate());   // no future work

    m_timeNoteEdit = new QLineEdit(page);
    m_timeNoteEdit->setPlaceholderText(tr("What did you do?"));

    auto *logButton = new QPushButton(tr("Log time"), page);
    connect(logButton, &QPushButton::clicked, this, &TicketDetailDialog::onLogTime);

    auto *entryRow = new QHBoxLayout;
    entryRow->addWidget(m_workDateEdit);
    entryRow->addWidget(m_hoursSpin);
    entryRow->addWidget(m_minutesSpin);
    entryRow->addWidget(m_timeNoteEdit, 1);
    entryRow->addWidget(logButton);
    layout->addLayout(entryRow);

    return page;
}

void TicketDetailDialog::onLogTime()
{
    const int minutes = m_hoursSpin->value() * 60 + m_minutesSpin->value();

    if (minutes <= 0) {
        QMessageBox::information(this, tr("Log time"),
                                 tr("Enter a duration greater than zero."));
        return;
    }

    m_api->addTimeEntry(m_ticket.id, minutes,
                        m_workDateEdit->date(),
                        m_timeNoteEdit->text().trimmed());
}

void TicketDetailDialog::onTimeEntriesReceived(int ticketId,
                                               const QList<TimeEntry> &entries,
                                               int totalMinutes)
{
    if (ticketId != m_ticket.id || !m_timeTable)
        return;

    m_timeEntries  = entries;
    m_totalMinutes = totalMinutes;

    m_totalLabel->setText(tr("Total: %1").arg(minutesToText(totalMinutes)));

    m_timeTable->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const TimeEntry &t = entries.at(i);
        m_timeTable->setItem(i, 0,
            new QTableWidgetItem(t.workDate.toString("dd.MM.yyyy")));
        m_timeTable->setItem(i, 1, new QTableWidgetItem(t.userName));
        m_timeTable->setItem(i, 2, new QTableWidgetItem(minutesToText(t.minutes)));
        m_timeTable->setItem(i, 3, new QTableWidgetItem(t.note));
    }
}

void TicketDetailDialog::onTimeEntryAdded(int ticketId)
{
    if (ticketId != m_ticket.id)
        return;

    // Re-read rather than appending locally, so the table shows exactly what
    // the server stored. Same reasoning as the comment store.
    m_api->fetchTimeEntries(m_ticket.id);

    m_hoursSpin->setValue(0);
    m_minutesSpin->setValue(30);
    m_timeNoteEdit->clear();
}


QWidget *TicketDetailDialog::buildAttachmentSection()
{
    auto *page   = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    m_attachmentTable = new QTableWidget(0, 4, page);
    m_attachmentTable->setHorizontalHeaderLabels({
        tr("File"), tr("Size"), tr("Uploaded by"), tr("Date")
    });
    m_attachmentTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_attachmentTable->verticalHeader()->setVisible(false);
    m_attachmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_attachmentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(m_attachmentTable, &QTableWidget::doubleClicked,
            this, &TicketDetailDialog::onDownloadAttachment);
    layout->addWidget(m_attachmentTable, 1);

    auto *uploadButton   = new QPushButton(tr("Add file..."), page);
    auto *downloadButton = new QPushButton(tr("Download selected"), page);

    connect(uploadButton, &QPushButton::clicked,
            this, &TicketDetailDialog::onUploadAttachment);
    connect(downloadButton, &QPushButton::clicked,
            this, &TicketDetailDialog::onDownloadAttachment);

    auto *hint = new QLabel(
        tr("Images, PDFs and Office documents up to 10 MB."), page);
    hint->setStyleSheet("color: #757575;");

    auto *row = new QHBoxLayout;
    row->addWidget(uploadButton);
    row->addWidget(downloadButton);
    row->addWidget(hint, 1);
    layout->addLayout(row);

    return page;
}

void TicketDetailDialog::onAttachmentsReceived(int ticketId,
                                               const QList<Attachment> &items)
{
    if (ticketId != m_ticket.id || !m_attachmentTable)
        return;

    m_attachments = items;
    m_attachmentTable->setRowCount(items.size());

    for (int i = 0; i < items.size(); ++i) {
        const Attachment &a = items.at(i);
        m_attachmentTable->setItem(i, 0, new QTableWidgetItem(a.name));
        m_attachmentTable->setItem(i, 1, new QTableWidgetItem(formatFileSize(a.sizeBytes)));
        m_attachmentTable->setItem(i, 2, new QTableWidgetItem(a.uploadedBy));
        m_attachmentTable->setItem(i, 3, new QTableWidgetItem(
            a.uploadedAt.toLocalTime().toString("dd.MM.yyyy HH:mm")));
    }
}

void TicketDetailDialog::onUploadAttachment()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Attach a file"), QString(),
        tr("Allowed files (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.pdf *.txt "
           "*.csv *.log *.doc *.docx *.xls *.xlsx *.ppt *.pptx *.zip)"));

    if (path.isEmpty())
        return;

    // The filter above is a convenience only — the server enforces the real
    // whitelist, because a file dialog filter is trivially bypassed.
    m_api->uploadAttachment(m_ticket.id, path);
}

void TicketDetailDialog::onDownloadAttachment()
{
    const auto selection = m_attachmentTable->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::information(this, tr("Download"), tr("Select a file first."));
        return;
    }

    const int row = selection.first().row();
    if (row < 0 || row >= m_attachments.size())
        return;

    const Attachment &a = m_attachments.at(row);

    const QString target = QFileDialog::getSaveFileName(
        this, tr("Save file"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            + "/" + a.name);

    if (target.isEmpty())
        return;

    m_api->downloadAttachment(a.id, target);
}
