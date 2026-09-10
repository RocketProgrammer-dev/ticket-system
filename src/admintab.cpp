#include "admintab.h"
#include "apiclient.h"
#include "usereditdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QTimer>
#include <QLabel>

namespace {

// Role names for display. Not tr()'d through a switch in three places —
// one helper, used everywhere.
QString roleLabel(UserRole r)
{
    switch (r) {
    case UserRole::Manager:    return QObject::tr("Manager");
    case UserRole::Technician: return QObject::tr("Technician");
    default:                   return QObject::tr("Staff");
    }
}

} // namespace


AdminTab::AdminTab(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildUsersTab(),       tr("Users"));
    m_tabs->addTab(buildDepartmentsTab(), tr("Departments"));
    m_tabs->addTab(buildCategoriesTab(),  tr("Categories"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tabs);

    connect(m_api, &ApiClient::adminUsersReceived,
            this, &AdminTab::onUsersReceived);
    connect(m_api, &ApiClient::departmentsReceived,
            this, &AdminTab::onDepartmentsReceived);
    connect(m_api, &ApiClient::categoriesReceived,
            this, &AdminTab::onCategoriesReceived);

    // After any successful write, re-read everything. Simple and always
    // correct; a targeted refresh would be faster but easy to get wrong.
    connect(m_api, &ApiClient::adminSaved, this, &AdminTab::refresh);

    connect(m_api, &ApiClient::deleteRefused, this, &AdminTab::onDeleteRefused);
}

void AdminTab::refresh()
{
    // Keep whatever filter the user set. Resetting it after every save would
    // be maddening when working through a list.
    m_api->fetchAdminUsers(m_showInactiveCheck && m_showInactiveCheck->isChecked(),
                           m_userSearchEdit ? m_userSearchEdit->text() : QString());
    m_api->fetchDepartments();
    m_api->fetchCategories();
}

int AdminTab::selectedRow(QTableWidget *table) const
{
    const auto selection = table->selectionModel()->selectedRows();
    return selection.isEmpty() ? -1 : selection.first().row();
}

QWidget *AdminTab::buildUsersTab()
{
    auto *page = new QWidget(this);

    // --- Filter row -------------------------------------------------------
    m_userSearchEdit = new QLineEdit(page);
    m_userSearchEdit->setPlaceholderText(tr("Search by name, username or email..."));
    m_userSearchEdit->setClearButtonEnabled(true);

    m_showInactiveCheck = new QCheckBox(tr("Show inactive accounts"), page);

    // Typing "Mehmet" would otherwise fire six requests, one per keystroke.
    // The timer restarts on every change and only fires 350ms after the user
    // stops typing — one request instead of six.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(350);
    connect(m_searchDebounce, &QTimer::timeout, this, &AdminTab::onUserFilterChanged);

    connect(m_userSearchEdit, &QLineEdit::textChanged,
            m_searchDebounce, qOverload<>(&QTimer::start));

    // The checkbox is a deliberate click, so it applies immediately.
    connect(m_showInactiveCheck, &QCheckBox::toggled,
            this, &AdminTab::onUserFilterChanged);

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(m_userSearchEdit, 1);
    filterRow->addWidget(m_showInactiveCheck);

    m_usersTable = new QTableWidget(0, 6, page);
    m_usersTable->setHorizontalHeaderLabels({
        tr("Username"), tr("Full name"), tr("Email"),
        tr("Role"), tr("Department"), tr("Active")
    });
    m_usersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_usersTable->verticalHeader()->setVisible(false);
    m_usersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_usersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_usersTable->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *addButton    = new QPushButton(tr("Add user"), page);
    auto *editButton   = new QPushButton(tr("Edit selected"), page);
    auto *deleteButton = new QPushButton(tr("Delete selected"), page);

    connect(addButton,    &QPushButton::clicked, this, &AdminTab::onAddUser);
    connect(editButton,   &QPushButton::clicked, this, &AdminTab::onEditUser);
    connect(deleteButton, &QPushButton::clicked, this, &AdminTab::onDeleteUser);
    connect(m_usersTable, &QTableWidget::doubleClicked, this, &AdminTab::onEditUser);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(editButton);
    buttons->addWidget(deleteButton);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->addLayout(filterRow);
    layout->addWidget(m_usersTable);
    layout->addLayout(buttons);

    return page;
}

void AdminTab::onUserFilterChanged()
{
    m_api->fetchAdminUsers(m_showInactiveCheck->isChecked(),
                           m_userSearchEdit->text());
}

QWidget *AdminTab::buildDepartmentsTab()
{
    auto *page = new QWidget(this);

    m_departmentsTable = new QTableWidget(0, 2, page);
    m_departmentsTable->setHorizontalHeaderLabels({ tr("Name"), tr("Active") });
    m_departmentsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_departmentsTable->verticalHeader()->setVisible(false);
    m_departmentsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_departmentsTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *addButton  = new QPushButton(tr("Add department"), page);
    auto *editButton = new QPushButton(tr("Edit selected"), page);

    connect(addButton,  &QPushButton::clicked, this, &AdminTab::onAddDepartment);
    connect(editButton, &QPushButton::clicked, this, &AdminTab::onEditDepartment);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(editButton);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(m_departmentsTable);
    layout->addLayout(buttons);

    return page;
}

