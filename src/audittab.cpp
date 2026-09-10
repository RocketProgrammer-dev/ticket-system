#include "audittab.h"
#include "apiclient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QBrush>
#include <QColor>
#include <QFont>

namespace {

// Raw action names are readable but ugly in a table. Translating them also
// means the log respects the language setting, which matters if a Turkish
// manager is the one auditing.
QString actionLabel(const QString &action)
{
    if (action == "login_success")  return QObject::tr("Signed in");
    if (action == "login_failed")   return QObject::tr("Failed sign-in");
    if (action == "ticket_created") return QObject::tr("Ticket created");
    if (action == "ticket_updated") return QObject::tr("Ticket changed");
    if (action == "user_created")   return QObject::tr("User created");
    if (action == "user_updated")   return QObject::tr("User changed");
    if (action == "user_deleted")   return QObject::tr("User deleted");

    // Anything not in the list still displays, just unprettified. Better than
    // showing a blank cell for an action added later.
    return action;
}

// The rows worth noticing at a glance.
bool isSecuritySensitive(const QString &action)
{
    return action == "login_failed"
        || action == "user_deleted"
        || action == "user_created"
        || action == "user_updated";
}

} // namespace


AuditTab::AuditTab(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    m_actionCombo = new QComboBox(this);
    m_actionCombo->addItem(tr("All actions"), QString());

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search by person or action..."));
    m_searchEdit->setClearButtonEnabled(true);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(350);
    connect(m_debounce, &QTimer::timeout, this, &AuditTab::onFilterChanged);
    connect(m_searchEdit, &QLineEdit::textChanged,
            m_debounce, qOverload<>(&QTimer::start));
    connect(m_actionCombo, &QComboBox::currentIndexChanged,
            this, &AuditTab::onFilterChanged);

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(m_searchEdit, 1);
    filterRow->addWidget(m_actionCombo);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({
        tr("When"), tr("Who"), tr("Action"), tr("Details")
    });
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_prevButton = new QPushButton(tr("< Previous"), this);
    m_nextButton = new QPushButton(tr("Next >"), this);
    m_pageLabel  = new QLabel(tr("Page 1 of 1"), this);

    connect(m_prevButton, &QPushButton::clicked, this, &AuditTab::onPreviousPage);
    connect(m_nextButton, &QPushButton::clicked, this, &AuditTab::onNextPage);

    auto *pageRow = new QHBoxLayout;
    pageRow->addStretch();
    pageRow->addWidget(m_prevButton);
    pageRow->addWidget(m_pageLabel);
    pageRow->addWidget(m_nextButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(filterRow);
    layout->addWidget(m_table, 1);
    layout->addLayout(pageRow);

    connect(m_api, &ApiClient::auditPageReceived,
            this, &AuditTab::onPageReceived);
    connect(m_api, &ApiClient::auditActionsReceived,
            this, &AuditTab::onActionsReceived);
}

void AuditTab::refresh()
{
    m_api->fetchAuditActions();
    m_api->fetchAuditLog(m_page, m_actionCombo->currentData().toString(),
                         m_searchEdit->text());
}

void AuditTab::onActionsReceived(const QStringList &actions)
{
    const QString current = m_actionCombo->currentData().toString();

    m_actionCombo->clear();
    m_actionCombo->addItem(tr("All actions"), QString());

    for (const QString &a : actions)
        m_actionCombo->addItem(actionLabel(a), a);

    // Restore the previous selection: refresh() is called whenever the tab is
    // opened, and silently resetting the filter would be infuriating.
    const int index = m_actionCombo->findData(current);
    if (index >= 0)
        m_actionCombo->setCurrentIndex(index);
}

void AuditTab::onPageReceived(const AuditPage &page)
{
    m_page       = page.page;
    m_totalPages = page.totalPages;

    m_pageLabel->setText(tr("Page %1 of %2  (%3 entries)")
                             .arg(page.page).arg(page.totalPages).arg(page.totalCount));
    m_prevButton->setEnabled(page.page > 1);
    m_nextButton->setEnabled(page.page < page.totalPages);

    m_table->setRowCount(page.items.size());

    for (int i = 0; i < page.items.size(); ++i) {
        const AuditEntry &a = page.items.at(i);

        m_table->setItem(i, 0, new QTableWidgetItem(
            a.createdAt.toLocalTime().toString("dd.MM.yyyy HH:mm:ss")));
        m_table->setItem(i, 1, new QTableWidgetItem(a.userName));
        m_table->setItem(i, 2, new QTableWidgetItem(actionLabel(a.action)));
        m_table->setItem(i, 3, new QTableWidgetItem(a.details));

        if (a.action == "login_failed") {
            // Failed sign-ins are what someone scans this table looking for.
            for (int c = 0; c < m_table->columnCount(); ++c)
                m_table->item(i, c)->setForeground(QBrush(QColor("#c62828")));
        } else if (isSecuritySensitive(a.action)) {
            for (int c = 0; c < m_table->columnCount(); ++c) {
                QFont f = m_table->item(i, c)->font();
                f.setBold(true);
                m_table->item(i, c)->setFont(f);
            }
        }
    }

    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
}

void AuditTab::onFilterChanged()
{
    m_page = 1;
    m_api->fetchAuditLog(m_page, m_actionCombo->currentData().toString(),
                         m_searchEdit->text());
}

void AuditTab::onPreviousPage()
{
    if (m_page <= 1)
        return;

    m_page--;
    m_api->fetchAuditLog(m_page, m_actionCombo->currentData().toString(),
                         m_searchEdit->text());
}

void AuditTab::onNextPage()
{
    if (m_page >= m_totalPages)
        return;

    m_page++;
    m_api->fetchAuditLog(m_page, m_actionCombo->currentData().toString(),
                         m_searchEdit->text());
}
