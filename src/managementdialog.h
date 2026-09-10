#pragma once

#include <QDialog>

#include "user.h"

class QTabWidget;
class QLabel;
class QTableWidget;
class ApiClient;
class BarChartWidget;
class AdminTab;
class AuditTab;
class QAbstractItemModel;

// Requirement 17: the manager's dashboard and reporting screen, in one window
// with tabs. Requirement 19's administration screens will become a third tab
// here rather than a separate window — one "Management" entry point is easier
// to explain than three scattered buttons.

class ManagementDialog : public QDialog
{
    Q_OBJECT

public:
    // ticketModel is the proxy from the main window, so "export what I'm
    // looking at" stays consistent with the ticket list.
    ManagementDialog(ApiClient *api, QAbstractItemModel *ticketModel,
                     QWidget *parent = nullptr);

private slots:
    void onDashboardReceived(const DashboardStats &stats);
    void onExportReport();
    void onRequestFailed(const QString &message);

private:
    QWidget *buildDashboardTab();
    QWidget *buildReportsTab();

    QLabel *makeStatCard(const QString &caption, const QString &color);

    ApiClient          *m_api;          // not owned
    QAbstractItemModel *m_ticketModel;  // not owned

    QTabWidget *m_tabs;
    QLabel     *m_statusLabel;   // errors must surface HERE, not behind the dialog

    QLabel *m_totalCard;
    QLabel *m_overdueCard;
    QLabel *m_unassignedCard;
    QLabel *m_openCard;

    BarChartWidget *m_statusChart;
    BarChartWidget *m_priorityChart;
    QTableWidget   *m_workloadTable;
    AdminTab       *m_adminTab;
    AuditTab       *m_auditTab;
};
