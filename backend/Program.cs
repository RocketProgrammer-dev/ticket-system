using System.Security.Claims;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.EntityFrameworkCore;
using Microsoft.IdentityModel.Tokens;
using System.IdentityModel.Tokens.Jwt;
using Microsoft.OpenApi.Models;
using Npgsql;
using TicketApi.Services;
using TicketApi.Data;
using TicketApi.Models;

var builder = WebApplication.CreateBuilder(args);

// ---------------------------------------------------------------------
//  Database
// ---------------------------------------------------------------------
var connString = builder.Configuration.GetConnectionString("Default")!;

// Npgsql needs to be told about the PostgreSQL ENUM types before it can map
// them. This must happen on the data source, not in OnModelCreating.
var dataSourceBuilder = new NpgsqlDataSourceBuilder(connString);
dataSourceBuilder.MapEnum<UserRole>("user_role");
dataSourceBuilder.MapEnum<TicketStatus>("ticket_status");
dataSourceBuilder.MapEnum<TicketPriority>("ticket_priority");
var dataSource = dataSourceBuilder.Build();

// The enums must be registered TWICE, and this trips up almost everyone.
//
// The data source mapping above teaches raw Npgsql how to read and write the
// PostgreSQL enum types. EF Core keeps its own separate type registry, so it
// needs to be told as well — otherwise EF tries to read user_role as an int
// and throws InvalidCastException at query time, not at startup.
builder.Services.AddDbContext<AppDbContext>(o => o
    .UseNpgsql(dataSource, npgsql => {
        npgsql.MapEnum<UserRole>("user_role");
        npgsql.MapEnum<TicketStatus>("ticket_status");
        npgsql.MapEnum<TicketPriority>("ticket_priority");
    }));

// ---------------------------------------------------------------------
//  Authentication (requirements 2, 20)
// ---------------------------------------------------------------------
var jwtKey = builder.Configuration["Jwt:Key"]!;

builder.Services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme)
    .AddJwtBearer(options => {
        options.TokenValidationParameters = new TokenValidationParameters {
            ValidateIssuerSigningKey = true,
            IssuerSigningKey = new SymmetricSecurityKey(Encoding.UTF8.GetBytes(jwtKey)),
            ValidateIssuer   = false,
            ValidateAudience = false,
            ValidateLifetime = true,
            ClockSkew = TimeSpan.FromMinutes(1)
        };
    });

builder.Services.AddAuthorization();

// Requirement 15: runs on a timer inside the API, independent of any client.
builder.Services.AddHostedService<OverdueScanner>();

// Requirement 11. Singleton: it holds only a path and does no per-request state.
builder.Services.AddSingleton<AttachmentStorage>();
builder.Services.AddEndpointsApiExplorer();
// Without this, Swagger renders the endpoints but gives you no way to send a
// token — no Authorize button appears, and every secured call returns 401.
builder.Services.AddSwaggerGen(c => {
    c.AddSecurityDefinition("Bearer", new OpenApiSecurityScheme {
        Description = "Paste the token from /api/auth/login. Just the token; "
                    + "Swagger adds the word Bearer for you.",
        Name         = "Authorization",
        In           = ParameterLocation.Header,
        Type         = SecuritySchemeType.Http,
        Scheme       = "bearer",
        BearerFormat = "JWT"
    });

    // Tells Swagger to attach the token to every request once you authorize.
    c.AddSecurityRequirement(new OpenApiSecurityRequirement {
        {
            new OpenApiSecurityScheme {
                Reference = new OpenApiReference {
                    Type = ReferenceType.SecurityScheme,
                    Id   = "Bearer"
                }
            },
            Array.Empty<string>()
        }
    });
});

var app = builder.Build();

app.UseSwagger();
app.UseSwaggerUI();          // browse to /swagger to try endpoints by hand
app.UseAuthentication();
app.UseAuthorization();


// ---------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------

// Reads the signed-in user out of the token. The client cannot forge these
// values — they were signed by the server at login. This is exactly why the
// Qt client's role dropdown must never be trusted.
static (int userId, UserRole role) CurrentUser(ClaimsPrincipal principal)
{
    var id   = int.Parse(principal.FindFirstValue(ClaimTypes.NameIdentifier)!);
    var role = Enum.Parse<UserRole>(principal.FindFirstValue(ClaimTypes.Role)!);
    return (id, role);
}


// ---------------------------------------------------------------------
//  POST /api/auth/login
// ---------------------------------------------------------------------
app.MapPost("/api/auth/login", async (LoginRequest req, AppDbContext db) => {

    var user = await db.Users
        .FirstOrDefaultAsync(u => u.Username == req.Username && u.IsActive);

    // Same response whether the username is wrong or the password is wrong.
    // Distinguishing them tells an attacker which usernames exist.
    if (user is null || !BCrypt.Net.BCrypt.Verify(req.Password, user.PasswordHash)) {

        // A failed login is the single most useful thing in an audit log:
        // repeated failures against one account are what a break-in attempt
        // looks like. Record the attempted username, never the password.
        db.AuditLogs.Add(new AuditLog {
            UserId     = user?.Id,          // null when the username is unknown
            Action     = "login_failed",
            EntityType = "user",
            EntityId   = user?.Id,
            Details    = JsonSerializer.Serialize(new { attemptedUsername = req.Username }),
            CreatedAt  = DateTime.UtcNow
        });
        await db.SaveChangesAsync();

        return Results.Unauthorized();
    }

    user.LastLoginAt = DateTime.UtcNow;

    // Requirement 21: log the sensitive action.
    db.AuditLogs.Add(new AuditLog {
        UserId = user.Id, Action = "login_success",
        EntityType = "user", EntityId = user.Id, CreatedAt = DateTime.UtcNow
    });
    await db.SaveChangesAsync();

    var claims = new[] {
        new Claim(ClaimTypes.NameIdentifier, user.Id.ToString()),
        new Claim(ClaimTypes.Name, user.Username),
        new Claim(ClaimTypes.Role, user.Role.ToString())
    };

    var creds = new SigningCredentials(
        new SymmetricSecurityKey(Encoding.UTF8.GetBytes(jwtKey)),
        SecurityAlgorithms.HmacSha256);

    var token = new JwtSecurityToken(
        claims: claims,
        expires: DateTime.UtcNow.AddHours(8),
        signingCredentials: creds);

    return Results.Ok(new {
        token    = new JwtSecurityTokenHandler().WriteToken(token),
        username = user.Username,
        fullName = user.FullName,
        role     = user.Role.ToString().ToLowerInvariant()
    });
});