QWidget *AdminTab::buildCategoriesTab()
{
    auto *page = new QWidget(this);

    m_categoriesTable = new QTableWidget(0, 3, page);
    m_categoriesTable->setHorizontalHeaderLabels({
        tr("Name"), tr("Department"), tr("Active")
    });
    m_categoriesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_categoriesTable->verticalHeader()->setVisible(false);
    m_categoriesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_categoriesTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *addButton  = new QPushButton(tr("Add category"), page);
    auto *editButton = new QPushButton(tr("Edit selected"), page);

    connect(addButton,  &QPushButton::clicked, this, &AdminTab::onAddCategory);
    connect(editButton, &QPushButton::clicked, this, &AdminTab::onEditCategory);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(editButton);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(m_categoriesTable);
    layout->addLayout(buttons);

    return page;
}

void AdminTab::onUsersReceived(const QList<AdminUser> &users)
{
    m_users = users;

    // The server caps results at 200. Saying so beats silently truncating,
    // which looks like a missing user rather than a narrow enough search.
    if (users.size() >= 200) {
        m_userSearchEdit->setToolTip(
            tr("Showing the first 200 matches. Narrow the search to see more."));
    } else {
        m_userSearchEdit->setToolTip(QString());
    }
    m_usersTable->setRowCount(users.size());

    for (int i = 0; i < users.size(); ++i) {
        const AdminUser &u = users.at(i);

        // Department ids mean nothing to a reader, so resolve to a name.
        QString deptName = tr("(none)");
        for (const DepartmentInfo &d : m_departments) {
            if (d.id == u.departmentId) {
                deptName = d.name;
                break;
            }
        }

        m_usersTable->setItem(i, 0, new QTableWidgetItem(u.username));
        m_usersTable->setItem(i, 1, new QTableWidgetItem(u.fullName));
        m_usersTable->setItem(i, 2, new QTableWidgetItem(u.email));
        m_usersTable->setItem(i, 3, new QTableWidgetItem(roleLabel(u.role)));
        m_usersTable->setItem(i, 4, new QTableWidgetItem(deptName));
        m_usersTable->setItem(i, 5,
            new QTableWidgetItem(u.isActive ? tr("Yes") : tr("No")));

        if (!u.isActive) {
            // Grey out deactivated accounts so they read as historical
            // records rather than current staff.
            for (int c = 0; c < m_usersTable->columnCount(); ++c)
                m_usersTable->item(i, c)->setForeground(QBrush(QColor("#9e9e9e")));
        }
    }
}

void AdminTab::onDepartmentsReceived(const QList<DepartmentInfo> &departments)
{
    m_departments = departments;
    m_departmentsTable->setRowCount(departments.size());

    for (int i = 0; i < departments.size(); ++i) {
        m_departmentsTable->setItem(i, 0, new QTableWidgetItem(departments.at(i).name));
        m_departmentsTable->setItem(i, 1,
            new QTableWidgetItem(departments.at(i).isActive ? tr("Yes") : tr("No")));
    }

    // The users table shows department names, so it needs redrawing whenever
    // the department list arrives (which may be after the users did).
    if (!m_users.isEmpty())
        onUsersReceived(m_users);
}

void AdminTab::onCategoriesReceived(const QList<CategoryInfo> &categories)
{
    m_categories = categories;
    m_categoriesTable->setRowCount(categories.size());

    for (int i = 0; i < categories.size(); ++i) {
        const CategoryInfo &c = categories.at(i);
        m_categoriesTable->setItem(i, 0, new QTableWidgetItem(c.name));
        m_categoriesTable->setItem(i, 1, new QTableWidgetItem(
            c.departmentName.isEmpty() ? tr("(none)") : c.departmentName));
        m_categoriesTable->setItem(i, 2,
            new QTableWidgetItem(c.isActive ? tr("Yes") : tr("No")));
    }
}

void AdminTab::onAddUser()
{
    UserEditDialog dialog(nullptr, m_departments, this);
    if (dialog.exec() == QDialog::Accepted)
        m_api->saveUser(dialog.user(), dialog.password());
}

void AdminTab::onEditUser()
{
    const int row = selectedRow(m_usersTable);
    if (row < 0 || row >= m_users.size()) {
        QMessageBox::information(this, tr("Edit user"), tr("Select a user first."));
        return;
    }

    UserEditDialog dialog(&m_users[row], m_departments, this);
    if (dialog.exec() == QDialog::Accepted)
        m_api->saveUser(dialog.user(), dialog.password());
}

