#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QDate>
#include <QNetworkRequest>

#include "ticket.h"
#include "comment.h"
#include "userrole.h"
#include "user.h"
#include "admintypes.h"
#include "timeentry.h"
#include "notification.h"
#include "ticketquery.h"
#include "audittypes.h"
#include "attachment.h"

class QNetworkAccessManager;

// All HTTP lives here. The UI never builds a URL and never parses JSON.
//
// Every method returns void. Network calls are asynchronous: login() sends a
// request and returns immediately, long before the server answers. The answer
// arrives later as a signal.

class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(const QString &baseUrl, QObject *parent = nullptr);

    void login(const QString &username, const QString &password);

    // Forgets the token. There is nothing to tell the server: a JWT is
    // stateless, so "logging out" means the client stops presenting it.
    void logout();
    void fetchTickets(const TicketQuery &query);
    void fetchComments(int ticketId);
    void postComment(int ticketId, const QString &text, bool isInternal);

    void createTicket(const QString &title, const QString &description,
                      TicketPriority priority, const QDate &dueDate);

    void fetchUsers();
    void fetchDashboard();

    // Requirements 6 and 7. Only the fields you pass are changed:
    // assigneeId of -1 means "leave alone", 0 means "unassign".
    void updateTicket(int ticketId, TicketStatus status,
                      TicketPriority priority, int assigneeId);

    // Requirement 19. saveX with id == 0 creates, otherwise updates — the
    // same call serves both so the UI needs only one code path.
    // Inactive accounts are excluded by default; ten years of departed staff
    // should not be in the list just because their tickets are in the database.
    void fetchAdminUsers(bool includeInactive = false,
                         const QString &search = QString());
    void saveUser(const AdminUser &user, const QString &password);
    void deleteUser(int userId);

    void fetchTimeEntries(int ticketId);
    void addTimeEntry(int ticketId, int minutes, const QDate &workDate,
                      const QString &note);

    void fetchNotifications();
    void markNotificationRead(int notificationId);
    void markAllNotificationsRead();

    void fetchAttachments(int ticketId);
    void uploadAttachment(int ticketId, const QString &localFilePath);
    void downloadAttachment(int attachmentId, const QString &saveToPath);

    void fetchAuditLog(int page, const QString &action, const QString &search);
    void fetchAuditActions();

    void fetchDepartments();
    void saveDepartment(const DepartmentInfo &dept);

    void fetchCategories();
    void saveCategory(const CategoryInfo &category);

signals:
    void loginSucceeded(const QString &username, UserRole role);
    void loginFailed(const QString &message);

    void ticketsReceived(const TicketPage &page);
    void commentsReceived(int ticketId, const QList<Comment> &comments);
    void commentPosted(int ticketId);
    void ticketCreated(const QString &ticketNumber);
    void ticketUpdated(int ticketId);
    void usersReceived(const QList<UserSummary> &users);
    void dashboardReceived(const DashboardStats &stats);

    void timeEntriesReceived(int ticketId, const QList<TimeEntry> &entries,
                             int totalMinutes);
    void timeEntryAdded(int ticketId);

    void notificationsReceived(const QList<Notification> &items, int unreadCount);
    void notificationsChanged();   // something was marked read; re-poll

    void adminUsersReceived(const QList<AdminUser> &users);
    void departmentsReceived(const QList<DepartmentInfo> &departments);
    void categoriesReceived(const QList<CategoryInfo> &categories);
    void adminSaved();   // any admin write succeeded; refresh the lists

    // Emitted when a delete is refused because the user has history. The UI
    // offers deactivation instead rather than showing a dead end.
    void deleteRefused(int userId, const QString &reason);

    void attachmentsReceived(int ticketId, const QList<Attachment> &items);
    void attachmentUploaded(int ticketId);
    void attachmentDownloaded(const QString &savedPath);

    void auditPageReceived(const AuditPage &page);
    void auditActionsReceived(const QStringList &actions);

    void requestFailed(const QString &message);

private:
    QNetworkRequest buildRequest(const QString &path) const;

    QNetworkAccessManager *m_network;
    QString m_baseUrl;
    QString m_token;
};
