using Microsoft.EntityFrameworkCore;
using TicketApi.Models;

namespace TicketApi.Data;

// IMPORTANT: this project uses the schema you already wrote in schema.sql.
// Do NOT run `dotnet ef migrations add` — EF would try to generate its own
// tables and fight the ones that exist. This is "database first": the SQL is
// the source of truth, and these mappings describe what's already there.

public class AppDbContext : DbContext
{
    public AppDbContext(DbContextOptions<AppDbContext> options) : base(options) { }

    public DbSet<User>          Users          => Set<User>();
    public DbSet<Ticket>        Tickets        => Set<Ticket>();
    public DbSet<TicketComment> Comments       => Set<TicketComment>();
    public DbSet<TicketHistory> History        => Set<TicketHistory>();
    public DbSet<AuditLog>      AuditLogs      => Set<AuditLog>();
    public DbSet<Department>    Departments    => Set<Department>();
    public DbSet<Category>      Categories     => Set<Category>();
    public DbSet<TimeEntry>     TimeEntries    => Set<TimeEntry>();
    public DbSet<Notification>  Notifications  => Set<Notification>();
    public DbSet<Attachment>    Attachments    => Set<Attachment>();

    protected override void OnModelCreating(ModelBuilder b)
    {
        // Postgres convention is snake_case; C# convention is PascalCase.
        // Rather than decorating every property, map them explicitly here so
        // the mismatch lives in exactly one file.

        b.Entity<User>(e => {
            e.ToTable("users");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.Username).HasColumnName("username");
            e.Property(x => x.PasswordHash).HasColumnName("password_hash");
            e.Property(x => x.FullName).HasColumnName("full_name");
            e.Property(x => x.Email).HasColumnName("email");
            e.Property(x => x.Role).HasColumnName("role");
            e.Property(x => x.DepartmentId).HasColumnName("department_id");
            e.Property(x => x.IsActive).HasColumnName("is_active");
            e.Property(x => x.LastLoginAt).HasColumnName("last_login_at");
            e.Property(x => x.CreatedAt).HasColumnName("created_at");
        });

        b.Entity<Ticket>(e => {
            e.ToTable("tickets");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.TicketNumber).HasColumnName("ticket_number")
             .ValueGeneratedOnAdd();   // the trigger fills this; EF must read it back
            e.Property(x => x.Title).HasColumnName("title");
            e.Property(x => x.Description).HasColumnName("description");
            e.Property(x => x.Status).HasColumnName("status");
            e.Property(x => x.Priority).HasColumnName("priority");
            e.Property(x => x.CreatedById).HasColumnName("created_by");
            e.Property(x => x.AssignedToId).HasColumnName("assigned_to");
            e.Property(x => x.DepartmentId).HasColumnName("department_id");
            e.Property(x => x.CategoryId).HasColumnName("category_id");
            e.Property(x => x.DueDate).HasColumnName("due_date");
            e.Property(x => x.CreatedAt).HasColumnName("created_at");
            e.Property(x => x.UpdatedAt).HasColumnName("updated_at");
            e.Property(x => x.ClosedAt).HasColumnName("closed_at");

            e.HasOne(x => x.CreatedBy).WithMany().HasForeignKey(x => x.CreatedById);
            e.HasOne(x => x.AssignedTo).WithMany().HasForeignKey(x => x.AssignedToId);
        });

        b.Entity<TicketComment>(e => {
            e.ToTable("ticket_comments");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.TicketId).HasColumnName("ticket_id");
            e.Property(x => x.AuthorId).HasColumnName("author_id");
            e.Property(x => x.Body).HasColumnName("body");
            e.Property(x => x.IsInternal).HasColumnName("is_internal");
            e.Property(x => x.CreatedAt).HasColumnName("created_at");

            e.HasOne(x => x.Author).WithMany().HasForeignKey(x => x.AuthorId);
        });

        b.Entity<TicketHistory>(e => {
            e.ToTable("ticket_history");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.TicketId).HasColumnName("ticket_id");
            e.Property(x => x.ChangedById).HasColumnName("changed_by");
            e.Property(x => x.FieldName).HasColumnName("field_name");
            e.Property(x => x.OldValue).HasColumnName("old_value");
            e.Property(x => x.NewValue).HasColumnName("new_value");
            e.Property(x => x.ChangedAt).HasColumnName("changed_at");
        });

        b.Entity<Attachment>(e => {
            e.ToTable("attachments");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.TicketId).HasColumnName("ticket_id");
            e.Property(x => x.CommentId).HasColumnName("comment_id");
            e.Property(x => x.OriginalName).HasColumnName("original_name");
            e.Property(x => x.StoredPath).HasColumnName("stored_path");
            e.Property(x => x.MimeType).HasColumnName("mime_type");
            e.Property(x => x.SizeBytes).HasColumnName("size_bytes");
            e.Property(x => x.UploadedById).HasColumnName("uploaded_by");
            e.Property(x => x.UploadedAt).HasColumnName("uploaded_at");

            e.HasOne(x => x.UploadedBy).WithMany().HasForeignKey(x => x.UploadedById);
        });

        b.Entity<Notification>(e => {
            e.ToTable("notifications");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.UserId).HasColumnName("user_id");
            e.Property(x => x.TicketId).HasColumnName("ticket_id");
            e.Property(x => x.Message).HasColumnName("message");
            e.Property(x => x.IsRead).HasColumnName("is_read");
            e.Property(x => x.CreatedAt).HasColumnName("created_at");
        });

        b.Entity<TimeEntry>(e => {
            e.ToTable("time_entries");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.TicketId).HasColumnName("ticket_id");
            e.Property(x => x.UserId).HasColumnName("user_id");
            e.Property(x => x.Minutes).HasColumnName("minutes");
            e.Property(x => x.WorkDate).HasColumnName("work_date");
            e.Property(x => x.Note).HasColumnName("note");
            e.Property(x => x.CreatedAt).HasColumnName("created_at");

            e.HasOne(x => x.User).WithMany().HasForeignKey(x => x.UserId);
        });

        b.Entity<Department>(e => {
            e.ToTable("departments");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.Name).HasColumnName("name");
            e.Property(x => x.IsActive).HasColumnName("is_active");
            e.Property(x => x.CreatedAt).HasColumnName("created_at");
        });

        b.Entity<Category>(e => {
            e.ToTable("categories");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.Name).HasColumnName("name");
            e.Property(x => x.DepartmentId).HasColumnName("department_id");
            e.Property(x => x.IsActive).HasColumnName("is_active");
            e.Property(x => x.CreatedAt).HasColumnName("created_at");

            e.HasOne(x => x.Department).WithMany().HasForeignKey(x => x.DepartmentId);
        });

        b.Entity<AuditLog>(e => {
            e.ToTable("audit_log");
            e.Property(x => x.Id).HasColumnName("id");
            e.Property(x => x.UserId).HasColumnName("user_id");
            e.Property(x => x.Action).HasColumnName("action");
            e.Property(x => x.EntityType).HasColumnName("entity_type");
            e.Property(x => x.EntityId).HasColumnName("entity_id");
            e.Property(x => x.Details).HasColumnName("details").HasColumnType("jsonb");
            e.Property(x => x.CreatedAt).HasColumnName("created_at");

            e.HasOne(x => x.User).WithMany().HasForeignKey(x => x.UserId);
        });
    }
}