// ---------------------------------------------------------------------
//  GET /api/tickets  — what ApiClient::fetchTickets() calls
// ---------------------------------------------------------------------
app.MapGet("/api/tickets", async (
    ClaimsPrincipal principal,
    AppDbContext db,
    int page = 1,
    int pageSize = 25,
    string? search = null,
    string? status = null,
    string? priority = null,
    bool overdueOnly = false,
    string? sortBy = null,
    bool sortDesc = false) => {

    var (userId, role) = CurrentUser(principal);

    // Requirement 16. Search, filter, sort and page ALL happen here rather
    // than in the client. With a page of 25 rows, filtering client-side would
    // only filter those 25 — the other 900 matching tickets would be invisible
    // and the feature would look broken rather than absent.
    var query = db.Tickets.Include(t => t.AssignedTo)
                          .Include(t => t.CreatedBy)
                          .AsQueryable();

    if (role == UserRole.Staff)
        query = query.Where(t => t.CreatedById == userId);

    if (!string.IsNullOrWhiteSpace(search)) {
        var term = search.Trim().ToLower();
        query = query.Where(t =>
            EF.Functions.Like(t.TicketNumber.ToLower(), $"%{term}%") ||
            EF.Functions.Like(t.Title.ToLower(), $"%{term}%") ||
            (t.AssignedTo != null && EF.Functions.Like(t.AssignedTo.FullName.ToLower(), $"%{term}%")) ||
            (t.CreatedBy  != null && EF.Functions.Like(t.CreatedBy.FullName.ToLower(),  $"%{term}%")));
    }

    if (!string.IsNullOrWhiteSpace(status) &&
        Enum.TryParse<TicketStatus>(status, true, out var statusValue))
        query = query.Where(t => t.Status == statusValue);

    if (!string.IsNullOrWhiteSpace(priority) &&
        Enum.TryParse<TicketPriority>(priority, true, out var priorityValue))
        query = query.Where(t => t.Priority == priorityValue);

    if (overdueOnly) {
        var today = DateOnly.FromDateTime(DateTime.UtcNow);
        query = query.Where(t => t.DueDate != null &&
                                 t.DueDate < today &&
                                 t.Status != TicketStatus.Resolved &&
                                 t.Status != TicketStatus.Closed);
    }

    // Count BEFORE paging: the client needs to know how many matches exist in
    // total, not how many are on this page.
    var totalCount = await query.CountAsync();

    // Sorting must happen in SQL too. Sorting a single page would order 25
    // arbitrary rows rather than picking the top 25 overall.
    query = (sortBy?.ToLower(), sortDesc) switch {
        ("number",   false) => query.OrderBy(t => t.TicketNumber),
        ("number",   true)  => query.OrderByDescending(t => t.TicketNumber),
        ("title",    false) => query.OrderBy(t => t.Title),
        ("title",    true)  => query.OrderByDescending(t => t.Title),
        ("status",   false) => query.OrderBy(t => t.Status),
        ("status",   true)  => query.OrderByDescending(t => t.Status),
        ("priority", false) => query.OrderBy(t => t.Priority),
        ("priority", true)  => query.OrderByDescending(t => t.Priority),
        ("duedate",  false) => query.OrderBy(t => t.DueDate),
        ("duedate",  true)  => query.OrderByDescending(t => t.DueDate),
        ("assignee", false) => query.OrderBy(t => t.AssignedTo!.FullName),
        ("assignee", true)  => query.OrderByDescending(t => t.AssignedTo!.FullName),
        ("createdby", false) => query.OrderBy(t => t.CreatedBy!.FullName),
        ("createdby", true)  => query.OrderByDescending(t => t.CreatedBy!.FullName),
        _ => query.OrderByDescending(t => t.Priority).ThenBy(t => t.DueDate)
    };

    // Clamp rather than trust. A client asking for pageSize=1000000 should get
    // a sane response, not an out-of-memory error on the server.
    pageSize = Math.Clamp(pageSize, 1, 1000);
    page     = Math.Max(page, 1);

    var totalPages = totalCount == 0 ? 1 : (int)Math.Ceiling(totalCount / (double)pageSize);
    page = Math.Min(page, totalPages);

    var rows = await query
        .Skip((page - 1) * pageSize)
        .Take(pageSize)
        .Select(t => new {
            t.Id,
            t.TicketNumber,
            t.Title,
            t.Description,
            t.Status,
            t.Priority,
            Assignee = t.AssignedTo != null ? t.AssignedTo.FullName : "",
            Creator  = t.CreatedBy  != null ? t.CreatedBy.FullName  : "",
            t.DueDate,
            t.CreatedAt
        })
        .ToListAsync();

    return Results.Ok(new {
        page,
        pageSize,
        totalCount,
        totalPages,
        items = rows.Select(t => new {
            id          = t.Id,
            number      = t.TicketNumber,
            title       = t.Title,
            description = t.Description ?? "",
            status      = ApiMap.ToApi(t.Status),
            priority    = ApiMap.ToApi(t.Priority),
            assignee    = t.Assignee,
            createdBy   = t.Creator,
            createdAt   = t.CreatedAt,
            dueDate     = t.DueDate?.ToString("yyyy-MM-dd")
        })
    });
}).RequireAuthorization();


// ---------------------------------------------------------------------
//  GET /api/tickets/{id}/comments  (requirements 9, 10, 20)
// ---------------------------------------------------------------------
app.MapGet("/api/tickets/{id:int}/comments",
    async (int id, ClaimsPrincipal principal, AppDbContext db) => {

    var (_, role) = CurrentUser(principal);

    var query = db.Comments.Include(c => c.Author)
                           .Where(c => c.TicketId == id);

    // THE line that matters for requirement 10. Staff never receive internal
    // notes — not hidden in the UI, simply never sent. Anyone can read your
    // API with curl; only this filter actually protects the data.
    if (role == UserRole.Staff)
        query = query.Where(c => !c.IsInternal);

    var comments = await query
        .OrderBy(c => c.CreatedAt)
        .Select(c => new {
            id         = c.Id,
            ticketId   = c.TicketId,
            author     = c.Author!.FullName,
            text       = c.Body,
            isInternal = c.IsInternal,
            createdAt  = c.CreatedAt
        })
        .ToListAsync();

    return Results.Ok(comments);
}).RequireAuthorization();


