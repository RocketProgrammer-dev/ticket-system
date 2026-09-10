#include "mainwindow.h"
#include "ticketmodel.h"
#include "ticketdetaildialog.h"
#include "csvexporter.h"
#include "commentstore.h"
#include "apiclient.h"
#include "newticketdialog.h"
#include "language.h"
#include "managementdialog.h"
#include "notificationsdialog.h"
#include "settings.h"
#include "logging.h"

#include <QTableView>
#include <QHeaderView>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>        // in Qt6 QAction moved from QtWidgets to QtGui
#include <QKeySequence>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMenuBar>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QTimer>
#include <QPushButton>
#include <QFileDialog>
#include <QStandardPaths>
#include <QItemSelectionModel>
#include <QDate>
#include <QDebug>

MainWindow::MainWindow(const QString &username, UserRole role,
                       ApiClient *api, QWidget *parent)
    : QMainWindow(parent)
    , m_username(username)
    , m_role(role)
    , m_api(api)
{
    setWindowTitle(tr("Ticket System — %1").arg(username));
    // Restore the size and position from last time, falling back to a
    // sensible default on first run.
    const QByteArray geometry = Settings::mainWindowGeometry();
    if (geometry.isEmpty())
        resize(1000, 600);
    else
        restoreGeometry(geometry);

    // --- Model layer -------------------------------------------------------
    m_model = new TicketModel(this);
    m_commentStore = new CommentStore(m_api, this);

    // --- Central widget ----------------------------------------------------
    // QMainWindow needs one central widget; everything else goes inside it.
    auto *central = new QWidget(this);
    auto *layout  = new QVBoxLayout(central);

    layout->addWidget(buildFilterBar(central));

    m_table = new QTableView(central);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);  // Ctrl/Shift click
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // read-only grid
    m_table->setAlternatingRowColors(true);
    // Sorting is done by the SERVER now, so Qt must not also sort the page
    // locally — that would reorder 25 rows instead of choosing which 25.
    // The header stays clickable; we intercept the click and re-query.
    m_table->setSortingEnabled(false);
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    m_table->horizontalHeader()->setSortIndicator(
        TicketModel::Col_Priority, Qt::DescendingOrder);
    m_table->verticalHeader()->setVisible(false);                 // hide 1,2,3... column
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(
        TicketModel::Col_Title, QHeaderView::Stretch);            // subject takes the slack

    layout->addWidget(m_table);

    // --- Pagination bar (requirement 16) ----------------------------------
    m_prevButton = new QPushButton(tr("< Previous"), central);
    m_nextButton = new QPushButton(tr("Next >"), central);
    m_pageLabel  = new QLabel(tr("Page 1 of 1"), central);

    m_pageSizeCombo = new QComboBox(central);
    m_pageSizeCombo->addItem(tr("25 per page"),  25);
    m_pageSizeCombo->addItem(tr("50 per page"),  50);
    m_pageSizeCombo->addItem(tr("100 per page"), 100);
    // The largest option exists so a report can be exported in one go; the
    // server clamps anything above 1000 regardless.
    m_pageSizeCombo->addItem(tr("1000 per page"), 1000);

    connect(m_prevButton, &QPushButton::clicked, this, &MainWindow::onPreviousPage);
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::onNextPage);
    connect(m_pageSizeCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onPageSizeChanged);

    auto *pageRow = new QHBoxLayout;
    pageRow->addWidget(m_pageSizeCombo);
    pageRow->addStretch();
    pageRow->addWidget(m_prevButton);
    pageRow->addWidget(m_pageLabel);
    pageRow->addWidget(m_nextButton);
    layout->addLayout(pageRow);

    setCentralWidget(central);

    buildToolBar();
    buildMenuBar();

    // --- Status bar --------------------------------------------------------
    m_countLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_countLabel);

    // --- Connections -------------------------------------------------------
    connect(m_table, &QTableView::doubleClicked,
            this, &MainWindow::onTicketDoubleClicked);

    // Searching hits the database now, so debounce it: one request when the
    // user stops typing, not one per keystroke.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(350);
    connect(m_searchDebounce, &QTimer::timeout, this, &MainWindow::onQueryChanged);

    connect(m_searchEdit, &QLineEdit::textChanged,
            m_searchDebounce, qOverload<>(&QTimer::start));

    connect(m_statusCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onQueryChanged);
    connect(m_priorityCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onQueryChanged);
    connect(m_overdueCheck, &QCheckBox::toggled,
            this, &MainWindow::onQueryChanged);

    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &MainWindow::onSortChanged);


    // The list arrives asynchronously: the table is empty until the reply
    // lands, then setTickets() fills it and the view redraws itself.
    connect(m_api, &ApiClient::ticketsReceived,
            this, &MainWindow::onTicketsReceived);

    connect(m_api, &ApiClient::requestFailed, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 6000);
    });

    // After a successful create, re-read the list so the new row appears with
    // the number and timestamps the server assigned.
    connect(m_api, &ApiClient::ticketCreated, this, [this](const QString &number) {
        statusBar()->showMessage(tr("Ticket %1 created.").arg(number), 5000);
        m_api->fetchTickets(m_query);
    });

    connect(m_api, &ApiClient::notificationsReceived,
            this, &MainWindow::onNotificationsReceived);

    // Re-poll immediately after anything is marked read, so the count in the
    // toolbar updates without waiting for the next tick.
    connect(m_api, &ApiClient::notificationsChanged,
            m_api, &ApiClient::fetchNotifications);

    // Polling, not push. A WebSocket would be tidier but adds a protocol,
    // reconnection logic and a whole class of bugs; 30-second polling is
    // entirely adequate for a ticket system and takes four lines.
    m_notificationTimer = new QTimer(this);
    m_notificationTimer->setInterval(30 * 1000);
    connect(m_notificationTimer, &QTimer::timeout,
            m_api, &ApiClient::fetchNotifications);
    m_notificationTimer->start();

    m_query.sortBy         = "priority";
    m_query.sortDescending = true;

    m_api->fetchTickets(m_query);
    m_api->fetchNotifications();
    updateStatusBar();
}

