#pragma once

#include <QWidget>

#include "audittypes.h"

class QTableWidget;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QTimer;
class ApiClient;

// Requirement 21's read side. An audit log nobody can read is a table that
// grows forever and answers no questions.

class AuditTab : public QWidget
{
    Q_OBJECT

public:
    explicit AuditTab(ApiClient *api, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onPageReceived(const AuditPage &page);
    void onActionsReceived(const QStringList &actions);
    void onFilterChanged();
    void onPreviousPage();
    void onNextPage();

private:
    ApiClient *m_api;   // not owned

    QTableWidget *m_table;
    QComboBox    *m_actionCombo;
    QLineEdit    *m_searchEdit;
    QTimer       *m_debounce;
    QPushButton  *m_prevButton;
    QPushButton  *m_nextButton;
    QLabel       *m_pageLabel;

    int m_page = 1;
    int m_totalPages = 1;
};