// ---------------------------------------------------------------------
//  POST /api/tickets/{id}/comments
// ---------------------------------------------------------------------
app.MapPost("/api/tickets/{id:int}/comments",
    async (int id, CommentRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, role) = CurrentUser(principal);

    if (!await db.Tickets.AnyAsync(t => t.Id == id))
        return Results.NotFound();

    if (string.IsNullOrWhiteSpace(req.Text))
        return Results.BadRequest(new { error = "Comment text is required." });

    // Staff cannot post internal notes, whatever the client sends. The hidden
    // checkbox in the Qt dialog is a convenience; this is the control.
    if (req.IsInternal && role == UserRole.Staff)
        return Results.Forbid();

    var comment = new TicketComment {
        TicketId   = id,
        AuthorId   = userId,
        Body       = req.Text.Trim(),
        IsInternal = req.IsInternal,
        CreatedAt  = DateTime.UtcNow
    };

    db.Comments.Add(comment);

    // Notify the other interested parties. An internal note must never reach
    // the requester — that would leak through the notification channel what
    // requirement 10 hides in the UI.
    var t = await db.Tickets.FirstAsync(x => x.Id == id);

    var recipients = new List<int>();
    if (t.AssignedToId is not null && t.AssignedToId != userId)
        recipients.Add(t.AssignedToId.Value);
    if (!req.IsInternal && t.CreatedById != userId)
        recipients.Add(t.CreatedById);

    foreach (var recipientId in recipients.Distinct()) {
        db.Notifications.Add(new Notification {
            UserId    = recipientId,
            TicketId  = id,
            Message   = $"New comment on {t.TicketNumber}: {t.Title}",
            IsRead    = false,
            CreatedAt = DateTime.UtcNow
        });
    }

    await db.SaveChangesAsync();

    return Results.Created($"/api/tickets/{id}/comments/{comment.Id}",
                           new { id = comment.Id });
}).RequireAuthorization();


// ---------------------------------------------------------------------
//  POST /api/tickets  (requirement 4)
// ---------------------------------------------------------------------
app.MapPost("/api/tickets",
    async (CreateTicketRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, _) = CurrentUser(principal);

    if (string.IsNullOrWhiteSpace(req.Title))
        return Results.BadRequest(new { error = "Title is required." });

    var ticket = new Ticket {
        Title        = req.Title.Trim(),
        Description  = req.Description,
        Status       = TicketStatus.Open,
        Priority     = Enum.Parse<TicketPriority>(req.Priority ?? "Normal", true),
        CreatedById  = userId,
        CategoryId   = req.CategoryId,
        DueDate      = req.DueDate,
        CreatedAt    = DateTime.UtcNow,
        UpdatedAt    = DateTime.UtcNow
        // TicketNumber deliberately unset — the database trigger generates it.
    };

    db.Tickets.Add(ticket);

    // Saved once here so the trigger assigns a ticket number. The notification
    // message quotes that number, so it cannot be written before this point.
    await db.SaveChangesAsync();

    // Requirement 14: managers are told about every new ticket, so nothing
    // sits unassigned because nobody noticed it. Skip the creator — a manager
    // opening their own ticket does not need to be told they did.
    var managerIds = await db.Users
        .Where(u => u.Role == UserRole.Manager && u.IsActive && u.Id != userId)
        .Select(u => u.Id)
        .ToListAsync();

    foreach (var managerId in managerIds) {
        db.Notifications.Add(new Notification {
            UserId    = managerId,
            TicketId  = ticket.Id,
            Message   = $"New {ApiMap.ToApi(ticket.Priority)} ticket "
                      + $"{ticket.TicketNumber}: {ticket.Title}",
            IsRead    = false,
            CreatedAt = DateTime.UtcNow
        });
    }

    db.AuditLogs.Add(new AuditLog {
        UserId     = userId,
        Action     = "ticket_created",
        EntityType = "ticket",
        EntityId   = ticket.Id,
        Details    = JsonSerializer.Serialize(new {
            number   = ticket.TicketNumber,
            priority = ApiMap.ToApi(ticket.Priority)
        }),
        CreatedAt  = DateTime.UtcNow
    });

    await db.SaveChangesAsync();

    return Results.Created($"/api/tickets/{ticket.Id}",
                           new { id = ticket.Id, number = ticket.TicketNumber });
}).RequireAuthorization();


// ---------------------------------------------------------------------
//  GET /api/users  — populates the "assign to" dropdown (requirement 6)
// ---------------------------------------------------------------------
app.MapGet("/api/users", async (ClaimsPrincipal principal, AppDbContext db) => {

    var (_, role) = CurrentUser(principal);

    // Only people who can assign tickets need the staff directory. Returning
    // it to everyone would leak the org chart for no reason.
    if (role == UserRole.Staff)
        return Results.Forbid();

    var users = await db.Users
        .Where(u => u.IsActive && u.Role != UserRole.Staff)
        .OrderBy(u => u.FullName)
        .Select(u => new { u.Id, u.FullName, u.Role })
        .ToListAsync();

    return Results.Ok(users.Select(u => new {
        id       = u.Id,
        fullName = u.FullName,
        role     = u.Role.ToString().ToLowerInvariant()
    }));
}).RequireAuthorization();


