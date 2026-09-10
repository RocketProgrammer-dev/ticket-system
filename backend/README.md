# Ticket API (C# / ASP.NET Core)

REST backend for the Qt desktop client. Runs on Linux, macOS or Windows.

## Setup

```bash
# 1. Create the database and load the schema
createdb -U postgres ticketdb
psql -U postgres -d ticketdb -f ../../db/schema.sql
psql -U postgres -d ticketdb -f ../../db/seed.sql

# 2. Set your connection string and JWT secret
#    Edit appsettings.json, or better, keep secrets out of Git:
dotnet user-secrets init
dotnet user-secrets set "ConnectionStrings:Default" "Host=localhost;Database=ticketdb;Username=postgres;Password=yourpassword"

# 3. Run
dotnet restore
dotnet run
```

Then open <http://localhost:5000/swagger> to try the endpoints by hand.

## Generating password hashes for seed data

The seed file ships with placeholder hashes that will not verify. Generate
real ones:

```bash
dotnet script eval 'Console.WriteLine(BCrypt.Net.BCrypt.HashPassword("Test1234!"))'
```

Or add a temporary endpoint that hashes a string, use it, then delete it.

## Do not run EF migrations

The schema is defined in `db/schema.sql` and is the source of truth.
`dotnet ef migrations add` would try to generate a competing set of tables.

## Endpoints

| Method | Path                          | Who          |
|--------|-------------------------------|--------------|
| POST   | /api/auth/login               | anyone       |
| GET    | /api/tickets                  | authenticated (staff see only their own) |
| POST   | /api/tickets                  | authenticated |
| GET    | /api/tickets/{id}/comments    | authenticated (staff never receive internal notes) |
| POST   | /api/tickets/{id}/comments    | authenticated (staff cannot post internal notes) |

## Still to do

- PUT /api/tickets/{id} with ticket_history rows written on every change (req 13)
- Attachments upload/download (req 11)
- Time entries (req 12)
- Notifications (req 14)
- Dashboard aggregates (req 17)
