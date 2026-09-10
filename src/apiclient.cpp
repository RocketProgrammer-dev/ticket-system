#include "apiclient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QHttpMultiPart>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

namespace {

// The API sends enums as strings ("in_progress"), matching the PostgreSQL
// ENUM labels in schema.sql. Keeping the mapping in one place means a change
// to the API vocabulary is a one-file edit.
TicketStatus statusFromApi(const QString &s)
{
    if (s == "in_progress") return TicketStatus::InProgress;
    if (s == "waiting")     return TicketStatus::Waiting;
    if (s == "resolved")    return TicketStatus::Resolved;
    if (s == "closed")      return TicketStatus::Closed;
    return TicketStatus::Open;   // safe default for anything unrecognised
}

TicketPriority priorityFromApi(const QString &s)
{
    if (s == "low")      return TicketPriority::Low;
    if (s == "high")     return TicketPriority::High;
    if (s == "critical") return TicketPriority::Critical;
    return TicketPriority::Normal;
}

UserRole roleFromApi(const QString &s)
{
    if (s == "manager")    return UserRole::Manager;
    if (s == "technician") return UserRole::Technician;
    return UserRole::Staff;   // least privilege when in doubt
}

// The API parses these with Enum.Parse, so the strings must match the C#
// enum member names, not the lowercase database labels.
QString roleToApi(UserRole r)
{
    switch (r) {
    case UserRole::Manager:    return "Manager";
    case UserRole::Technician: return "Technician";
    default:                   return "Staff";
    }
}

QString statusToApi(TicketStatus s)
{
    switch (s) {
    case TicketStatus::InProgress: return "InProgress";
    case TicketStatus::Waiting:    return "Waiting";
    case TicketStatus::Resolved:   return "Resolved";
    case TicketStatus::Closed:     return "Closed";
    default:                       return "Open";
    }
}

QString priorityToApi(TicketPriority p)
{
    switch (p) {
    case TicketPriority::Low:      return "Low";
    case TicketPriority::High:     return "High";
    case TicketPriority::Critical: return "Critical";
    default:                       return "Normal";
    }
}

Ticket ticketFromJson(const QJsonObject &o)
{
    Ticket t;
    t.id       = o.value("id").toInt();
    t.number   = o.value("number").toString();
    t.title    = o.value("title").toString();
    t.description = o.value("description").toString();
    t.assignee  = o.value("assignee").toString();
    t.createdBy = o.value("createdBy").toString();
    t.createdAt = QDateTime::fromString(o.value("createdAt").toString(), Qt::ISODate);
    t.dueDate  = QDate::fromString(o.value("dueDate").toString(), Qt::ISODate);
    t.status   = statusFromApi(o.value("status").toString());
    t.priority = priorityFromApi(o.value("priority").toString());
    return t;
}

Comment commentFromJson(const QJsonObject &o)
{
    Comment c;
    c.id         = o.value("id").toInt();
    c.ticketId   = o.value("ticketId").toInt();
    c.author     = o.value("author").toString();
    c.text       = o.value("text").toString();
    c.isInternal = o.value("isInternal").toBool();
    c.createdAt  = QDateTime::fromString(o.value("createdAt").toString(), Qt::ISODate);
    return c;
}

} // namespace