// ---------------------------------------------------------------------
//  PUT /api/tickets/{id}  — requirements 6, 7 and 13
// ---------------------------------------------------------------------
app.MapPut("/api/tickets/{id:int}",
    async (int id, UpdateTicketRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, role) = CurrentUser(principal);

    // Requirement 20 again: the client hides these controls from staff, but
    // this is the check that actually stops them.
    if (!(role == UserRole.Technician || role == UserRole.Manager))
        return Results.Forbid();

    var ticket = await db.Tickets.FirstOrDefaultAsync(t => t.Id == id);
    if (ticket is null)
        return Results.NotFound();

    // Requirement 13: record every change BEFORE applying it, while the old
    // value is still available. One row per changed field.
    void Track(string field, string? oldValue, string? newValue)
    {
        if (oldValue == newValue)
            return;   // nothing changed, don't write a noise row

        db.History.Add(new TicketHistory {
            TicketId    = id,
            ChangedById = userId,
            FieldName   = field,
            OldValue    = oldValue,
            NewValue    = newValue,
            ChangedAt   = DateTime.UtcNow
        });
    }

    if (req.Status is not null) {
        var newStatus = Enum.Parse<TicketStatus>(req.Status, true);
        Track("status", ApiMap.ToApi(ticket.Status), ApiMap.ToApi(newStatus));

        // Stamp the closing time when a ticket reaches a terminal state, and
        // clear it if the ticket is reopened.
        if (newStatus is TicketStatus.Resolved or TicketStatus.Closed) {
            if (ticket.ClosedAt is null)
                ticket.ClosedAt = DateTime.UtcNow;
        } else {
            ticket.ClosedAt = null;
        }

        ticket.Status = newStatus;
    }

    if (req.Priority is not null) {
        var newPriority = Enum.Parse<TicketPriority>(req.Priority, true);
        Track("priority", ApiMap.ToApi(ticket.Priority), ApiMap.ToApi(newPriority));
        ticket.Priority = newPriority;
    }

    // AssignedToId is nullable AND optional, which are different things:
    // "not present in the request" means leave it alone, while an explicit
    // null means unassign. HasAssignee distinguishes them.
    if (req.HasAssignee) {

        // Reject an assignment to someone who cannot work tickets, rather
        // than trusting whatever id the client sent.
        if (req.AssignedToId is not null) {
            var valid = await db.Users.AnyAsync(u =>
                u.Id == req.AssignedToId &&
                u.IsActive &&
                (u.Role == UserRole.Technician || u.Role == UserRole.Manager));

            if (!valid)
                return Results.BadRequest(new { error = "Invalid assignee." });
        }

        // Store NAMES in the history, not ids. "assigned_to: 2 -> 3" is
        // meaningless to whoever reads the audit trail later; the whole point
        // of requirement 13 is that a human can follow what happened.
        // Ids are for joins, labels are for history.
        var unassigned = "Unassigned";

        var oldName = ticket.AssignedToId is null
            ? unassigned
            : await db.Users.Where(u => u.Id == ticket.AssignedToId)
                            .Select(u => u.FullName)
                            .FirstOrDefaultAsync() ?? unassigned;

        var newName = req.AssignedToId is null
            ? unassigned
            : await db.Users.Where(u => u.Id == req.AssignedToId)
                            .Select(u => u.FullName)
                            .FirstOrDefaultAsync() ?? unassigned;

        Track("assigned_to", oldName, newName);
        ticket.AssignedToId = req.AssignedToId;
    }

    // Requirement 14: tell people when something happens TO them. Never
    // notify the person who made the change — they already know.
    if (req.HasAssignee && req.AssignedToId is not null && req.AssignedToId != userId) {
        db.Notifications.Add(new Notification {
            UserId    = req.AssignedToId.Value,
            TicketId  = id,
            Message   = $"{ticket.TicketNumber} was assigned to you: {ticket.Title}",
            IsRead    = false,
            CreatedAt = DateTime.UtcNow
        });
    }

    if (req.Status is not null && ticket.CreatedById != userId) {
        db.Notifications.Add(new Notification {
            UserId    = ticket.CreatedById,
            TicketId  = id,
            Message   = $"{ticket.TicketNumber} is now {ApiMap.ToApi(ticket.Status)}",
            IsRead    = false,
            CreatedAt = DateTime.UtcNow
        });
    }

    ticket.UpdatedAt = DateTime.UtcNow;

    // Requirement 21: assignment and status changes are worth auditing.
    db.AuditLogs.Add(new AuditLog {
        UserId = userId, Action = "ticket_updated",
        EntityType = "ticket", EntityId = id, CreatedAt = DateTime.UtcNow
    });

    await db.SaveChangesAsync();
    return Results.Ok(new { id });
}).RequireAuthorization();


// ---------------------------------------------------------------------
//  GET /api/tickets/{id}/history  (requirement 13, read side)
// ---------------------------------------------------------------------
app.MapGet("/api/tickets/{id:int}/history", async (int id, AppDbContext db) => {

    var rows = await db.History
        .Where(h => h.TicketId == id)
        .OrderByDescending(h => h.ChangedAt)
        .Join(db.Users, h => h.ChangedById, u => u.Id, (h, u) => new {
            h.FieldName, h.OldValue, h.NewValue, h.ChangedAt, u.FullName
        })
        .ToListAsync();

    return Results.Ok(rows.Select(r => new {
        field     = r.FieldName,
        oldValue  = r.OldValue ?? "",
        newValue  = r.NewValue ?? "",
        changedBy = r.FullName,
        changedAt = r.ChangedAt
    }));
}).RequireAuthorization();


// ---------------------------------------------------------------------
//  GET /api/dashboard  (requirement 17)
// ---------------------------------------------------------------------
app.MapGet("/api/dashboard", async (ClaimsPrincipal principal, AppDbContext db) => {

    var (_, role) = CurrentUser(principal);

    if (role != UserRole.Manager)
        return Results.Forbid();

    var today = DateOnly.FromDateTime(DateTime.UtcNow);

    // GroupBy runs as SQL here, so the database does the counting and returns
    // a handful of rows instead of every ticket.
    var byStatus = await db.Tickets
        .GroupBy(t => t.Status)
        .Select(g => new { Status = g.Key, Count = g.Count() })
        .ToListAsync();

    var byPriority = await db.Tickets
        .GroupBy(t => t.Priority)
        .Select(g => new { Priority = g.Key, Count = g.Count() })
        .ToListAsync();

    var overdue = await db.Tickets.CountAsync(t =>
        t.DueDate != null &&
        t.DueDate < today &&
        t.Status != TicketStatus.Resolved &&
        t.Status != TicketStatus.Closed);

    var unassigned = await db.Tickets.CountAsync(t =>
        t.AssignedToId == null &&
        t.Status != TicketStatus.Closed);

    var workload = await db.Tickets
        .Where(t => t.AssignedToId != null &&
                    t.Status != TicketStatus.Resolved &&
                    t.Status != TicketStatus.Closed)
        .GroupBy(t => t.AssignedTo!.FullName)
        .Select(g => new { Name = g.Key, Count = g.Count() })
        .OrderByDescending(x => x.Count)
        .ToListAsync();

    int CountFor(TicketStatus s) =>
        byStatus.FirstOrDefault(x => x.Status == s)?.Count ?? 0;
    int CountForP(TicketPriority p) =>
        byPriority.FirstOrDefault(x => x.Priority == p)?.Count ?? 0;

    return Results.Ok(new {
        total      = byStatus.Sum(x => x.Count),
        open       = CountFor(TicketStatus.Open),
        inProgress = CountFor(TicketStatus.InProgress),
        waiting    = CountFor(TicketStatus.Waiting),
        resolved   = CountFor(TicketStatus.Resolved),
        closed     = CountFor(TicketStatus.Closed),
        overdue,
        unassigned,
        low        = CountForP(TicketPriority.Low),
        normal     = CountForP(TicketPriority.Normal),
        high       = CountForP(TicketPriority.High),
        critical   = CountForP(TicketPriority.Critical),
        workload   = workload.Select(w => new { name = w.Name, count = w.Count })
    });
}).RequireAuthorization();

