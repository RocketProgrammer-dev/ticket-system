using NpgsqlTypes;

namespace TicketApi.Models;

// These enums must match the PostgreSQL ENUM types in schema.sql exactly.
// [PgName] maps each C# member to its database label — without it, Npgsql
// would send "InProgress" where Postgres expects "in_progress".

public enum UserRole
{
    [PgName("staff")]      Staff,
    [PgName("technician")] Technician,
    [PgName("manager")]    Manager
}

public enum TicketStatus
{
    [PgName("open")]        Open,
    [PgName("in_progress")] InProgress,
    [PgName("waiting")]     Waiting,
    [PgName("resolved")]    Resolved,
    [PgName("closed")]      Closed
}

public enum TicketPriority
{
    [PgName("low")]      Low,
    [PgName("normal")]   Normal,
    [PgName("high")]     High,
    [PgName("critical")] Critical
}


public class User
{
    public int      Id { get; set; }
    public string   Username { get; set; } = "";
    public string   PasswordHash { get; set; } = "";
    public string   FullName { get; set; } = "";
    public string?  Email { get; set; }
    public UserRole Role { get; set; }
    public int?     DepartmentId { get; set; }
    public bool     IsActive { get; set; } = true;
    public DateTime? LastLoginAt { get; set; }
    public DateTime CreatedAt { get; set; }
}

public class Ticket
{
    public int    Id { get; set; }

    // Set by the database trigger on insert — never assign it here.
    public string TicketNumber { get; set; } = "";

    public string  Title { get; set; } = "";
    public string? Description { get; set; }
    public TicketStatus   Status { get; set; }
    public TicketPriority Priority { get; set; }

    public int  CreatedById { get; set; }
    public int? AssignedToId { get; set; }
    public int? DepartmentId { get; set; }
    public int? CategoryId { get; set; }

    public DateOnly? DueDate { get; set; }
    public DateTime  CreatedAt { get; set; }
    public DateTime  UpdatedAt { get; set; }
    public DateTime? ClosedAt { get; set; }

    // Navigation properties. EF uses these to JOIN when you call .Include(),
    // so you can read ticket.AssignedTo.FullName without a second query.
    public User? CreatedBy { get; set; }
    public User? AssignedTo { get; set; }
}

public class TicketComment
{
    public int      Id { get; set; }
    public int      TicketId { get; set; }
    public int      AuthorId { get; set; }
    public string   Body { get; set; } = "";
    public bool     IsInternal { get; set; }
    public DateTime CreatedAt { get; set; }

    public User? Author { get; set; }
}

public class TicketHistory
{
    public int      Id { get; set; }
    public int      TicketId { get; set; }
    public int      ChangedById { get; set; }
    public string   FieldName { get; set; } = "";
    public string?  OldValue { get; set; }
    public string?  NewValue { get; set; }
    public DateTime ChangedAt { get; set; }
}

public class AuditLog
{
    public int      Id { get; set; }

    // Nullable because a FAILED login has no authenticated user — and those
    // are among the most important rows in the table.
    public int?     UserId { get; set; }

    public string   Action { get; set; } = "";
    public string?  EntityType { get; set; }
    public int?     EntityId { get; set; }

    // jsonb in PostgreSQL. Free-form context that varies by action: the
    // attempted username on a failed login, the old and new role on a
    // permission change. A fixed column per case would need a migration
    // every time a new action is audited.
    public string?  Details { get; set; }

    public DateTime CreatedAt { get; set; }

    public User? User { get; set; }
}


// Requirement 19. These tables existed in schema.sql from the start; they only
// needed entity classes once the API had to manage them.

public class Department
{
    public int      Id { get; set; }
    public string   Name { get; set; } = "";
    public bool     IsActive { get; set; } = true;
    public DateTime CreatedAt { get; set; }
}

public class Category
{
    public int      Id { get; set; }
    public string   Name { get; set; } = "";
    public int?     DepartmentId { get; set; }
    public bool     IsActive { get; set; } = true;
    public DateTime CreatedAt { get; set; }

    public Department? Department { get; set; }
}


// Requirement 12. Minutes as an integer, never hours as a double: 0.1 hours
// has no exact binary representation, and rounding drift in a number someone
// bills from is not a bug you want to explain.
public class TimeEntry
{
    public int      Id { get; set; }
    public int      TicketId { get; set; }
    public int      UserId { get; set; }
    public int      Minutes { get; set; }
    public DateOnly WorkDate { get; set; }
    public string?  Note { get; set; }
    public DateTime CreatedAt { get; set; }

    public User? User { get; set; }
}


// Requirement 14.
public class Notification
{
    public int      Id { get; set; }
    public int      UserId { get; set; }
    public int?     TicketId { get; set; }
    public string   Message { get; set; } = "";
    public bool     IsRead { get; set; }
    public DateTime CreatedAt { get; set; }
}


// Requirement 11.
public class Attachment
{
    public int     Id { get; set; }
    public int?    TicketId { get; set; }
    public int?    CommentId { get; set; }

    // What the user called it. Shown in the UI, used for the download name,
    // and NEVER used to build a path on disk.
    public string  OriginalName { get; set; } = "";

    // What we called it: a generated name in our own directory. This is the
    // difference between a file store and a remote code execution bug.
    public string  StoredPath { get; set; } = "";

    public string? MimeType { get; set; }
    public long    SizeBytes { get; set; }
    public int     UploadedById { get; set; }
    public DateTime UploadedAt { get; set; }

    public User? UploadedBy { get; set; }
}