ApiClient::ApiClient(const QString &baseUrl, QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_baseUrl(baseUrl)
{
}

QNetworkRequest ApiClient::buildRequest(const QString &path) const
{
    QNetworkRequest req(QUrl(m_baseUrl + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());

    return req;
}

void ApiClient::login(const QString &username, const QString &password)
{
    QJsonObject body;
    body["username"] = username;
    body["password"] = password;

    QNetworkReply *reply = m_network->post(buildRequest("/api/auth/login"),
                                           QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (http == 401) {
            emit loginFailed(tr("Wrong username or password."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit loginFailed(tr("Cannot reach the server: %1").arg(reply->errorString()));
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        // The role comes from the server, signed into the token. This is why
        // the old role dropdown on the login screen had to go: a client that
        // chooses its own permissions is not a permission system.
        m_token = obj.value("token").toString();

        emit loginSucceeded(obj.value("username").toString(),
                            roleFromApi(obj.value("role").toString()));
    });
}

void ApiClient::fetchTickets(const TicketQuery &query)
{
    QUrl url(m_baseUrl + "/api/tickets");
    QUrlQuery params;

    params.addQueryItem("page",     QString::number(query.page));
    params.addQueryItem("pageSize", QString::number(query.pageSize));

    if (!query.search.trimmed().isEmpty())
        params.addQueryItem("search", query.search.trimmed());

    // Only send a filter when one is set. Sending "status=" would make the
    // server parse an empty string on every request for no reason.
    if (query.status.has_value())
        params.addQueryItem("status", statusToApi(query.status.value()));

    if (query.priority.has_value())
        params.addQueryItem("priority", priorityToApi(query.priority.value()));

    if (query.overdueOnly)
        params.addQueryItem("overdueOnly", "true");

    if (!query.sortBy.isEmpty()) {
        params.addQueryItem("sortBy", query.sortBy);
        params.addQueryItem("sortDesc", query.sortDescending ? "true" : "false");
    }

    url.setQuery(params);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());

    QNetworkReply *reply = m_network->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not load tickets: %1").arg(reply->errorString()));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit requestFailed(tr("Unexpected response from server."));
            return;
        }

        const QJsonObject o = doc.object();

        TicketPage result;
        result.page       = o.value("page").toInt(1);
        result.pageSize   = o.value("pageSize").toInt(25);
        result.totalCount = o.value("totalCount").toInt();
        result.totalPages = o.value("totalPages").toInt(1);

        for (const QJsonValue &v : o.value("items").toArray())
            result.items.append(ticketFromJson(v.toObject()));

        emit ticketsReceived(result);
    });
}

void ApiClient::fetchComments(int ticketId)
{
    QNetworkReply *reply = m_network->get(
        buildRequest(QString("/api/tickets/%1/comments").arg(ticketId)));

    // ticketId is captured so the handler knows which ticket the reply belongs
    // to — several can be in flight at once.
    connect(reply, &QNetworkReply::finished, this, [this, reply, ticketId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not load comments: %1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isArray()) {
            emit requestFailed(tr("Unexpected response from server."));
            return;
        }

        QList<Comment> comments;
        for (const QJsonValue &v : doc.array())
            comments.append(commentFromJson(v.toObject()));

        emit commentsReceived(ticketId, comments);
    });
}

void ApiClient::postComment(int ticketId, const QString &text, bool isInternal)
{
    QJsonObject body;
    body["text"]       = text;
    body["isInternal"] = isInternal;

    QNetworkReply *reply = m_network->post(
        buildRequest(QString("/api/tickets/%1/comments").arg(ticketId)),
        QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, ticketId]() {
        reply->deleteLater();

        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (http == 403) {
            // The server rejected an internal note from a staff user. This is
            // requirement 20 working exactly as intended.
            emit requestFailed(tr("You are not allowed to post internal notes."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not post comment: %1").arg(reply->errorString()));
            return;
        }

        // Don't guess what the server stored — re-read it.
        emit commentPosted(ticketId);
    });
}

void ApiClient::createTicket(const QString &title, const QString &description,
                             TicketPriority priority, const QDate &dueDate)
{
    QJsonObject body;
    body["title"]       = title;
    body["description"] = description;
    body["priority"]    = priorityToApi(priority);

    // An invalid QDate means "no deadline". Send JSON null rather than an
    // empty string — DateOnly? on the C# side accepts null, not "".
    if (dueDate.isValid())
        body["dueDate"] = dueDate.toString(Qt::ISODate);
    else
        body["dueDate"] = QJsonValue();

    QNetworkReply *reply = m_network->post(buildRequest("/api/tickets"),
                                           QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not create ticket: %1").arg(reply->errorString()));
            return;
        }

        // The server generated the ticket number via the database trigger,
        // so read it back rather than guessing.
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        emit ticketCreated(obj.value("number").toString());
    });
}