// =====================================================================
//  ADMINISTRATION (requirement 19)
//  Every endpoint below is manager-only and writes an audit_log row,
//  because these are exactly the "critical operations" requirement 21
//  is about: creating accounts and changing who can do what.
// =====================================================================

// Small helper so the manager check isn't copy-pasted eight times.
static bool IsManager(ClaimsPrincipal p) =>
    p.FindFirstValue(ClaimTypes.Role) == nameof(UserRole.Manager);


// --- Users -----------------------------------------------------------
app.MapGet("/api/admin/users", async (ClaimsPrincipal principal, AppDbContext db,
                                      bool includeInactive = false,
                                      string? search = null) => {

    if (!IsManager(principal)) return Results.Forbid();

    var query = db.Users.AsQueryable();

    // Deactivated accounts are history, not staff. After a few years they
    // outnumber current employees, so they are hidden unless asked for —
    // the rows stay in the database, they just leave the list.
    if (!includeInactive)
        query = query.Where(u => u.IsActive);

    if (!string.IsNullOrWhiteSpace(search)) {
        var term = search.Trim().ToLower();

        // EF.Functions.Like maps to SQL LIKE, so the database does the
        // filtering. Doing it in C# would mean fetching every user first,
        // which is exactly the problem this is meant to avoid.
        query = query.Where(u =>
            EF.Functions.Like(u.FullName.ToLower(), $"%{term}%") ||
            EF.Functions.Like(u.Username.ToLower(), $"%{term}%") ||
            (u.Email != null && EF.Functions.Like(u.Email.ToLower(), $"%{term}%")));
    }

    // A hard cap so a careless client cannot pull 50,000 rows. If a search
    // hits the limit, the answer is a narrower search, not a bigger response.
    var users = await query.OrderBy(u => u.FullName).Take(200).ToListAsync();

    // Note what is NOT returned: password_hash never leaves the server, not
    // even to a manager. There is no legitimate use for it in the client.
    return Results.Ok(users.Select(u => new {
        id           = u.Id,
        username     = u.Username,
        fullName     = u.FullName,
        email        = u.Email ?? "",
        role         = u.Role.ToString().ToLowerInvariant(),
        departmentId = u.DepartmentId ?? 0,
        isActive     = u.IsActive
    }));
}).RequireAuthorization();


app.MapPost("/api/admin/users",
    async (SaveUserRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    if (!IsManager(principal)) return Results.Forbid();

    if (string.IsNullOrWhiteSpace(req.Username) || string.IsNullOrWhiteSpace(req.FullName))
        return Results.BadRequest(new { error = "Username and full name are required." });

    if (string.IsNullOrWhiteSpace(req.Password) || req.Password.Length < 8)
        return Results.BadRequest(new { error = "Password must be at least 8 characters." });

    if (await db.Users.AnyAsync(u => u.Username == req.Username))
        return Results.Conflict(new { error = "That username is already taken." });

    var user = new User {
        Username     = req.Username.Trim(),
        PasswordHash = BCrypt.Net.BCrypt.HashPassword(req.Password),
        FullName     = req.FullName.Trim(),
        Email        = string.IsNullOrWhiteSpace(req.Email) ? null : req.Email.Trim(),
        Role         = Enum.Parse<UserRole>(req.Role ?? "Staff", true),
        DepartmentId = req.DepartmentId > 0 ? req.DepartmentId : null,
        IsActive     = true,
        CreatedAt    = DateTime.UtcNow
    };

    db.Users.Add(user);
    await db.SaveChangesAsync();

    db.AuditLogs.Add(new AuditLog {
        UserId = int.Parse(principal.FindFirstValue(ClaimTypes.NameIdentifier)!),
        Action = "user_created", EntityType = "user", EntityId = user.Id,
        CreatedAt = DateTime.UtcNow
    });
    await db.SaveChangesAsync();

    return Results.Created($"/api/admin/users/{user.Id}", new { id = user.Id });
}).RequireAuthorization();


app.MapPut("/api/admin/users/{id:int}",
    async (int id, SaveUserRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    if (!IsManager(principal)) return Results.Forbid();

    var actorId = int.Parse(principal.FindFirstValue(ClaimTypes.NameIdentifier)!);
    var user = await db.Users.FirstOrDefaultAsync(u => u.Id == id);
    if (user is null) return Results.NotFound();

    var newRole = Enum.Parse<UserRole>(req.Role ?? user.Role.ToString(), true);

    // Guard against a manager removing their own access and locking everyone
    // out of administration. Cheap to check, awkward to recover from.
    if (user.Id == actorId && (newRole != UserRole.Manager || !req.IsActive))
        return Results.BadRequest(new {
            error = "You cannot remove your own manager role or deactivate yourself."
        });

    user.FullName     = req.FullName.Trim();
    user.Email        = string.IsNullOrWhiteSpace(req.Email) ? null : req.Email.Trim();
    user.Role         = newRole;
    user.DepartmentId = req.DepartmentId > 0 ? req.DepartmentId : null;
    user.IsActive     = req.IsActive;

    // An empty password means "leave it alone" — the client sends the field
    // blank unless the manager is deliberately resetting it.
    if (!string.IsNullOrWhiteSpace(req.Password)) {
        if (req.Password.Length < 8)
            return Results.BadRequest(new { error = "Password must be at least 8 characters." });
        user.PasswordHash = BCrypt.Net.BCrypt.HashPassword(req.Password);
    }

    db.AuditLogs.Add(new AuditLog {
        UserId     = actorId,
        Action     = "user_updated",
        EntityType = "user",
        EntityId   = id,
        // Record what actually changed. "user_updated" alone tells you
        // nothing; a role going from staff to manager is the row someone
        // will want to find six months from now.
        Details    = JsonSerializer.Serialize(new {
            username        = user.Username,
            role            = newRole.ToString().ToLowerInvariant(),
            isActive        = req.IsActive,
            passwordChanged = !string.IsNullOrWhiteSpace(req.Password)
        }),
        CreatedAt  = DateTime.UtcNow
    });

    await db.SaveChangesAsync();
    return Results.Ok(new { id });
}).RequireAuthorization();


