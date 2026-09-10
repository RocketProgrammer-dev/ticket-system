-- =====================================================================
--  Ticket System — PostgreSQL schema
--  Run with:  psql -U postgres -d ticketdb -f schema.sql
-- =====================================================================

-- Enum types instead of loose text columns. The database itself now rejects
-- a typo like 'in progres', which no amount of C# validation guarantees.
CREATE TYPE user_role       AS ENUM ('staff', 'technician', 'manager');
CREATE TYPE ticket_status   AS ENUM ('open', 'in_progress', 'waiting', 'resolved', 'closed');
CREATE TYPE ticket_priority AS ENUM ('low', 'normal', 'high', 'critical');


-- ---------------------------------------------------------------------
--  Organisation (requirement 19)
-- ---------------------------------------------------------------------
CREATE TABLE departments (
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(100) NOT NULL UNIQUE,
    is_active   BOOLEAN NOT NULL DEFAULT TRUE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE categories (
    id            SERIAL PRIMARY KEY,
    name          VARCHAR(100) NOT NULL,
    department_id INTEGER REFERENCES departments(id) ON DELETE SET NULL,
    is_active     BOOLEAN NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (name, department_id)
);


-- ---------------------------------------------------------------------
--  Users (requirements 2, 3)
-- ---------------------------------------------------------------------
CREATE TABLE users (
    id             SERIAL PRIMARY KEY,
    username       VARCHAR(50)  NOT NULL UNIQUE,

    -- NEVER store the password itself. This column holds a BCrypt hash,
    -- which is one-way: you can verify a password against it but not recover
    -- it. In C#: BCrypt.Net.BCrypt.HashPassword(plain) to store,
    -- BCrypt.Net.BCrypt.Verify(plain, hash) to check.
    password_hash  VARCHAR(255) NOT NULL,

    full_name      VARCHAR(150) NOT NULL,
    email          VARCHAR(150) UNIQUE,
    role           user_role    NOT NULL DEFAULT 'staff',
    department_id  INTEGER REFERENCES departments(id) ON DELETE SET NULL,
    is_active      BOOLEAN      NOT NULL DEFAULT TRUE,
    last_login_at  TIMESTAMPTZ,
    created_at     TIMESTAMPTZ  NOT NULL DEFAULT now()
);

CREATE INDEX idx_users_department ON users(department_id);


-- ---------------------------------------------------------------------
--  Tickets (requirements 4-8)
-- ---------------------------------------------------------------------

-- Requirement 5: unique, automatic ticket numbers. A sequence guarantees
-- uniqueness even when two people submit at the exact same moment —
-- something a "SELECT MAX(id)+1" approach silently gets wrong under load.
CREATE SEQUENCE ticket_number_seq START 1;

CREATE TABLE tickets (
    id             SERIAL PRIMARY KEY,

    -- Filled by the trigger below, e.g. 'TKT-2026-000042'
    ticket_number  VARCHAR(20) NOT NULL UNIQUE,

    title          VARCHAR(200) NOT NULL,
    description    TEXT,
    status         ticket_status   NOT NULL DEFAULT 'open',
    priority       ticket_priority NOT NULL DEFAULT 'normal',

    -- ON DELETE RESTRICT: you cannot delete a user who still has tickets.
    -- Deactivate them (is_active = false) instead. Deleting people out from
    -- under historical records is how audit trails get holes in them.
    created_by     INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    assigned_to    INTEGER          REFERENCES users(id) ON DELETE SET NULL,

    department_id  INTEGER REFERENCES departments(id) ON DELETE SET NULL,
    category_id    INTEGER REFERENCES categories(id)  ON DELETE SET NULL,

    due_date       DATE,                        -- requirement 8, NULL = no deadline
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    closed_at      TIMESTAMPTZ
);

-- Requirement 16: these indexes are what keep filtering fast once the table
-- has 50,000 rows. Index the columns you filter and sort by.
CREATE INDEX idx_tickets_status      ON tickets(status);
CREATE INDEX idx_tickets_priority    ON tickets(priority);
CREATE INDEX idx_tickets_assigned    ON tickets(assigned_to);
CREATE INDEX idx_tickets_created_by  ON tickets(created_by);
CREATE INDEX idx_tickets_due_date    ON tickets(due_date);

-- Case-insensitive subject search without a full scan.
CREATE INDEX idx_tickets_title_lower ON tickets(LOWER(title));


-- Requirement 5: generate the number automatically on insert.
CREATE OR REPLACE FUNCTION generate_ticket_number()
RETURNS TRIGGER AS $$
BEGIN
    IF NEW.ticket_number IS NULL OR NEW.ticket_number = '' THEN
        NEW.ticket_number := 'TKT-'
            || to_char(now(), 'YYYY') || '-'
            || lpad(nextval('ticket_number_seq')::TEXT, 6, '0');
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_ticket_number
    BEFORE INSERT ON tickets
    FOR EACH ROW EXECUTE FUNCTION generate_ticket_number();


-- Keep updated_at honest without relying on the application to remember.
CREATE OR REPLACE FUNCTION touch_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at := now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_tickets_touch
    BEFORE UPDATE ON tickets
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();


-- ---------------------------------------------------------------------
--  Comments (requirements 9, 10)
-- ---------------------------------------------------------------------
CREATE TABLE ticket_comments (
    id          SERIAL PRIMARY KEY,
    ticket_id   INTEGER NOT NULL REFERENCES tickets(id) ON DELETE CASCADE,
    author_id   INTEGER NOT NULL REFERENCES users(id)   ON DELETE RESTRICT,
    body        TEXT    NOT NULL,

    -- Requirement 10. The API must filter on this for staff users:
    --   WHERE ticket_id = @id AND (@role <> 'staff' OR is_internal = FALSE)
    -- Hiding internal notes only in the Qt client is not a control.
    is_internal BOOLEAN NOT NULL DEFAULT FALSE,

    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_comments_ticket ON ticket_comments(ticket_id, created_at);


-- ---------------------------------------------------------------------
--  Attachments (requirement 11)
-- ---------------------------------------------------------------------
CREATE TABLE attachments (
    id            SERIAL PRIMARY KEY,

    -- An attachment hangs off either a ticket or a comment, never both and
    -- never neither. The CHECK enforces that instead of trusting the API.
    ticket_id     INTEGER REFERENCES tickets(id)         ON DELETE CASCADE,
    comment_id    INTEGER REFERENCES ticket_comments(id) ON DELETE CASCADE,

    original_name VARCHAR(255) NOT NULL,

    -- Store files on disk, keep only the path here. Large binaries in the
    -- database bloat backups and slow every query on the table.
    stored_path   VARCHAR(500) NOT NULL,

    mime_type     VARCHAR(100),
    size_bytes    BIGINT NOT NULL,
    uploaded_by   INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    uploaded_at   TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT chk_attachment_parent CHECK (
        (ticket_id IS NOT NULL AND comment_id IS NULL) OR
        (ticket_id IS NULL AND comment_id IS NOT NULL)
    )
);

CREATE INDEX idx_attachments_ticket  ON attachments(ticket_id);
CREATE INDEX idx_attachments_comment ON attachments(comment_id);


-- ---------------------------------------------------------------------
--  Time tracking (requirement 12)
-- ---------------------------------------------------------------------
CREATE TABLE time_entries (
    id          SERIAL PRIMARY KEY,
    ticket_id   INTEGER NOT NULL REFERENCES tickets(id) ON DELETE CASCADE,
    user_id     INTEGER NOT NULL REFERENCES users(id)   ON DELETE RESTRICT,

    -- Minutes as an integer, not hours as a float. 0.1 hours cannot be stored
    -- exactly in binary floating point, and rounding errors in a report that
    -- someone bills from is a bad afternoon.
    minutes     INTEGER NOT NULL CHECK (minutes > 0),

    work_date   DATE NOT NULL DEFAULT CURRENT_DATE,
    note        VARCHAR(500),
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_time_ticket ON time_entries(ticket_id);
CREATE INDEX idx_time_user   ON time_entries(user_id, work_date);


-- ---------------------------------------------------------------------
--  Change history (requirement 13)
-- ---------------------------------------------------------------------
-- One row per changed field. Generic (field_name / old / new as text) rather
-- than a column per trackable field, so adding a new tracked field later
-- needs no migration.
CREATE TABLE ticket_history (
    id          SERIAL PRIMARY KEY,
    ticket_id   INTEGER NOT NULL REFERENCES tickets(id) ON DELETE CASCADE,
    changed_by  INTEGER NOT NULL REFERENCES users(id)   ON DELETE RESTRICT,
    field_name  VARCHAR(50) NOT NULL,   -- 'status', 'priority', 'assigned_to'
    old_value   TEXT,
    new_value   TEXT,
    changed_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_history_ticket ON ticket_history(ticket_id, changed_at DESC);


-- ---------------------------------------------------------------------
--  Notifications (requirement 14)
-- ---------------------------------------------------------------------
CREATE TABLE notifications (
    id          SERIAL PRIMARY KEY,
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ticket_id   INTEGER          REFERENCES tickets(id) ON DELETE CASCADE,
    message     VARCHAR(300) NOT NULL,
    is_read     BOOLEAN NOT NULL DEFAULT FALSE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Partial index: only unread rows are indexed, which is all the client polls
-- for. Smaller index, faster lookups.
CREATE INDEX idx_notifications_unread
    ON notifications(user_id, created_at DESC)
    WHERE is_read = FALSE;


-- ---------------------------------------------------------------------
--  Audit log (requirement 21)
-- ---------------------------------------------------------------------
-- Distinct from ticket_history: that tracks *what changed on a ticket*, this
-- tracks *who did something sensitive* — logins, permission changes, deletes.
CREATE TABLE audit_log (
    id          SERIAL PRIMARY KEY,
    user_id     INTEGER REFERENCES users(id) ON DELETE SET NULL,
    action      VARCHAR(100) NOT NULL,   -- 'login_success', 'user_deleted', ...
    entity_type VARCHAR(50),             -- 'ticket', 'user', ...
    entity_id   INTEGER,
    details     JSONB,                   -- free-form extras, queryable
    ip_address  INET,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_audit_user   ON audit_log(user_id, created_at DESC);
CREATE INDEX idx_audit_action ON audit_log(action, created_at DESC);


-- ---------------------------------------------------------------------
--  Convenience view (requirement 15)
-- ---------------------------------------------------------------------
-- Overdue detection lives here rather than being re-typed in the API, the
-- report and the dashboard. One definition, three consumers.
CREATE VIEW v_overdue_tickets AS
SELECT t.*
FROM tickets t
WHERE t.due_date IS NOT NULL
  AND t.due_date < CURRENT_DATE
  AND t.status NOT IN ('resolved', 'closed');