void ApiClient::fetchUsers()
{
    QNetworkReply *reply = m_network->get(buildRequest("/api/users"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 403 is expected for staff — they never see the dropdown anyway.
        // Anything else is a real failure and must be reported, not swallowed.
        // Silent returns here cost an hour of debugging an empty dropdown.
        if (http == 403)
            return;

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not load the staff list: %1 (HTTP %2)")
                                   .arg(reply->errorString()).arg(http));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isArray())
            return;

        QList<UserSummary> users;
        for (const QJsonValue &v : doc.array()) {
            const QJsonObject o = v.toObject();
            UserSummary u;
            u.id       = o.value("id").toInt();
            u.fullName = o.value("fullName").toString();
            u.role     = roleFromApi(o.value("role").toString());
            users.append(u);
        }

        emit usersReceived(users);
    });
}

void ApiClient::updateTicket(int ticketId, TicketStatus status,
                             TicketPriority priority, int assigneeId)
{
    QJsonObject body;
    body["status"]   = statusToApi(status);
    body["priority"] = priorityToApi(priority);

    // -1 is our "don't touch it" sentinel; anything else is an explicit
    // change, including 0 which means unassign.
    if (assigneeId >= 0) {
        body["hasAssignee"]  = true;
        body["assignedToId"] = assigneeId > 0 ? QJsonValue(assigneeId) : QJsonValue();
    }

    QNetworkReply *reply = m_network->sendCustomRequest(
        buildRequest(QString("/api/tickets/%1").arg(ticketId)),
        "PUT",
        QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, ticketId]() {
        reply->deleteLater();

        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (http == 403) {
            emit requestFailed(tr("You are not allowed to change this ticket."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not update ticket: %1").arg(reply->errorString()));
            return;
        }

        emit ticketUpdated(ticketId);
    });
}

void ApiClient::fetchDashboard()
{
    QNetworkReply *reply = m_network->get(buildRequest("/api/dashboard"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const int http = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit requestFailed(tr("Could not load dashboard: %1 (HTTP %2)")
                                   .arg(reply->errorString()).arg(http));
            return;
        }

        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();

        DashboardStats st;
        st.total      = o.value("total").toInt();
        st.open       = o.value("open").toInt();
        st.inProgress = o.value("inProgress").toInt();
        st.waiting    = o.value("waiting").toInt();
        st.resolved   = o.value("resolved").toInt();
        st.closed     = o.value("closed").toInt();
        st.overdue    = o.value("overdue").toInt();
        st.unassigned = o.value("unassigned").toInt();
        st.low        = o.value("low").toInt();
        st.normal     = o.value("normal").toInt();
        st.high       = o.value("high").toInt();
        st.critical   = o.value("critical").toInt();

        for (const QJsonValue &v : o.value("workload").toArray()) {
            const QJsonObject w = v.toObject();
            st.workload.append({ w.value("name").toString(), w.value("count").toInt() });
        }

        emit dashboardReceived(st);
    });
}


// --- Administration (requirement 19) ---------------------------------------

void ApiClient::fetchAdminUsers(bool includeInactive, const QString &search)
{
    // QUrlQuery percent-encodes for us. Building the string by hand would
    // break the moment someone searches for a name containing a space or an
    // ampersand — and Turkish names with spaces are the common case here.
    QUrl url(m_baseUrl + "/api/admin/users");
    QUrlQuery query;
    query.addQueryItem("includeInactive", includeInactive ? "true" : "false");

    if (!search.trimmed().isEmpty())
        query.addQueryItem("search", search.trimmed());

    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());

    QNetworkReply *reply = m_network->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not load users: %1").arg(reply->errorString()));
            return;
        }

        QList<AdminUser> users;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array()) {
            const QJsonObject o = v.toObject();
            AdminUser u;
            u.id           = o.value("id").toInt();
            u.username     = o.value("username").toString();
            u.fullName     = o.value("fullName").toString();
            u.email        = o.value("email").toString();
            u.role         = roleFromApi(o.value("role").toString());
            u.departmentId = o.value("departmentId").toInt();
            u.isActive     = o.value("isActive").toBool();
            users.append(u);
        }

        emit adminUsersReceived(users);
    });
}

