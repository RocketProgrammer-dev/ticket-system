#pragma once

#include <QDialog>
#include <QList>

#include "ticket.h"
#include "comment.h"
#include "userrole.h"
#include "user.h"
#include "timeentry.h"
#include "attachment.h"

class QListWidget;
class QTextEdit;
class QCheckBox;
class QPushButton;
class QLabel;

class CommentStore;
class ApiClient;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QDateEdit;
class QTableWidget;
class QTabWidget;
class QPushButton;

// The dialog no longer owns any comments. It holds a pointer to the shared
// store, reads from it when it needs to draw, and writes through it when the
// user adds something. That single change is what stopped every ticket from
// showing the same conversation.

class TicketDetailDialog : public QDialog
{
    Q_OBJECT

public:
    TicketDetailDialog(const Ticket &ticket,
                       UserRole role,
                       CommentStore *store,
                       ApiClient *api,
                       QWidget *parent = nullptr);

private slots:
    void onAddComment();
    void onCommentsChanged(int ticketId);
    void onExportTicket();
    void onSaveChanges();
    void onUsersReceived(const QList<UserSummary> &users);
    void onTicketUpdated(int ticketId);
    void onLogTime();
    void onTimeEntriesReceived(int ticketId, const QList<TimeEntry> &entries,
                               int totalMinutes);
    void onTimeEntryAdded(int ticketId);
    void onAttachmentsReceived(int ticketId, const QList<Attachment> &items);
    void onUploadAttachment();
    void onDownloadAttachment();

private:
    QWidget *buildHeaderSection();
    QWidget *buildCommentSection();
    QWidget *buildTimeSection();
    QWidget *buildAttachmentSection();
    void     refreshCommentList();

    Ticket        m_ticket;
    UserRole      m_role;
    CommentStore *m_store;     // not owned
    ApiClient    *m_api;       // not owned: the main window owns it

    // Requirements 6 and 7: editable for technicians and managers,
    // read-only labels for everyone else.
    QComboBox   *m_statusCombo   = nullptr;
    QComboBox   *m_priorityCombo = nullptr;
    QComboBox   *m_assigneeCombo = nullptr;
    QPushButton *m_saveButton    = nullptr;

    // Requirement 12
    QTableWidget *m_timeTable    = nullptr;
    QList<TimeEntry> m_timeEntries;    // kept for the CSV export
    int m_totalMinutes = 0;
    QSpinBox     *m_hoursSpin    = nullptr;
    QSpinBox     *m_minutesSpin  = nullptr;
    QDateEdit    *m_workDateEdit = nullptr;
    QLineEdit    *m_timeNoteEdit = nullptr;
    QLabel       *m_totalLabel   = nullptr;

    // Requirement 11
    QTableWidget *m_attachmentTable = nullptr;
    QList<Attachment> m_attachments;

    QListWidget *m_commentList;
    QTextEdit   *m_commentEdit;
    QCheckBox   *m_internalCheck;
    QPushButton *m_addButton;
};