// --- Departments -----------------------------------------------------
app.MapGet("/api/admin/departments", async (AppDbContext db) => {
    var rows = await db.Departments.OrderBy(d => d.Name).ToListAsync();
    return Results.Ok(rows.Select(d => new {
        id = d.Id, name = d.Name, isActive = d.IsActive
    }));
}).RequireAuthorization();


app.MapPost("/api/admin/departments",
    async (SaveNamedRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    if (!IsManager(principal)) return Results.Forbid();

    if (string.IsNullOrWhiteSpace(req.Name))
        return Results.BadRequest(new { error = "Name is required." });

    var dept = new Department {
        Name = req.Name.Trim(), IsActive = true, CreatedAt = DateTime.UtcNow
    };
    db.Departments.Add(dept);
    await db.SaveChangesAsync();

    return Results.Created($"/api/admin/departments/{dept.Id}", new { id = dept.Id });
}).RequireAuthorization();


app.MapPut("/api/admin/departments/{id:int}",
    async (int id, SaveNamedRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    if (!IsManager(principal)) return Results.Forbid();

    var dept = await db.Departments.FirstOrDefaultAsync(d => d.Id == id);
    if (dept is null) return Results.NotFound();

    dept.Name     = req.Name.Trim();
    dept.IsActive = req.IsActive;

    await db.SaveChangesAsync();
    return Results.Ok(new { id });
}).RequireAuthorization();


// --- Categories ------------------------------------------------------
app.MapGet("/api/admin/categories", async (AppDbContext db) => {
    var rows = await db.Categories.Include(c => c.Department)
                                  .OrderBy(c => c.Name).ToListAsync();
    return Results.Ok(rows.Select(c => new {
        id             = c.Id,
        name           = c.Name,
        departmentId   = c.DepartmentId ?? 0,
        departmentName = c.Department != null ? c.Department.Name : "",
        isActive       = c.IsActive
    }));
}).RequireAuthorization();


app.MapPost("/api/admin/categories",
    async (SaveCategoryRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    if (!IsManager(principal)) return Results.Forbid();

    if (string.IsNullOrWhiteSpace(req.Name))
        return Results.BadRequest(new { error = "Name is required." });

    var cat = new Category {
        Name         = req.Name.Trim(),
        DepartmentId = req.DepartmentId > 0 ? req.DepartmentId : null,
        IsActive     = true,
        CreatedAt    = DateTime.UtcNow
    };
    db.Categories.Add(cat);
    await db.SaveChangesAsync();

    return Results.Created($"/api/admin/categories/{cat.Id}", new { id = cat.Id });
}).RequireAuthorization();


app.MapPut("/api/admin/categories/{id:int}",
    async (int id, SaveCategoryRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    if (!IsManager(principal)) return Results.Forbid();

    var cat = await db.Categories.FirstOrDefaultAsync(c => c.Id == id);
    if (cat is null) return Results.NotFound();

    cat.Name         = req.Name.Trim();
    cat.DepartmentId = req.DepartmentId > 0 ? req.DepartmentId : null;
    cat.IsActive     = req.IsActive;

    await db.SaveChangesAsync();
    return Results.Ok(new { id });
}).RequireAuthorization();

// ---------------------------------------------------------------------
//  Time tracking (requirement 12)
// ---------------------------------------------------------------------
app.MapGet("/api/tickets/{id:int}/time-entries",
    async (int id, ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, role) = CurrentUser(principal);

    // Time logs reveal how long staff spend on things, which is management
    // information rather than requester information. Staff see the total on
    // their own tickets but not who spent what.
    var entries = await db.TimeEntries
        .Include(t => t.User)
        .Where(t => t.TicketId == id)
        .OrderByDescending(t => t.WorkDate)
        .ToListAsync();

    var total = entries.Sum(t => t.Minutes);

    if (role == UserRole.Staff)
        return Results.Ok(new { totalMinutes = total, entries = Array.Empty<object>() });

    return Results.Ok(new {
        totalMinutes = total,
        entries = entries.Select(t => new {
            id       = t.Id,
            userName = t.User!.FullName,
            minutes  = t.Minutes,
            workDate = t.WorkDate.ToString("yyyy-MM-dd"),
            note     = t.Note ?? ""
        })
    });
}).RequireAuthorization();


app.MapPost("/api/tickets/{id:int}/time-entries",
    async (int id, TimeEntryRequest req, ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, role) = CurrentUser(principal);

    // Only the people who actually work tickets can log time against them.
    if (!(role == UserRole.Technician || role == UserRole.Manager))
        return Results.Forbid();

    if (!await db.Tickets.AnyAsync(t => t.Id == id))
        return Results.NotFound();

    // The database has CHECK (minutes > 0); this gives a readable message
    // instead of a constraint violation surfacing as a 500.
    if (req.Minutes <= 0)
        return Results.BadRequest(new { error = "Minutes must be greater than zero." });

    if (req.Minutes > 24 * 60)
        return Results.BadRequest(new { error = "A single entry cannot exceed 24 hours." });

    var workDate = req.WorkDate ?? DateOnly.FromDateTime(DateTime.UtcNow);

    if (workDate > DateOnly.FromDateTime(DateTime.UtcNow))
        return Results.BadRequest(new { error = "Work date cannot be in the future." });

    db.TimeEntries.Add(new TimeEntry {
        TicketId  = id,
        UserId    = userId,
        Minutes   = req.Minutes,
        WorkDate  = workDate,
        Note      = string.IsNullOrWhiteSpace(req.Note) ? null : req.Note.Trim(),
        CreatedAt = DateTime.UtcNow
    });

    await db.SaveChangesAsync();
    return Results.Ok(new { ticketId = id });
}).RequireAuthorization();

