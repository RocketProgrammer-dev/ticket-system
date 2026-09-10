# Ticket System — İş Takip ve Ticket Yönetim Sistemi

Multi-user desktop ticket management system. Employees open tickets for
support, maintenance and software requests; those tickets are assigned to
technical staff and tracked through to resolution.

*Türkçe belgelendirme için [README.tr.md](README.tr.md) dosyasına bakın.*

---

## Contents

- [Architecture](#architecture)
- [Technology](#technology)
- [Quick start](#quick-start)
- [Detailed setup](#detailed-setup)
- [Building from source](#building-from-source)
- [Using the application](#using-the-application)
- [Design decisions](#design-decisions)
- [Requirements coverage](#requirements-coverage)
- [Known limitations](#known-limitations)
- [Troubleshooting](#troubleshooting)
- [Project layout](#project-layout)
- [Licensing](#licensing)

---

## Architecture

Three tiers, deployed on two machines:

```
  [User PC]  Qt client  ─┐
  [User PC]  Qt client  ─┼── HTTP/JSON ──▶  REST API  ── SQL ──▶  PostgreSQL
  [User PC]  Qt client  ─┘                  [Server machine]
```

**The client never connects to the database directly.** This is not stylistic:
requirement 20 places permission checks on the server, and that is impossible
if every desktop holds database credentials. Anyone could read the connection
string out of the executable and query the tickets table as an administrator.

Each tier is separated internally as well (requirement 22):

| Layer | Client | Server |
|---|---|---|
| Presentation | `mainwindow`, dialogs | — |
| Data model | `ticketmodel`, `commentstore` | — |
| Transport | `apiclient` | Minimal API endpoints |
| Business rules | — | permission checks, validation |
| Persistence | — | EF Core → PostgreSQL |

The client's UI classes never build a URL or parse JSON; they talk to
`ApiClient`. Replacing REST with something else would change one file.

---

## Technology

| Component | Technology | Notes |
|---|---|---|
| Desktop client | C++20, Qt 6 (Widgets) | Built and tested on Qt 6.11 |
| REST API | C# / ASP.NET Core Minimal API | .NET 10 |
| ORM | Entity Framework Core + Npgsql | Database-first |
| Database | PostgreSQL 16 | |
| Auth | JWT bearer tokens, BCrypt hashing | |
| Build | CMake (client), dotnet CLI (API) | |
| Installer | Inno Setup 7 | |

The specification names C++20 and Qt 6, which apply to the client. The backend
language was confirmed as free choice with the project supervisor; C# was
chosen for development speed within the internship timeframe.

---

## Quick start

For a single-machine demonstration where client, API and database all run on
the same computer.

```bash
# 1. Database
createdb ticketdb
psql -d ticketdb -f db/schema.sql
psql -d ticketdb -f db/seed.sql

# 2. API
cd backend/TicketApi
dotnet run
# leave this terminal running

# 3. Client
cmake -B build -S .
cmake --build build
./build/TicketApp
```

Sign in with `e.demir` / `Test1234` (manager).

If the client cannot reach the server, see
[Troubleshooting](#troubleshooting).

---

## Detailed setup

### 1. PostgreSQL

Install PostgreSQL 16 or later.

**Create a dedicated database user.** The application needs access to one
database; the `postgres` superuser account should not be sitting in a config
file.

```sql
CREATE USER ticketapp WITH PASSWORD 'choose-a-strong-password';
CREATE DATABASE ticketdb OWNER ticketapp;
```

Load the schema and demo data:

```bash
psql -U ticketapp -d ticketdb -f db/schema.sql
psql -U ticketapp -d ticketdb -f db/seed.sql
```

`schema.sql` creates ten tables, three enum types, two triggers, a view and
the supporting indexes. `seed.sql` inserts four departments, five categories,
five users and five tickets with comments and time entries.

**Demo accounts** — all use the password `Test1234`:

| Username | Name | Role | Sees |
|---|---|---|---|
| `e.demir` | Elif Demir | Manager | Everything, plus Management window |
| `b.aydin` | Burak Aydın | Technician | All tickets, internal notes, can log time |
| `s.koc` | Selin Koç | Technician | As above |
| `o.sahin` | Onur Şahin | Staff | Only their own tickets, no internal notes |
| `d.yildiz` | Deniz Yıldız | Staff | As above |

> If login fails with correct credentials, the seed file's password hashes
> may need regenerating for your BCrypt version. See
> [Troubleshooting](#troubleshooting).

### 2. The API

**Development:**

```bash
cd backend/TicketApi
dotnet restore
dotnet run
```

Configuration comes from `appsettings.Development.json`, which is not in
version control because it holds a password. Create it:

```json
{
  "ConnectionStrings": {
    "Default": "Host=localhost;Port=5432;Database=ticketdb;Username=ticketapp;Password=your-password"
  },
  "Jwt": {
    "Key": "any-string-of-at-least-32-characters-for-development"
  }
}
```

The API prints the address it is listening on. Open `/swagger` in a browser
to explore and test the endpoints directly.

**Production:** see [backend/DEPLOYMENT.md](backend/DEPLOYMENT.md) for
publishing self-contained, running as a Windows service, firewall rules and
configuration.

### 3. The client

The client reads the API address from `ticketapp.ini`, which must sit **in
the same folder as the executable**:

```ini
[Server]
Url=http://192.168.1.42:5000
```

Use the server's hostname or IP address. `localhost` only works when the API
runs on the same machine as the client.

There is deliberately no user interface for changing this — see
[Design decisions](#design-decisions).

On Windows the installer asks for this address during setup and writes the
file for you.

---

## Building from source

### Client (Linux)

```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-l10n-tools cmake build-essential
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Client (Windows)

Install Qt 6.11 with the MinGW toolchain, then either open `CMakeLists.txt`
in Qt Creator and build in **Release**, or from a Qt command prompt:

```
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

### Translations

Every user-facing string is wrapped in `tr()`. After adding UI text:

```bash
lupdate-qt6 src/ -ts translations/ticketapp_tr.ts translations/ticketapp_en.ts
linguist6 translations/ticketapp_tr.ts     # translate, Ctrl+Enter marks done
lrelease-qt6 translations/ticketapp_tr.ts  # produces the .qm
```

The `.qm` files must be deployed alongside the executable. Unfinished entries
in Linguist are excluded from the `.qm` and silently fall back to English.

> On Ubuntu the unsuffixed `lupdate` and `lrelease` commands often point at
> Qt5 and fail. Use the `-qt6` suffixed versions.

### Windows installer

See [INSTALLER.md](INSTALLER.md) for the full procedure. Summary:

```
cmake --build build-release                    # Release, not Debug
windeployqt --release deploy\windows\TicketApp.exe
copy translations\*.qm deploy\windows\
ISCC.exe installer.iss
```

Test the result from a **plain** Command Prompt, not a Qt one — the Qt prompt
has Qt on the PATH and will hide any missing DLL.

---

## Using the application

### Roles

| | Staff | Technician | Manager |
|---|---|---|---|
| Create tickets | ✔ | ✔ | ✔ |
| See own tickets | ✔ | ✔ | ✔ |
| See all tickets | | ✔ | ✔ |
| Public comments | ✔ | ✔ | ✔ |
| Internal notes | | ✔ | ✔ |
| Change status/priority/assignee | | ✔ | ✔ |
| Log time | | ✔ | ✔ |
| Dashboard and reports | | | ✔ |
| User/department/category admin | | | ✔ |
| Audit log | | | ✔ |

Every one of these is enforced on the server. The client hides controls a
user cannot use, but that is convenience — the API rejects the request
regardless of what the client sends.

### Main window

- **Ticket list** — overdue rows in red, critical priority in bold
- **Search** — matches ticket number, subject, requester or assignee
- **Filters** — status, priority, overdue only
- **Sorting** — click any column header
- **Pagination** — 25/50/100/1000 per page
- **Ctrl+N** new ticket, **Ctrl+E** export CSV, **F5** refresh
- **Notifications** — unread count in the toolbar, polled every 30 seconds

Selecting rows before exporting exports only those; selecting none exports
the whole visible page.

### Ticket detail

Double-click a ticket. Tabs for **Comments**, **Time spent** (technical staff
only) and **Attachments**. Technical staff also get editable status, priority
and assignee with a Save button; every change writes a history row.

"Export this ticket" produces a CSV with four sections: summary, comments,
time entries and attachments.

### Management window (managers only)

- **Dashboard** — summary cards, tickets by status and priority, workload per
  person
- **Reports** — CSV export of the current ticket list
- **Administration** — users, departments and categories
- **Audit log** — filterable, with failed sign-ins highlighted in red

### Language

**Settings → Language**, then restart. Qt applies translations when widgets
are constructed, so a language change cannot take effect in windows that
already exist. On first run the system language is used if available.

---

## Design decisions

Decisions that were made deliberately and are worth explaining.

**Staff see only their own tickets.** The specification does not say what
requesters may see after opening a ticket. Least privilege was chosen because
ticket descriptions routinely contain sensitive material — HR issues, salary
questions, security problems. Changing this to department-wide visibility is
a one-line change in `GET /api/tickets`.

**The API address is not user-configurable.** It is read from
`ticketapp.ini`, installed under Program Files where ordinary users cannot
write. A client whose destination any user can retype is a credential-harvesting
opportunity: someone only has to say "IT here, please point your app at this
address". Whoever administers the machine can still edit the file, which is
the correct level of access.

**Users are deactivated, not deleted.** Deletion is refused (HTTP 409) for
any user with tickets, comments or time entries, and the UI then offers
deactivation instead. Removing a person who appears throughout the history
would tear holes in the audit trail that requirement 21 exists to maintain.
Deletion is permitted only for accounts with no activity at all.

**Deactivated users are hidden from the administration list by default.**
Retention and visibility are separate concerns. After several years, departed
employees would outnumber current staff and make the screen unusable; the
records stay in the database, but the list shows what is operationally
relevant. A checkbox reveals them.

**Charts are drawn with QPainter rather than Qt Charts.** Qt Charts is
licensed under GPL or a commercial licence, not LGPL. Using it would require
this application to be GPL-licensed, which is not a decision an intern can
make on the company's behalf. Roughly forty lines of painting in
`barchartwidget.cpp` avoids the question entirely.

**Filtering, sorting and pagination all happen in SQL.** Doing any of them in
the client would only affect the current page: filtering to "critical" would
show the critical tickets on page 1 while hiding others on later pages, and
sorting would reorder 25 arbitrary rows rather than choosing which 25 belong
first. Pagination without server-side filtering is worse than no pagination,
because it appears to work.

**Ticket numbers come from a database sequence and trigger.** Two
simultaneous inserts cannot collide. Generating them in application code
eventually produces duplicates under concurrent load.

**Time is stored as integer minutes.** Hours as a floating-point number
cannot represent 0.1 exactly, and rounding drift in a figure someone bills
from is not a defensible bug.

**Change history stores names, not ids.** `assigned_to: 2 → 3` is
meaningless to whoever reads the audit trail later. Ids are for joins; labels
are for history.

**Uploaded files are given generated names.** The client's filename is stored
as a display label and never used to build a path. A filename such as
`../../etc/cron.d/evil` becomes harmless text in a database column because
the actual file is a GUID in a directory the application chose. Extensions
are checked against a whitelist, never a blacklist — blacklists always miss
something.

---

## Requirements coverage

| # | Requirement | Status | Where |
|---|---|---|---|
| 1 | Developed entirely from scratch | ✔ | — |
| 2 | User login and logout | ✔ | `loginwindow`, `POST /api/auth/login` |
| 3 | Staff, technician and manager roles | ✔ | `userrole.h`, JWT claims |
| 4 | Users can create tickets | ✔ | `newticketdialog` |
| 5 | Unique automatic ticket numbers | ✔ | `ticket_number_seq` + trigger |
| 6 | Tickets assignable to staff | ✔ | `PUT /api/tickets/{id}` |
| 7 | Status and priority management | ✔ | Ticket detail dialog |
| 8 | Due dates | ✔ | `tickets.due_date` |
| 9 | Comments on tickets | ✔ | `ticket_comments` |
| 10 | Public comments separated from internal notes | ✔ | `is_internal`, filtered server-side |
| 11 | File attachments | ✔ | `AttachmentStorage.cs` |
| 12 | Time tracking | ✔ | `time_entries` |
| 13 | Status, assignment and priority history | ✔ | `ticket_history` |
| 14 | In-application notifications | ✔ | `notifications`, 30s polling |
| 15 | Automatic overdue detection | ✔ | `OverdueScanner` background service |
| 16 | Search, filter, sort and pagination | ✔ | All server-side |
| 17 | Manager dashboard and reporting | ✔ | Management window |
| 18 | CSV export | ✔ | `csvexporter.cpp` |
| 19 | User, department and category management | ✔ | Administration tab |
| 20 | Server-side permission checks | ✔ | Every endpoint |
| 21 | Audit log of critical operations | ✔ | `audit_log`, Audit tab |
| 22 | Separated database, UI and business layers | ✔ | See [Architecture](#architecture) |
| 23 | Object-oriented design | ✔ | — |
| 24 | Errors written to a log file | ✔ | `logging.cpp` |
| 25 | Regular meaningful Git commits | ✔ | — |
| 26 | Installation and usage documentation | ✔ | This file |
| 27 | Windows installation package | ✔ | `installer.iss` |

---

## Known limitations

Things that are incomplete or would need work before real deployment. These
are known, not overlooked.

**Traffic is plain HTTP.** Usernames, passwords and ticket contents are
readable by anyone capturing packets on the network. Production use requires
HTTPS — either a certificate configured in Kestrel, or the API behind IIS or
nginx as a reverse proxy. This is the most significant limitation in the
project.

**The JWT signing key is stored in a plain configuration file.** Restrict the
file's permissions to the service account. A secrets manager would be better.

**Notifications are polled every 30 seconds**, not pushed. A WebSocket
connection would be more responsive but adds reconnection logic and a
protocol to maintain; polling is adequate at this scale.

**Every manager is notified of every new ticket.** With three managers this
is useful; with thirty it is noise. Notifying only managers of the ticket's
department would be the refinement — the schema already supports it via
`users.department_id`.

**The assignee dropdown matches by name.** `Ticket` carries the assignee's
display name rather than their id, so two users with identical names would
be indistinguishable. Adding `assignedToId` to the ticket JSON is the fix.

**Ticket descriptions are sent with the list.** Fetching 500 tickets
transfers 500 descriptions that are mostly not displayed. A separate
`GET /api/tickets/{id}` returning full detail would be tidier.

**No automated database backup** is configured.

**No automated tests.** Verification was manual throughout.

---

## Troubleshooting

Problems actually encountered during development, and what caused them.

### `psql: error: db/schema.sql: Permission denied`

`sudo -u postgres` switches to a user that cannot read inside your home
directory. Let your own shell open the file instead:

```bash
sudo -u postgres psql -d ticketdb -v ON_ERROR_STOP=1 < db/schema.sql
```

Note the `<` redirect rather than `-f`. Better still, create a database role
matching your Linux username: `sudo -u postgres createuser --superuser $USER`.

### `28P01: password authentication failed for user "postgres"`

The API connects over TCP, which requires password authentication, unlike
`psql` on a local socket which may use peer authentication. Set a password:

```bash
sudo -u postgres psql -c "ALTER USER postgres PASSWORD 'yourpassword';"
```

Then verify with the same mechanism the application uses:

```bash
psql -h localhost -U postgres -d ticketdb -c "SELECT 1;"
```

If that works but the API still fails, the API is reading a different
configuration than you think. See the next entry.

### The API ignores `appsettings.Development.json`

That file is only loaded when `ASPNETCORE_ENVIRONMENT=Development`, normally
set by `Properties/launchSettings.json`. If that file is missing, the
application runs as Production. Create it:

```json
{
  "profiles": {
    "TicketApi": {
      "commandName": "Project",
      "applicationUrl": "http://localhost:5000",
      "environmentVariables": { "ASPNETCORE_ENVIRONMENT": "Development" }
    }
  }
}
```

An environment variable set with `export` only exists in that one terminal,
which is why configuration belongs in a file.

### `BCrypt.Net.SaltParseException: Invalid salt version`

The stored hash is corrupted, almost always by shell expansion. A BCrypt hash
contains `$2a$11$`, and inside **double** quotes bash expands `$2` and `$11`
as variables, silently mangling it. Use an interactive psql session:

```bash
psql -d ticketdb
```

```sql
UPDATE users SET password_hash = '$2a$11$...';
```

A valid hash is exactly 60 characters and begins `$2a$` or `$2b$`. Verify:

```sql
SELECT username, left(password_hash, 7), length(password_hash) FROM users;
```

To generate one:

```bash
python3 -m venv ~/.venvs/tools
~/.venvs/tools/bin/pip install bcrypt
~/.venvs/tools/bin/python -c "import bcrypt; print(bcrypt.hashpw(b'Test1234', bcrypt.gensalt(11)).decode())"
```

### `Reading as 'System.Int32' is not supported for fields having DataTypeName 'public.user_role'`

PostgreSQL enum types must be registered **twice** — once with Npgsql on the
data source, and once with EF Core, which keeps its own type registry:

```csharp
dataSourceBuilder.MapEnum<UserRole>("user_role");          // Npgsql

builder.Services.AddDbContext<AppDbContext>(o => o
    .UseNpgsql(dataSource, npgsql => {
        npgsql.MapEnum<UserRole>("user_role");             // EF Core
    }));
```

Registering only the first fails at query time, not at startup.

### `Failed to bind to address http://127.0.0.1:5000: address already in use`

An earlier `dotnet run` is still running:

```bash
ss -tlnp | grep 5000     # find the PID
pkill -f "dotnet run"
```

### An endpoint returns 404 that clearly exists in the code

The API is serving a previous build. `dotnet run` does not reload on change,
and if compilation fails it keeps serving the last successful binary. Use
`dotnet watch run` during development. Confirm what the server actually knows:

```bash
curl -s http://localhost:5000/swagger/v1/swagger.json | grep -o '"/api/[^"]*"' | sort -u
```

### CMake produces files named `(` and `)`, or paths rooted at `/`

The project path contains spaces or parentheses. CMake splits unquoted
arguments on whitespace and treats parentheses as list syntax, producing
targets with nonsensical names. Keep build paths free of spaces,
parentheses and non-ASCII characters.

### `undefined reference to 'ClassName::slotName()'` mentioning a `moc_` file

A slot is declared in the header but has no implementation. moc generates
code for every declared slot whether or not it is connected. Either implement
it or remove the declaration.

By contrast, `undefined reference to vtable` usually means `CMAKE_AUTOMOC` is
off, the class is missing `Q_OBJECT`, or the header is not listed in
`add_executable`.

### `lupdate: could not exec '/usr/lib/qt5/bin/lupdate'`

Ubuntu ships Qt5 and Qt6 side by side and the unsuffixed commands often point
at Qt5. Use `lupdate-qt6`, `lrelease-qt6`, `linguist6`, or the full path
inside your Qt installation.

### The application starts on Windows with no window

Almost always a missing `platforms\qwindows.dll`. `windeployqt` should copy
it into a `platforms` subfolder next to the executable. Test from a plain
Command Prompt, never the Qt one — the Qt prompt has Qt on the PATH and hides
exactly this failure.

### The client cannot reach the server across two machines

Three usual causes, in order of likelihood:

1. **The API is bound to localhost.** Start it with
   `--urls "http://0.0.0.0:5000"` or set `Kestrel:Endpoints` in
   `appsettings.json`. Bound to localhost it accepts connections only from
   its own machine.
2. **A firewall is blocking the port.** `sudo ufw allow 5000/tcp` on Linux.
3. **The network isolates clients from each other**, which many corporate
   wireless networks do. Test with a phone hotspot or a cable.

Verify from the client machine before blaming the application:

```
curl http://<server-ip>:5000/swagger/index.html
```

### Where are the logs?

**Client:** `%APPDATA%\TicketSystem\TicketApp\ticketapp.log` on Windows,
`~/.local/share/TicketSystem/TicketApp/ticketapp.log` on Linux. Also
reachable from **Settings → Open log folder**. Rotates at 2 MB, keeping one
previous file.

**API:** written to the console. Redirect it to a file when running as a
service.

---

## Project layout

```
ticket-system/
├── CMakeLists.txt              Client build
├── installer.iss               Inno Setup script
├── INSTALLER.md                Windows packaging procedure
├── README.md                   This file
├── README.tr.md                Turkish documentation
│
├── src/                        Qt client
│   ├── main.cpp                Entry point, screen transitions
│   ├── loginwindow.*           Authentication
│   ├── mainwindow.*            Ticket list, toolbar, menus, paging
│   ├── ticketmodel.*           Table model
│   ├── ticketdetaildialog.*    Comments, time, attachments, editing
│   ├── newticketdialog.*       Ticket creation form
│   ├── managementdialog.*      Dashboard, reports, admin, audit
│   ├── admintab.*              Users, departments, categories
│   ├── audittab.*              Audit log viewer
│   ├── barchartwidget.*        QPainter charts (avoids GPL Qt Charts)
│   ├── apiclient.*             All HTTP; the only file that knows about REST
│   ├── commentstore.*          Comment cache, keyed by ticket
│   ├── csvexporter.*           CSV generation
│   ├── logging.*               File logging via qInstallMessageHandler
│   ├── settings.*              Deployment configuration
│   └── language.*              Translation loading
│
├── resources/                  Icon and Qt resource files
├── translations/               .ts translation sources
├── deploy/                     ticketapp.ini template
│
├── db/
│   ├── schema.sql              Tables, types, triggers, indexes, view
│   └── seed.sql                Demo data
│
└── backend/
    ├── DEPLOYMENT.md           Server deployment guide
    └── TicketApi/
        ├── Program.cs          All endpoints
        ├── Data/AppDbContext.cs
        ├── Models/Entities.cs
        └── Services/
            ├── OverdueScanner.cs      Background overdue detection
            └── AttachmentStorage.cs   File upload handling
```

---

## Licensing

**Qt** is used under **LGPLv3**, which permits use in a closed-source
application provided that Qt is linked dynamically, users can replace the Qt
libraries, and the licence and attribution are included. `windeployqt`
produces exactly this arrangement — an executable alongside separate Qt DLLs.

**Qt Charts is deliberately not used.** It is available only under GPL or a
commercial licence; including it would require this application to be
GPL-licensed. Charts are drawn manually with `QPainter` instead.

**Qt's Installer Framework is deliberately not used**, for the same reason.
The installer is built with **Inno Setup**.

> Recent Inno Setup releases request a commercial licence for commercial use.
> This should be confirmed before the application is deployed commercially.
> WiX Toolset (MS-PL) and NSIS (zlib) are freely usable alternatives if it
> proves to be a problem.

**BCrypt.Net-Next**, **Npgsql**, **Entity Framework Core** and **Swashbuckle**
are all MIT or Apache 2.0 licensed and impose no restrictions on this
application.
