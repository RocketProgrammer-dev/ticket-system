#pragma once

#include <QWidget>
#include <QList>

#include "admintypes.h"

class QTableWidget;
class QTabWidget;
class ApiClient;
class QLineEdit;
class QCheckBox;
class QTimer;

// Requirement 19, as a tab inside the Management window rather than a separate
// screen. Three sub-tabs sharing one pattern: a table, an Add button, and an
// Edit button that works on the selected row.

class AdminTab : public QWidget
{
    Q_OBJECT

public:
    explicit AdminTab(ApiClient *api, QWidget *parent = nullptr);

    void refresh();   // called when the tab is first shown

private slots:
    void onUsersReceived(const QList<AdminUser> &users);
    void onDepartmentsReceived(const QList<DepartmentInfo> &departments);
    void onCategoriesReceived(const QList<CategoryInfo> &categories);

    void onAddUser();
    void onEditUser();
    void onDeleteUser();
    void onUserFilterChanged();
    void onDeleteRefused(int userId, const QString &reason);
    void onAddDepartment();
    void onEditDepartment();
    void onAddCategory();
    void onEditCategory();

private:
    QWidget *buildUsersTab();
    QWidget *buildDepartmentsTab();
    QWidget *buildCategoriesTab();

    // Returns -1 when nothing is selected, so callers can bail out cleanly.
    int selectedRow(QTableWidget *table) const;

    ApiClient *m_api;   // not owned

    QTabWidget   *m_tabs;
    QTableWidget *m_usersTable;
    QLineEdit    *m_userSearchEdit;
    QCheckBox    *m_showInactiveCheck;
    QTimer       *m_searchDebounce;
    QTableWidget *m_departmentsTable;
    QTableWidget *m_categoriesTable;

    // Kept so edit dialogs can be populated without another round trip.
    QList<AdminUser>      m_users;
    QList<DepartmentInfo> m_departments;
    QList<CategoryInfo>   m_categories;
};