// ---------------------------------------------------------------------
//  Notifications (requirement 14)
// ---------------------------------------------------------------------
app.MapGet("/api/notifications", async (ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, _) = CurrentUser(principal);

    // Only this user's notifications, always. There is no query parameter to
    // request someone else's, because there is no reason for one to exist.
    var items = await db.Notifications
        .Where(n => n.UserId == userId)
        .OrderByDescending(n => n.CreatedAt)
        .Take(50)
        .ToListAsync();

    return Results.Ok(new {
        unreadCount = items.Count(n => !n.IsRead),
        items = items.Select(n => new {
            id        = n.Id,
            ticketId  = n.TicketId ?? 0,
            message   = n.Message,
            isRead    = n.IsRead,
            createdAt = n.CreatedAt
        })
    });
}).RequireAuthorization();


app.MapPost("/api/notifications/{id:int}/read",
    async (int id, ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, _) = CurrentUser(principal);

    // Match on user id as well as notification id: without it, anyone could
    // mark anyone else's notifications read by guessing numbers.
    var n = await db.Notifications
        .FirstOrDefaultAsync(x => x.Id == id && x.UserId == userId);

    if (n is null) return Results.NotFound();

    n.IsRead = true;
    await db.SaveChangesAsync();
    return Results.Ok(new { id });
}).RequireAuthorization();


app.MapPost("/api/notifications/read-all",
    async (ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, _) = CurrentUser(principal);

    // ExecuteUpdateAsync runs a single UPDATE statement instead of loading
    // every row into memory and saving them back one by one.
    var count = await db.Notifications
        .Where(n => n.UserId == userId && !n.IsRead)
        .ExecuteUpdateAsync(s => s.SetProperty(n => n.IsRead, true));

    return Results.Ok(new { updated = count });
}).RequireAuthorization();

app.MapDelete("/api/admin/users/{id:int}",
    async (int id, ClaimsPrincipal principal, AppDbContext db) => {

    if (!IsManager(principal)) return Results.Forbid();

    var actorId = int.Parse(principal.FindFirstValue(ClaimTypes.NameIdentifier)!);

    if (id == actorId)
        return Results.BadRequest(new { error = "You cannot delete your own account." });

    var user = await db.Users.FirstOrDefaultAsync(u => u.Id == id);
    if (user is null) return Results.NotFound();

    // Never leave the system with no way back in. If this is the last active
    // manager, deleting them locks everyone out of administration permanently.
    if (user.Role == UserRole.Manager) {
        var otherManagers = await db.Users.CountAsync(u =>
            u.Role == UserRole.Manager && u.IsActive && u.Id != id);

        if (otherManagers == 0)
            return Results.BadRequest(new { error = "This is the last active manager." });
    }

    // A user referenced by tickets, comments or time entries cannot be
    // deleted: the schema says ON DELETE RESTRICT, and rightly so. Removing
    // them would tear holes in the audit trail requirement 21 exists to keep.
    // Deactivating preserves the history while revoking access, which is what
    // "removing an employee" actually means in a system of record.
    var hasTickets  = await db.Tickets.AnyAsync(t => t.CreatedById == id || t.AssignedToId == id);
    var hasComments = await db.Comments.AnyAsync(c => c.AuthorId == id);
    var hasTime     = await db.TimeEntries.AnyAsync(t => t.UserId == id);

    if (hasTickets || hasComments || hasTime) {
        return Results.Conflict(new {
            error = "This user has tickets or comments and cannot be deleted. "
                  + "Deactivate the account instead — their history stays intact "
                  + "and they can no longer sign in.",
            canDeactivate = true
        });
    }

    db.Users.Remove(user);

    db.AuditLogs.Add(new AuditLog {
        UserId = actorId, Action = "user_deleted",
        EntityType = "user", EntityId = id, CreatedAt = DateTime.UtcNow
    });

    await db.SaveChangesAsync();
    return Results.Ok(new { id });
}).RequireAuthorization();

// ---------------------------------------------------------------------
//  GET /api/admin/audit  (requirement 21, read side)
// ---------------------------------------------------------------------
app.MapGet("/api/admin/audit", async (
    ClaimsPrincipal principal,
    AppDbContext db,
    int page = 1,
    int pageSize = 50,
    string? action = null,
    string? search = null) => {

    // Managers only. An audit log shows who did what and when, which is
    // exactly the information an attacker would use to find a target.
    if (!IsManager(principal)) return Results.Forbid();

    var query = db.AuditLogs.Include(a => a.User).AsQueryable();

    if (!string.IsNullOrWhiteSpace(action))
        query = query.Where(a => a.Action == action);

    if (!string.IsNullOrWhiteSpace(search)) {
        var term = search.Trim().ToLower();
        query = query.Where(a =>
            (a.User != null && EF.Functions.Like(a.User.FullName.ToLower(), $"%{term}%")) ||
            EF.Functions.Like(a.Action.ToLower(), $"%{term}%"));
    }

    var totalCount = await query.CountAsync();

    pageSize = Math.Clamp(pageSize, 1, 500);
    page     = Math.Max(page, 1);

    var totalPages = totalCount == 0 ? 1 : (int)Math.Ceiling(totalCount / (double)pageSize);
    page = Math.Min(page, totalPages);

    var rows = await query
        .OrderByDescending(a => a.CreatedAt)   // newest first: that is what you look for
        .Skip((page - 1) * pageSize)
        .Take(pageSize)
        .Select(a => new {
            a.Id,
            a.Action,
            a.EntityType,
            a.EntityId,
            a.Details,
            a.CreatedAt,
            UserName = a.User != null ? a.User.FullName : null
        })
        .ToListAsync();

    return Results.Ok(new {
        page, pageSize, totalCount, totalPages,
        items = rows.Select(a => new {
            id         = a.Id,
            action     = a.Action,
            entityType = a.EntityType ?? "",
            entityId   = a.EntityId ?? 0,
            details    = a.Details ?? "",
            createdAt  = a.CreatedAt,
            // "(system)" rather than blank: a null user means the action had
            // no authenticated actor, which is information, not an absence.
            userName   = a.UserName ?? "(unknown)"
        })
    });
}).RequireAuthorization();


// Distinct action names, so the filter dropdown lists what actually exists
// rather than a hardcoded set that drifts out of date.
app.MapGet("/api/admin/audit/actions", async (ClaimsPrincipal principal, AppDbContext db) => {

    if (!IsManager(principal)) return Results.Forbid();

    var actions = await db.AuditLogs
        .Select(a => a.Action)
        .Distinct()
        .OrderBy(a => a)
        .ToListAsync();

    return Results.Ok(actions);
}).RequireAuthorization();