void AdminTab::onAddDepartment()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New department"), tr("Department name:"),
        QLineEdit::Normal, QString(), &ok);

    if (!ok || name.trimmed().isEmpty())
        return;

    DepartmentInfo d;
    d.id       = 0;          // 0 means create
    d.name     = name.trimmed();
    d.isActive = true;
    m_api->saveDepartment(d);
}

void AdminTab::onEditDepartment()
{
    const int row = selectedRow(m_departmentsTable);
    if (row < 0 || row >= m_departments.size()) {
        QMessageBox::information(this, tr("Edit department"),
                                 tr("Select a department first."));
        return;
    }

    DepartmentInfo d = m_departments.at(row);

    // A one-off dialog built inline. Worth a dedicated class only if it grows
    // beyond a couple of fields.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit department"));

    auto *nameEdit    = new QLineEdit(d.name, &dialog);
    auto *activeCheck = new QCheckBox(tr("Active"), &dialog);
    activeCheck->setChecked(d.isActive);

    auto *form = new QFormLayout;
    form->addRow(tr("Name:"), nameEdit);
    form->addRow(QString(),   activeCheck);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    if (nameEdit->text().trimmed().isEmpty())
        return;

    d.name     = nameEdit->text().trimmed();
    d.isActive = activeCheck->isChecked();
    m_api->saveDepartment(d);
}

void AdminTab::onAddCategory()
{
    CategoryInfo c;
    c.id = 0;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("New category"));

    auto *nameEdit = new QLineEdit(&dialog);
    auto *deptCombo = new QComboBox(&dialog);
    deptCombo->addItem(tr("(none)"), 0);
    for (const DepartmentInfo &d : m_departments) {
        if (d.isActive)
            deptCombo->addItem(d.name, d.id);
    }

    auto *form = new QFormLayout;
    form->addRow(tr("Name:"),       nameEdit);
    form->addRow(tr("Department:"), deptCombo);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted || nameEdit->text().trimmed().isEmpty())
        return;

    c.name         = nameEdit->text().trimmed();
    c.departmentId = deptCombo->currentData().toInt();
    c.isActive     = true;
    m_api->saveCategory(c);
}

void AdminTab::onEditCategory()
{
    const int row = selectedRow(m_categoriesTable);
    if (row < 0 || row >= m_categories.size()) {
        QMessageBox::information(this, tr("Edit category"),
                                 tr("Select a category first."));
        return;
    }

    CategoryInfo c = m_categories.at(row);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit category"));

    auto *nameEdit  = new QLineEdit(c.name, &dialog);
    auto *deptCombo = new QComboBox(&dialog);
    deptCombo->addItem(tr("(none)"), 0);
    for (const DepartmentInfo &d : m_departments) {
        if (d.isActive || d.id == c.departmentId)
            deptCombo->addItem(d.name, d.id);
    }
    const int deptIndex = deptCombo->findData(c.departmentId);
    if (deptIndex >= 0)
        deptCombo->setCurrentIndex(deptIndex);

    auto *activeCheck = new QCheckBox(tr("Active"), &dialog);
    activeCheck->setChecked(c.isActive);

    auto *form = new QFormLayout;
    form->addRow(tr("Name:"),       nameEdit);
    form->addRow(tr("Department:"), deptCombo);
    form->addRow(QString(),         activeCheck);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted || nameEdit->text().trimmed().isEmpty())
        return;

    c.name         = nameEdit->text().trimmed();
    c.departmentId = deptCombo->currentData().toInt();
    c.isActive     = activeCheck->isChecked();
    m_api->saveCategory(c);
}


void AdminTab::onDeleteUser()
{
    const int row = selectedRow(m_usersTable);
    if (row < 0 || row >= m_users.size()) {
        QMessageBox::information(this, tr("Delete user"), tr("Select a user first."));
        return;
    }

    const AdminUser &u = m_users.at(row);

    // Naming the user in the prompt, not just "this user". Deleting the wrong
    // row because the confirmation was generic is an avoidable mistake.
    const auto answer = QMessageBox::question(
        this, tr("Delete user"),
        tr("Permanently delete %1 (%2)?\n\n"
           "This cannot be undone. If the user has any tickets or comments, "
           "deletion will be refused and you will be offered deactivation instead.")
            .arg(u.fullName, u.username),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes)
        return;

    m_api->deleteUser(u.id);
}

void AdminTab::onDeleteRefused(int userId, const QString &reason)
{
    // Find the user again: the server refused by id, and we want their name.
    AdminUser target;
    for (const AdminUser &u : m_users) {
        if (u.id == userId) {
            target = u;
            break;
        }
    }

    if (target.id == 0)
        return;

    const auto answer = QMessageBox::question(
        this, tr("Cannot delete"),
        reason + tr("\n\nDeactivate %1 now?").arg(target.fullName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (answer != QMessageBox::Yes)
        return;

    // Deactivation is an ordinary update, so it reuses the save path.
    // An empty password means "leave the existing one alone".
    target.isActive = false;
    m_api->saveUser(target, QString());
}