void MainWindow::onNotificationsReceived(const QList<Notification> &items,
                                         int unreadCount)
{
    m_notifications = items;

    m_notificationAction->setText(unreadCount > 0
        ? tr("Notifications (%1)").arg(unreadCount)
        : tr("Notifications"));
}

void MainWindow::onLogout()
{
    const auto answer = QMessageBox::question(
        this, tr("Log out"),
        tr("Log out of %1?").arg(m_username),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes)
        return;

    // Stop polling before anything else. A timer that fires during teardown
    // would send a request with a token that has just been discarded.
    if (m_notificationTimer)
        m_notificationTimer->stop();

    Settings::setMainWindowGeometry(saveGeometry());

    qInfo() << "User logged out:" << m_username;

    emit logoutRequested();
}

void MainWindow::onShowNotifications()
{
    auto *dialog = new NotificationsDialog(m_notifications, m_api, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    connect(dialog, &NotificationsDialog::ticketRequested,
            this, &MainWindow::openTicketById);

    dialog->exec();
}

void MainWindow::openTicketById(int ticketId)
{
    const int sourceRow = m_model->rowForTicketId(ticketId);

    if (sourceRow < 0) {
        // The ticket isn't in the current list, usually because a filter is
        // hiding it. Saying so is better than doing nothing and looking broken.
        statusBar()->showMessage(
            tr("That ticket is not in the current list. Try clearing the filters."),
            5000);
        return;
    }

    // Reuse the existing double-click path so there is one place that opens a
    // ticket, not two that can drift apart.
    const QModelIndex sourceIndex = m_model->index(sourceRow, 0);
    onTicketDoubleClicked(sourceIndex);
}

void MainWindow::buildMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));

    auto *logoutAction = fileMenu->addAction(tr("Log &out"));
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);

    fileMenu->addSeparator();

    auto *exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto *settingsMenu = menuBar()->addMenu(tr("&Settings"));

    // Opens the log folder in the system file manager. Sounds trivial, but it
    // turns "send me your log file" from a support conversation into a click.
    auto *logAction = settingsMenu->addAction(tr("Open &log folder"));
    connect(logAction, &QAction::triggered, []() {
        const QString dir = QFileInfo(Logging::logFilePath()).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    settingsMenu->addSeparator();
    auto *languageMenu = settingsMenu->addMenu(tr("&Language"));

    // An action group makes the entries mutually exclusive and gives them
    // radio-button appearance automatically.
    auto *group = new QActionGroup(this);
    group->setExclusive(true);

    const QString current = Language::currentCode();

    for (const Language::Entry &entry : Language::available()) {
        auto *action = languageMenu->addAction(entry.displayName);
        action->setCheckable(true);
        action->setChecked(entry.code == current);
        group->addAction(action);

        // Capture the code by value: `entry` is a loop variable and will be
        // gone by the time anyone clicks this.
        const QString code = entry.code;
        connect(action, &QAction::triggered, this, [this, code]() {
            onLanguageSelected(code);
        });
    }
}

void MainWindow::onLanguageSelected(const QString &code)
{
    if (code == Language::currentCode())
        return;

    Language::setCurrentCode(code);

    // Qt applies translations when widgets are constructed, so existing
    // windows keep their old text. Retranslating live would mean writing a
    // retranslateUi() for every window; asking for a restart is the normal
    // desktop compromise.
    QMessageBox::information(
        this,
        tr("Language"),
        tr("The language will change the next time the application starts."));
}

void MainWindow::buildToolBar()
{
    auto *bar = addToolBar(tr("Main"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // QAction is Qt's "one command, many entry points" object: the same action
    // can appear in a toolbar, a menu and a keyboard shortcut.
    auto *newAction = bar->addAction(tr("New ticket"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewTicket);

    auto *refreshAction = bar->addAction(tr("Refresh"));
    refreshAction->setShortcut(QKeySequence::Refresh);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::onRefresh);

    // Requirement 14. The label carries the unread count, so the toolbar
    // itself is the indicator — no separate badge widget needed.
    m_notificationAction = bar->addAction(tr("Notifications"));
    connect(m_notificationAction, &QAction::triggered,
            this, &MainWindow::onShowNotifications);


    // Role-based UI. The API must repeat these checks server-side.
    bar->addSeparator();

    // Export is available to everyone: it can only ever write out rows the
    // user is already looking at. Gating it behind Manager was the bug — with
    // the app hardcoded to log in as Staff, the button never appeared at all.
    auto *exportAction = bar->addAction(tr("Export CSV"));
    exportAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportCsv);

    // One entry point for managers: dashboard, reports, and later the
    // administration screens, all as tabs in the same window.
    if (canAccessReports(m_role)) {
        bar->addSeparator();
        auto *manageAction = bar->addAction(tr("Management"));
        connect(manageAction, &QAction::triggered, this, [this]() {
            ManagementDialog dialog(m_api, m_model, this);
            dialog.exec();
        });
    }
}

QWidget *MainWindow::buildFilterBar(QWidget *parent)
{
    auto *bar = new QWidget(parent);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit(bar);
    m_searchEdit->setPlaceholderText(
        tr("Search ticket no, subject, requester or assignee..."));
    m_searchEdit->setClearButtonEnabled(true);

    // addItem's second argument is user data. Storing the enum here means the
    // slot doesn't have to map combo positions to statuses by hand.
    m_statusCombo = new QComboBox(bar);
    m_statusCombo->addItem(tr("All statuses"), QVariant());
    m_statusCombo->addItem(statusToString(TicketStatus::Open),       static_cast<int>(TicketStatus::Open));
    m_statusCombo->addItem(statusToString(TicketStatus::InProgress), static_cast<int>(TicketStatus::InProgress));
    m_statusCombo->addItem(statusToString(TicketStatus::Waiting),    static_cast<int>(TicketStatus::Waiting));
    m_statusCombo->addItem(statusToString(TicketStatus::Resolved),   static_cast<int>(TicketStatus::Resolved));
    m_statusCombo->addItem(statusToString(TicketStatus::Closed),     static_cast<int>(TicketStatus::Closed));

    m_priorityCombo = new QComboBox(bar);
    m_priorityCombo->addItem(tr("All priorities"), QVariant());
    m_priorityCombo->addItem(priorityToString(TicketPriority::Low),      static_cast<int>(TicketPriority::Low));
    m_priorityCombo->addItem(priorityToString(TicketPriority::Normal),   static_cast<int>(TicketPriority::Normal));
    m_priorityCombo->addItem(priorityToString(TicketPriority::High),     static_cast<int>(TicketPriority::High));
    m_priorityCombo->addItem(priorityToString(TicketPriority::Critical), static_cast<int>(TicketPriority::Critical));

    m_overdueCheck = new QCheckBox(tr("Overdue only"), bar);

    row->addWidget(m_searchEdit, 1);   // stretch factor 1: search grows, combos don't
    row->addWidget(m_statusCombo);
    row->addWidget(m_priorityCombo);
    row->addWidget(m_overdueCheck);

    return bar;
}

void MainWindow::onQueryChanged()
{
    // Any filter change resets to page 1. Staying on page 5 of a result set
    // that now has two pages would show an empty table.
    m_query.page   = 1;
    m_query.search = m_searchEdit->text();

    const QVariant statusData = m_statusCombo->currentData();
    m_query.status = statusData.isValid()
        ? std::optional<TicketStatus>(static_cast<TicketStatus>(statusData.toInt()))
        : std::nullopt;

    const QVariant priorityData = m_priorityCombo->currentData();
    m_query.priority = priorityData.isValid()
        ? std::optional<TicketPriority>(static_cast<TicketPriority>(priorityData.toInt()))
        : std::nullopt;

    m_query.overdueOnly = m_overdueCheck->isChecked();

    m_api->fetchTickets(m_query);
}

void MainWindow::onSortChanged(int column, Qt::SortOrder order)
{
    // Map the column to the name the API expects. A switch rather than an
    // array so an added column fails to compile rather than silently sorting
    // by the wrong field.
    switch (column) {
    case TicketModel::Col_Number:    m_query.sortBy = "number";    break;
    case TicketModel::Col_Title:     m_query.sortBy = "title";     break;
    case TicketModel::Col_Status:    m_query.sortBy = "status";    break;
    case TicketModel::Col_Priority:  m_query.sortBy = "priority";  break;
    case TicketModel::Col_CreatedBy: m_query.sortBy = "createdby"; break;
    case TicketModel::Col_Assignee:  m_query.sortBy = "assignee";  break;
    case TicketModel::Col_DueDate:   m_query.sortBy = "duedate";   break;
    default: return;
    }

    m_query.sortDescending = (order == Qt::DescendingOrder);
    m_query.page = 1;
    m_api->fetchTickets(m_query);
}

void MainWindow::onPreviousPage()
{
    if (m_query.page <= 1)
        return;

    m_query.page--;
    m_api->fetchTickets(m_query);
}

void MainWindow::onNextPage()
{
    if (m_query.page >= m_totalPages)
        return;

    m_query.page++;
    m_api->fetchTickets(m_query);
}

void MainWindow::onPageSizeChanged(int index)
{
    m_query.pageSize = m_pageSizeCombo->itemData(index).toInt();
    m_query.page     = 1;
    m_api->fetchTickets(m_query);
}

void MainWindow::onTicketsReceived(const TicketPage &page)
{
    m_model->setTickets(page.items);

    m_query.page = page.page;
    m_totalPages = page.totalPages;

    m_pageLabel->setText(tr("Page %1 of %2").arg(page.page).arg(page.totalPages));
    m_prevButton->setEnabled(page.page > 1);
    m_nextButton->setEnabled(page.page < page.totalPages);

    m_countLabel->setText(tr("%1 tickets").arg(page.totalCount));
}

void MainWindow::onTicketDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    // No proxy any more: the view reads the model directly, because filtering
    // and sorting moved to the server. So the row IS the model row.
    const Ticket &t = m_model->ticketAt(index.row());

    // Pass the shared store, not a copy. Comments now survive closing the
    // dialog, and each ticket keeps its own conversation.
    auto *dialog = new TicketDetailDialog(t, m_role, m_commentStore, m_api, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    // Accepted means the ticket was changed, so re-read the list to pick up
    // the new status, priority or assignee.
    connect(dialog, &QDialog::accepted, this, [this]() {
        m_api->fetchTickets(m_query);
    });

    // open() shows it without blocking, unlike exec(). Prefer open() so the
    // rest of the app stays responsive and background refreshes keep running.
    dialog->open();
}

void MainWindow::onNewTicket()
{
    NewTicketDialog dialog(this);

    // exec() blocks here until the user closes the dialog, and returns
    // QDialog::Accepted or Rejected. Modal is right for a form: there is no
    // sensible thing to do with the main window while filling it in.
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_api->createTicket(dialog.title(),
                        dialog.description(),
                        dialog.priority(),
                        dialog.dueDate());

    statusBar()->showMessage(tr("Creating ticket..."), 2000);
}

void MainWindow::onRefresh()
{
    m_api->fetchTickets(m_query);
    statusBar()->showMessage(tr("Refreshing..."), 2000);
}

void MainWindow::onExportCsv()
{
    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested =
        defaultDir + "/tickets_" + QDate::currentDate().toString("yyyyMMdd") + ".csv";

    // If rows are selected, export only those. Otherwise export everything
    // currently visible. No extra button needed — the selection says what the
    // user means.
    QList<int> selectedRows;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        selectedRows.append(idx.row());

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export tickets"), suggested, tr("CSV files (*.csv)"));

    if (path.isEmpty())
        return;   // user cancelled — not an error, just stop

    // Exports the page currently on screen. To export everything matching the
    // filters, set the page size to 1000 first — that is what the size
    // selector's largest option is for.
    QString error;
    if (CsvExporter::exportToFile(m_model, path, &error, selectedRows)) {
        const int count = selectedRows.isEmpty() ? m_model->rowCount()
                                                 : selectedRows.size();
        statusBar()->showMessage(
            selectedRows.isEmpty() ? tr("Exported all %1 visible tickets.").arg(count)
                                   : tr("Exported %1 selected tickets.").arg(count),
            4000);
    } else {
        QMessageBox::warning(this, tr("Export failed"), error);
    }
}

void MainWindow::updateStatusBar()
{
    // Filled in by onTicketsReceived from the server's total, which is the
    // number of MATCHING tickets, not the number currently on screen.
    if (m_countLabel->text().isEmpty())
        m_countLabel->setText(tr("Loading..."));
}



void MainWindow::closeEvent(QCloseEvent *event)
{
    // Save on the way out. saveGeometry() encodes size, position and which
    // screen the window was on, which is why it returns an opaque QByteArray
    // rather than four integers.
    Settings::setMainWindowGeometry(saveGeometry());
    QMainWindow::closeEvent(event);
}
