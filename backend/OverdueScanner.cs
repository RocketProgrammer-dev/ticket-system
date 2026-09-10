using Microsoft.EntityFrameworkCore;
using TicketApi.Data;
using TicketApi.Models;

namespace TicketApi.Services;

// Requirement 15: overdue tickets are identified AUTOMATICALLY.
//
// The client already colours overdue rows red, but that only happens when
// someone is looking. "Automatically" means the system notices on its own, so
// this runs on a timer inside the API and raises a notification once per day
// per overdue ticket.
//
// BackgroundService is ASP.NET's built-in base class for long-running work.
// It starts with the app and stops cleanly on shutdown.

public class OverdueScanner : BackgroundService
{
    private readonly IServiceScopeFactory _scopeFactory;
    private readonly ILogger<OverdueScanner> _logger;

    // Short enough to see working during a demo, long enough not to hammer
    // the database. A real deployment would read this from configuration.
    private static readonly TimeSpan Interval = TimeSpan.FromMinutes(5);

    public OverdueScanner(IServiceScopeFactory scopeFactory,
                          ILogger<OverdueScanner> logger)
    {
        _scopeFactory = scopeFactory;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        // A short delay so the scan doesn't compete with application startup.
        await Task.Delay(TimeSpan.FromSeconds(10), stoppingToken);

        while (!stoppingToken.IsCancellationRequested) {
            try {
                await ScanAsync(stoppingToken);
            } catch (Exception ex) {
                // Never let an exception kill the loop: one bad scan should
                // not silently disable overdue detection until the next
                // restart. Log it and try again next interval.
                _logger.LogError(ex, "Overdue scan failed");
            }

            await Task.Delay(Interval, stoppingToken);
        }
    }

    private async Task ScanAsync(CancellationToken token)
    {
        // A BackgroundService is a singleton but DbContext is scoped, so you
        // cannot inject the context directly — you create a scope per run.
        // Injecting it would give you one context for the lifetime of the app,
        // which leaks memory and serves stale data.
        using var scope = _scopeFactory.CreateScope();
        var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();

        var today = DateOnly.FromDateTime(DateTime.UtcNow);

        var overdue = await db.Tickets
            .Where(t => t.DueDate != null &&
                        t.DueDate < today &&
                        t.Status != TicketStatus.Resolved &&
                        t.Status != TicketStatus.Closed)
            .ToListAsync(token);

        if (overdue.Count == 0)
            return;

        var startOfToday = DateTime.UtcNow.Date;
        var created = 0;

        foreach (var ticket in overdue) {

            // Notify whoever is responsible: the assignee, or the creator if
            // nobody has picked it up yet.
            var recipientId = ticket.AssignedToId ?? ticket.CreatedById;

            // Don't re-notify about the same ticket every five minutes. One
            // reminder per day is a nag; one every interval is spam that
            // trains people to ignore the bell entirely.
            var alreadyToday = await db.Notifications.AnyAsync(n =>
                n.TicketId == ticket.Id &&
                n.UserId == recipientId &&
                n.CreatedAt >= startOfToday, token);

            if (alreadyToday)
                continue;

            var daysLate = today.DayNumber - ticket.DueDate!.Value.DayNumber;

            db.Notifications.Add(new Notification {
                UserId    = recipientId,
                TicketId  = ticket.Id,
                Message   = $"{ticket.TicketNumber} is overdue by {daysLate} day(s): {ticket.Title}",
                IsRead    = false,
                CreatedAt = DateTime.UtcNow
            });

            created++;
        }

        if (created > 0) {
            await db.SaveChangesAsync(token);
            _logger.LogInformation("Overdue scan created {Count} notifications", created);
        }
    }
}