void ApiClient::saveUser(const AdminUser &user, const QString &password)
{
    QJsonObject body;
    body["username"]     = user.username;
    body["fullName"]     = user.fullName;
    body["email"]        = user.email;
    body["role"]         = roleToApi(user.role);
    body["departmentId"] = user.departmentId;
    body["isActive"]     = user.isActive;
    body["password"]     = password;   // blank on edit means "keep existing"

    const QByteArray payload = QJsonDocument(body).toJson();

    // id 0 means this user doesn't exist yet: POST to create, PUT to update.
    QNetworkReply *reply = user.id == 0
        ? m_network->post(buildRequest("/api/admin/users"), payload)
        : m_network->sendCustomRequest(
              buildRequest(QString("/api/admin/users/%1").arg(user.id)),
              "PUT", payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            // The server explains refusals in a JSON "error" field; showing
            // that is far more useful than a generic HTTP message.
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            const QString detail = o.value("error").toString();

            emit requestFailed(detail.isEmpty()
                ? tr("Could not save user (HTTP %1).").arg(http)
                : detail);
            return;
        }

        emit adminSaved();
    });
}

void ApiClient::fetchDepartments()
{
    QNetworkReply *reply = m_network->get(buildRequest("/api/admin/departments"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not load departments: %1").arg(reply->errorString()));
            return;
        }

        QList<DepartmentInfo> list;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array()) {
            const QJsonObject o = v.toObject();
            DepartmentInfo d;
            d.id       = o.value("id").toInt();
            d.name     = o.value("name").toString();
            d.isActive = o.value("isActive").toBool();
            list.append(d);
        }

        emit departmentsReceived(list);
    });
}

void ApiClient::saveDepartment(const DepartmentInfo &dept)
{
    QJsonObject body;
    body["name"]     = dept.name;
    body["isActive"] = dept.isActive;

    const QByteArray payload = QJsonDocument(body).toJson();

    QNetworkReply *reply = dept.id == 0
        ? m_network->post(buildRequest("/api/admin/departments"), payload)
        : m_network->sendCustomRequest(
              buildRequest(QString("/api/admin/departments/%1").arg(dept.id)),
              "PUT", payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not save department: %1").arg(reply->errorString()));
            return;
        }
        emit adminSaved();
    });
}

void ApiClient::fetchCategories()
{
    QNetworkReply *reply = m_network->get(buildRequest("/api/admin/categories"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not load categories: %1").arg(reply->errorString()));
            return;
        }

        QList<CategoryInfo> list;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array()) {
            const QJsonObject o = v.toObject();
            CategoryInfo c;
            c.id             = o.value("id").toInt();
            c.name           = o.value("name").toString();
            c.departmentId   = o.value("departmentId").toInt();
            c.departmentName = o.value("departmentName").toString();
            c.isActive       = o.value("isActive").toBool();
            list.append(c);
        }

        emit categoriesReceived(list);
    });
}

void ApiClient::saveCategory(const CategoryInfo &category)
{
    QJsonObject body;
    body["name"]         = category.name;
    body["departmentId"] = category.departmentId;
    body["isActive"]     = category.isActive;

    const QByteArray payload = QJsonDocument(body).toJson();

    QNetworkReply *reply = category.id == 0
        ? m_network->post(buildRequest("/api/admin/categories"), payload)
        : m_network->sendCustomRequest(
              buildRequest(QString("/api/admin/categories/%1").arg(category.id)),
              "PUT", payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not save category: %1").arg(reply->errorString()));
            return;
        }
        emit adminSaved();
    });
}


// --- Time tracking (requirement 12) ----------------------------------------

void ApiClient::fetchTimeEntries(int ticketId)
{
    QNetworkReply *reply = m_network->get(
        buildRequest(QString("/api/tickets/%1/time-entries").arg(ticketId)));

    connect(reply, &QNetworkReply::finished, this, [this, reply, ticketId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not load time entries: %1")
                                   .arg(reply->errorString()));
            return;
        }

        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();

        QList<TimeEntry> entries;
        for (const QJsonValue &v : o.value("entries").toArray()) {
            const QJsonObject e = v.toObject();
            TimeEntry t;
            t.id       = e.value("id").toInt();
            t.userName = e.value("userName").toString();
            t.minutes  = e.value("minutes").toInt();
            t.workDate = QDate::fromString(e.value("workDate").toString(), Qt::ISODate);
            t.note     = e.value("note").toString();
            entries.append(t);
        }

        emit timeEntriesReceived(ticketId, entries,
                                 o.value("totalMinutes").toInt());
    });
}

