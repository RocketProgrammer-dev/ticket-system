#pragma once

#include <QMainWindow>
#include <QModelIndex>
#include <QList>
#include <QString>

#include "comment.h"   // brings in UserRole
#include "notification.h"
#include "ticketquery.h"

class QTableView;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;

class TicketModel;
class CommentStore;
class ApiClient;
class QTimer;

// QMainWindow (not plain QWidget) because it gives you a menu bar, toolbars
// and a status bar for free. Use it for the primary window; use QWidget or
// QDialog for everything else.

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // The role decides which controls are visible. The server must enforce
    // permissions too (requirement 20) — hiding a button is convenience,
    // not security.
    // Role now lives in comment.h so the detail dialog can use the same type.
    MainWindow(const QString &username, UserRole role,
               ApiClient *api, QWidget *parent = nullptr);

signals:
    // The main window doesn't know how to show the login screen — main()
    // owns that transition, exactly as it does at startup.
    void logoutRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onNewTicket();
    void onRefresh();
    void onTicketDoubleClicked(const QModelIndex &index);
    void onTicketsReceived(const TicketPage &page);
    void onQueryChanged();          // any filter changed: back to page 1
    void onSortChanged(int column, Qt::SortOrder order);
    void onPreviousPage();
    void onNextPage();
    void onPageSizeChanged(int index);
    void onExportCsv();
    void onLanguageSelected(const QString &code);
    void onLogout();
    void onShowNotifications();
    void onNotificationsReceived(const QList<Notification> &items, int unreadCount);
    void openTicketById(int ticketId);
    void updateStatusBar();

private:
    void     buildToolBar();
    void     buildMenuBar();
    QWidget *buildFilterBar(QWidget *parent);

    QString m_username;
    UserRole m_role;

    TicketModel       *m_model;

    CommentStore      *m_commentStore;
    ApiClient         *m_api;          // not owned

    QTableView *m_table;
    QLineEdit  *m_searchEdit;
    QComboBox  *m_statusCombo;
    QComboBox  *m_priorityCombo;
    QCheckBox  *m_overdueCheck;
    QLabel     *m_countLabel;

    // Requirement 16
    TicketQuery  m_query;
    int          m_totalPages = 1;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QLabel      *m_pageLabel  = nullptr;
    QComboBox   *m_pageSizeCombo = nullptr;
    QTimer      *m_searchDebounce = nullptr;

    // Requirement 14
    QAction             *m_notificationAction = nullptr;
    QTimer              *m_notificationTimer  = nullptr;
    QList<Notification>  m_notifications;
};