// ---------------------------------------------------------------------
//  Attachments (requirement 11)
// ---------------------------------------------------------------------

// Can this user see this ticket? Download and list both need the answer, and
// duplicating the rule is how the two drift apart.
static async Task<bool> CanSeeTicketAsync(AppDbContext db, int ticketId,
                                          int userId, UserRole role)
{
    var ticket = await db.Tickets.FirstOrDefaultAsync(t => t.Id == ticketId);
    if (ticket is null) return false;

    return role != UserRole.Staff || ticket.CreatedById == userId;
}


app.MapGet("/api/tickets/{id:int}/attachments",
    async (int id, ClaimsPrincipal principal, AppDbContext db) => {

    var (userId, role) = CurrentUser(principal);

    if (!await CanSeeTicketAsync(db, id, userId, role))
        return Results.Forbid();

    var rows = await db.Attachments
        .Include(a => a.UploadedBy)
        .Where(a => a.TicketId == id)
        .OrderBy(a => a.UploadedAt)
        .ToListAsync();

    return Results.Ok(rows.Select(a => new {
        id         = a.Id,
        name       = a.OriginalName,
        sizeBytes  = a.SizeBytes,
        mimeType   = a.MimeType ?? "",
        uploadedBy = a.UploadedBy != null ? a.UploadedBy.FullName : "",
        uploadedAt = a.UploadedAt
    }));
}).RequireAuthorization();


app.MapPost("/api/tickets/{id:int}/attachments",
    async (int id, HttpRequest request, ClaimsPrincipal principal,
           AppDbContext db, AttachmentStorage storage) => {

    var (userId, role) = CurrentUser(principal);

    if (!await CanSeeTicketAsync(db, id, userId, role))
        return Results.Forbid();

    if (!request.HasFormContentType)
        return Results.BadRequest(new { error = "Expected a multipart form upload." });

    var form = await request.ReadFormAsync();
    var file = form.Files.FirstOrDefault();

    if (file is null || file.Length == 0)
        return Results.BadRequest(new { error = "No file was uploaded." });

    if (file.Length > AttachmentStorage.MaxFileSizeBytes)
        return Results.BadRequest(new {
            error = $"Files must be under {AttachmentStorage.MaxFileSizeBytes / (1024 * 1024)} MB."
        });

    if (!AttachmentStorage.IsAllowed(file.FileName, out var reason))
        return Results.BadRequest(new { error = reason });

    // Path.GetFileName strips any directory portion the client sent, so a
    // "filename" of "../../secrets.txt" becomes "secrets.txt" before it is
    // ever stored as a display name.
    var displayName = Path.GetFileName(file.FileName);

    await using var stream = file.OpenReadStream();
    var storedPath = await storage.SaveAsync(stream, displayName);

    var attachment = new Attachment {
        TicketId     = id,
        OriginalName = displayName,
        StoredPath   = storedPath,
        MimeType     = file.ContentType,
        SizeBytes    = file.Length,
        UploadedById = userId,
        UploadedAt   = DateTime.UtcNow
    };

    db.Attachments.Add(attachment);

    db.AuditLogs.Add(new AuditLog {
        UserId     = userId,
        Action     = "attachment_uploaded",
        EntityType = "ticket",
        EntityId   = id,
        Details    = JsonSerializer.Serialize(new { name = displayName, size = file.Length }),
        CreatedAt  = DateTime.UtcNow
    });

    await db.SaveChangesAsync();

    return Results.Ok(new { id = attachment.Id, name = displayName });

}).RequireAuthorization().DisableAntiforgery();


app.MapGet("/api/attachments/{id:int}",
    async (int id, ClaimsPrincipal principal, AppDbContext db,
           AttachmentStorage storage) => {

    var (userId, role) = CurrentUser(principal);

    var attachment = await db.Attachments.FirstOrDefaultAsync(a => a.Id == id);
    if (attachment is null) return Results.NotFound();

    // The permission check is on the TICKET, not the attachment. Otherwise a
    // staff user could download any file by guessing ids — the files are not
    // secret because their URLs are hard to guess, they are protected because
    // this line runs.
    if (attachment.TicketId is not null &&
        !await CanSeeTicketAsync(db, attachment.TicketId.Value, userId, role))
        return Results.Forbid();

    if (!storage.TryResolve(attachment.StoredPath, out var fullPath))
        return Results.NotFound();

    // "application/octet-stream" plus a filename forces a download rather
    // than letting the browser render it. An HTML or SVG file served inline
    // from your own domain is a stored XSS.
    return Results.File(fullPath, "application/octet-stream", attachment.OriginalName);

}).RequireAuthorization();


app.Run();


// Request bodies. Records because they're immutable data with no behaviour —
// less code than a class and they compare by value.
// A real static class rather than local functions. Top-level statements turn
// "static" methods into LOCAL functions, and local functions cannot be
// overloaded — which is what caused the CS0128 error: two methods named
// ToApiString in one scope. Inside a class, overloading works normally.
static class ApiMap
{
    public static string ToApi(TicketStatus s) => s switch {
        TicketStatus.Open       => "open",
        TicketStatus.InProgress => "in_progress",
        TicketStatus.Waiting    => "waiting",
        TicketStatus.Resolved   => "resolved",
        _                       => "closed"
    };

    public static string ToApi(TicketPriority p) => p switch {
        TicketPriority.Low    => "low",
        TicketPriority.Normal => "normal",
        TicketPriority.High   => "high",
        _                     => "critical"
    };
}

record LoginRequest(string Username, string Password);
record CommentRequest(string Text, bool IsInternal);

// HasAssignee separates "leave the assignee alone" from "set it to nobody".
// Without it, a request that omits the field would be indistinguishable from
// one that explicitly unassigns.
record UpdateTicketRequest(string? Status, string? Priority,
                           int? AssignedToId, bool HasAssignee = false);

// Password is optional on update: blank means "keep the existing one".
record SaveUserRequest(string Username, string FullName, string? Email,
                       string? Role, int DepartmentId, bool IsActive,
                       string? Password);

record SaveNamedRequest(string Name, bool IsActive = true);
record TimeEntryRequest(int Minutes, DateOnly? WorkDate, string? Note);
record SaveCategoryRequest(string Name, int DepartmentId, bool IsActive = true);
record CreateTicketRequest(string Title, string? Description, string? Priority,
                           int? CategoryId, DateOnly? DueDate);