void ApiClient::addTimeEntry(int ticketId, int minutes, const QDate &workDate,
                             const QString &note)
{
    QJsonObject body;
    body["minutes"]  = minutes;
    body["workDate"] = workDate.toString(Qt::ISODate);
    body["note"]     = note;

    QNetworkReply *reply = m_network->post(
        buildRequest(QString("/api/tickets/%1/time-entries").arg(ticketId)),
        QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, ticketId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // Server validation messages ("Work date cannot be in the future")
            // are far more useful than the HTTP status alone.
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            const QString detail = o.value("error").toString();

            emit requestFailed(detail.isEmpty()
                ? tr("Could not log time: %1").arg(reply->errorString())
                : detail);
            return;
        }

        emit timeEntryAdded(ticketId);
    });
}


// --- Notifications (requirement 14) ----------------------------------------

void ApiClient::fetchNotifications()
{
    QNetworkReply *reply = m_network->get(buildRequest("/api/notifications"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        // Deliberately silent on failure. This runs on a timer every 30
        // seconds; a server hiccup must not produce a popup, or a brief
        // outage would bury the user in error messages.
        if (reply->error() != QNetworkReply::NoError)
            return;

        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();

        QList<Notification> items;
        for (const QJsonValue &v : o.value("items").toArray()) {
            const QJsonObject e = v.toObject();
            Notification n;
            n.id        = e.value("id").toInt();
            n.ticketId  = e.value("ticketId").toInt();
            n.message   = e.value("message").toString();
            n.isRead    = e.value("isRead").toBool();
            n.createdAt = QDateTime::fromString(e.value("createdAt").toString(),
                                                Qt::ISODate);
            items.append(n);
        }

        emit notificationsReceived(items, o.value("unreadCount").toInt());
    });
}

void ApiClient::markNotificationRead(int notificationId)
{
    QNetworkReply *reply = m_network->post(
        buildRequest(QString("/api/notifications/%1/read").arg(notificationId)),
        QByteArray("{}"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            emit notificationsChanged();
    });
}

void ApiClient::markAllNotificationsRead()
{
    QNetworkReply *reply = m_network->post(
        buildRequest("/api/notifications/read-all"), QByteArray("{}"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            emit notificationsChanged();
    });
}


void ApiClient::logout()
{
    m_token.clear();
}

void ApiClient::deleteUser(int userId)
{
    QNetworkReply *reply = m_network->sendCustomRequest(
        buildRequest(QString("/api/admin/users/%1").arg(userId)), "DELETE");

    connect(reply, &QNetworkReply::finished, this, [this, reply, userId]() {
        reply->deleteLater();

        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            const QString detail = o.value("error").toString();

            // 409 means "refused, but there is a sensible alternative".
            // Distinguishing it from a plain error lets the UI offer that
            // alternative instead of just saying no.
            if (http == 409) {
                emit deleteRefused(userId, detail);
                return;
            }

            emit requestFailed(detail.isEmpty()
                ? tr("Could not delete user (HTTP %1).").arg(http)
                : detail);
            return;
        }

        emit adminSaved();
    });
}


// --- Audit log (requirement 21) --------------------------------------------

void ApiClient::fetchAuditLog(int page, const QString &action, const QString &search)
{
    QUrl url(m_baseUrl + "/api/admin/audit");
    QUrlQuery params;
    params.addQueryItem("page", QString::number(page));
    params.addQueryItem("pageSize", "50");

    if (!action.isEmpty())
        params.addQueryItem("action", action);
    if (!search.trimmed().isEmpty())
        params.addQueryItem("search", search.trimmed());

    url.setQuery(params);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());

    QNetworkReply *reply = m_network->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Could not load the audit log: %1")
                                   .arg(reply->errorString()));
            return;
        }

        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();

        AuditPage result;
        result.page       = o.value("page").toInt(1);
        result.totalCount = o.value("totalCount").toInt();
        result.totalPages = o.value("totalPages").toInt(1);

        for (const QJsonValue &v : o.value("items").toArray()) {
            const QJsonObject e = v.toObject();
            AuditEntry a;
            a.id         = e.value("id").toInt();
            a.action     = e.value("action").toString();
            a.entityType = e.value("entityType").toString();
            a.entityId   = e.value("entityId").toInt();
            a.details    = e.value("details").toString();
            a.userName   = e.value("userName").toString();
            a.createdAt  = QDateTime::fromString(e.value("createdAt").toString(),
                                                 Qt::ISODate);
            result.items.append(a);
        }

        emit auditPageReceived(result);
    });
}

void ApiClient::fetchAuditActions()
{
    QNetworkReply *reply = m_network->get(buildRequest("/api/admin/audit/actions"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
            return;

        QStringList actions;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array())
            actions.append(v.toString());

        emit auditActionsReceived(actions);
    });
}


// --- Attachments (requirement 11) ------------------------------------------

void ApiClient::fetchAttachments(int ticketId)
{
    QNetworkReply *reply = m_network->get(
        buildRequest(QString("/api/tickets/%1/attachments").arg(ticketId)));

    connect(reply, &QNetworkReply::finished, this, [this, reply, ticketId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
            return;   // staff on someone else's ticket get 403; not an error

        QList<Attachment> items;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array()) {
            const QJsonObject o = v.toObject();
            Attachment a;
            a.id         = o.value("id").toInt();
            a.name       = o.value("name").toString();
            a.sizeBytes  = static_cast<qint64>(o.value("sizeBytes").toDouble());
            a.mimeType   = o.value("mimeType").toString();
            a.uploadedBy = o.value("uploadedBy").toString();
            a.uploadedAt = QDateTime::fromString(o.value("uploadedAt").toString(),
                                                 Qt::ISODate);
            items.append(a);
        }

        emit attachmentsReceived(ticketId, items);
    });
}

void ApiClient::uploadAttachment(int ticketId, const QString &localFilePath)
{
    auto *file = new QFile(localFilePath);

    if (!file->open(QIODevice::ReadOnly)) {
        emit requestFailed(tr("Could not open %1").arg(localFilePath));
        delete file;
        return;
    }

    const QFileInfo info(localFilePath);

    // Check the size here as well as on the server. The server's limit is the
    // one that counts, but failing locally avoids pushing 200 MB up a slow
    // connection just to be told no.
    if (info.size() > 10 * 1024 * 1024) {
        emit requestFailed(tr("%1 is larger than the 10 MB limit.").arg(info.fileName()));
        file->close();
        delete file;
        return;
    }

    // multipart/form-data: the same encoding a browser uses for a file input.
    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QVariant(QMimeDatabase().mimeTypeForFile(info).name()));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QString("form-data; name=\"file\"; filename=\"%1\"")
                                    .arg(info.fileName())));
    filePart.setBodyDevice(file);

    // The multipart takes ownership of the file, so it is closed and deleted
    // when the multipart is. Forgetting this leaks a file handle per upload.
    file->setParent(multiPart);
    multiPart->append(filePart);

    // Not buildRequest(): that sets Content-Type to application/json, which
    // would break the multipart boundary. Qt sets the correct header itself.
    QNetworkRequest request(QUrl(m_baseUrl
        + QString("/api/tickets/%1/attachments").arg(ticketId)));

    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());

    QNetworkReply *reply = m_network->post(request, multiPart);
    multiPart->setParent(reply);   // freed with the reply

    connect(reply, &QNetworkReply::finished, this, [this, reply, ticketId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            const QString detail = o.value("error").toString();

            emit requestFailed(detail.isEmpty()
                ? tr("Upload failed: %1").arg(reply->errorString())
                : detail);
            return;
        }

        emit attachmentUploaded(ticketId);
    });
}

void ApiClient::downloadAttachment(int attachmentId, const QString &saveToPath)
{
    QNetworkReply *reply = m_network->get(
        buildRequest(QString("/api/attachments/%1").arg(attachmentId)));

    connect(reply, &QNetworkReply::finished, this, [this, reply, saveToPath]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tr("Download failed: %1").arg(reply->errorString()));
            return;
        }

        QFile out(saveToPath);
        if (!out.open(QIODevice::WriteOnly)) {
            emit requestFailed(tr("Could not write to %1").arg(saveToPath));
            return;
        }

        out.write(reply->readAll());
        out.close();

        emit attachmentDownloaded(saveToPath);
    });
}
