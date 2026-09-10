#include "managementdialog.h"
#include "apiclient.h"
#include "barchartwidget.h"
#include "csvexporter.h"
#include "admintab.h"
#include "audittab.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDate>

ManagementDialog::ManagementDialog(ApiClient *api, QAbstractItemModel *ticketModel,
                                   QWidget *parent)
    : QDialog(parent)
    , m_api(api)
    , m_ticketModel(ticketModel)
{
    setWindowTitle(tr("Management"));
    resize(760, 640);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildDashboardTab(), tr("Dashboard"));
    m_tabs->addTab(buildReportsTab(),   tr("Reports"));

    // Requirement 19. Loading it lazily — only when the tab is first opened —
    // avoids three extra requests for a manager who only wanted the charts.
    m_adminTab = new AdminTab(m_api, this);
    m_tabs->addTab(m_adminTab, tr("Administration"));

    // Requirement 21. Also lazily loaded — the audit table is the largest in
    // the database and nobody wants it fetched every time they check a chart.
    m_auditTab = new AuditTab(m_api, this);
    m_tabs->addTab(m_auditTab, tr("Audit log"));

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_tabs->widget(index) == m_adminTab)
            m_adminTab->refresh();
        else if (m_tabs->widget(index) == m_auditTab)
            m_auditTab->refresh();
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // A modal dialog hides the main window's status bar, so any error raised
    // while this is open would be invisible. It needs its own place to speak.
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #c62828;");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addWidget(m_statusLabel);
    layout->addWidget(buttons);

    connect(m_api, &ApiClient::dashboardReceived,
            this, &ManagementDialog::onDashboardReceived);

    connect(m_api, &ApiClient::requestFailed,
            this, &ManagementDialog::onRequestFailed);

    // Numbers arrive asynchronously; the cards show zeros until they land.
    m_api->fetchDashboard();
}

QLabel *ManagementDialog::makeStatCard(const QString &caption, const QString &color)
{
    // Rich text in a QLabel is a cheap way to get a two-line card without
    // building a custom widget. The number is filled in later with setText.
    auto *label = new QLabel(this);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumHeight(72);
    label->setStyleSheet(QString(
        "QLabel { background: %1; border-radius: 6px; padding: 8px; color: white; }")
        .arg(color));
    label->setText(QString("<div style='font-size:20pt; font-weight:bold;'>0</div>"
                           "<div style='font-size:9pt;'>%1</div>").arg(caption));
    label->setProperty("caption", caption);
    return label;
}

QWidget *ManagementDialog::buildDashboardTab()
{
    auto *page   = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    // --- Summary cards ----------------------------------------------------
    m_totalCard      = makeStatCard(tr("Total tickets"), "#455a64");
    m_openCard       = makeStatCard(tr("Open"),          "#1976d2");
    m_overdueCard    = makeStatCard(tr("Overdue"),       "#c62828");
    m_unassignedCard = makeStatCard(tr("Unassigned"),    "#ef6c00");

    auto *cards = new QHBoxLayout;
    cards->addWidget(m_totalCard);
    cards->addWidget(m_openCard);
    cards->addWidget(m_overdueCard);
    cards->addWidget(m_unassignedCard);
    layout->addLayout(cards);

    // --- Charts -----------------------------------------------------------
    m_statusChart = new BarChartWidget(page);
    m_statusChart->setTitle(tr("Tickets by status"));

    m_priorityChart = new BarChartWidget(page);
    m_priorityChart->setTitle(tr("Tickets by priority"));

    auto *charts = new QHBoxLayout;
    charts->addWidget(m_statusChart);
    charts->addWidget(m_priorityChart);
    layout->addLayout(charts);

    // --- Workload ---------------------------------------------------------
    auto *workloadBox = new QGroupBox(tr("Open tickets per person"), page);
    auto *workloadLayout = new QVBoxLayout(workloadBox);

    m_workloadTable = new QTableWidget(0, 2, workloadBox);
    m_workloadTable->setHorizontalHeaderLabels({ tr("Person"), tr("Open tickets") });
    m_workloadTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_workloadTable->verticalHeader()->setVisible(false);
    m_workloadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    workloadLayout->addWidget(m_workloadTable);

    layout->addWidget(workloadBox, 1);

    return page;
}

QWidget *ManagementDialog::buildReportsTab()
{
    auto *page   = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *explanation = new QLabel(
        tr("Export the current ticket list to CSV. The file contains exactly the "
           "rows visible in the main window, including any filters you applied."),
        page);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto *exportButton = new QPushButton(tr("Export ticket report (CSV)"), page);
    connect(exportButton, &QPushButton::clicked,
            this, &ManagementDialog::onExportReport);
    layout->addWidget(exportButton);

    layout->addStretch();
    return page;
}

void ManagementDialog::onDashboardReceived(const DashboardStats &stats)
{
    // Small helper so each card update is one line instead of four.
    auto setCard = [](QLabel *label, int value) {
        const QString caption = label->property("caption").toString();
        label->setText(QString("<div style='font-size:20pt; font-weight:bold;'>%1</div>"
                               "<div style='font-size:9pt;'>%2</div>")
                           .arg(value).arg(caption));
    };

    setCard(m_totalCard,      stats.total);
    setCard(m_openCard,       stats.open);
    setCard(m_overdueCard,    stats.overdue);
    setCard(m_unassignedCard, stats.unassigned);

    m_statusChart->setBars({
        { tr("Open"),        stats.open,       QColor("#1976d2") },
        { tr("In progress"), stats.inProgress, QColor("#0288d1") },
        { tr("Waiting"),     stats.waiting,    QColor("#9e9e9e") },
        { tr("Resolved"),    stats.resolved,   QColor("#388e3c") },
        { tr("Closed"),      stats.closed,     QColor("#616161") }
    });

    m_priorityChart->setBars({
        { tr("Low"),      stats.low,      QColor("#8bc34a") },
        { tr("Normal"),   stats.normal,   QColor("#03a9f4") },
        { tr("High"),     stats.high,     QColor("#ff9800") },
        { tr("Critical"), stats.critical, QColor("#c62828") }
    });

    m_workloadTable->setRowCount(stats.workload.size());
    for (int i = 0; i < stats.workload.size(); ++i) {
        m_workloadTable->setItem(i, 0,
            new QTableWidgetItem(stats.workload.at(i).first));
        m_workloadTable->setItem(i, 1,
            new QTableWidgetItem(QString::number(stats.workload.at(i).second)));
    }
}

void ManagementDialog::onRequestFailed(const QString &message)
{
    m_statusLabel->setText(message);
    m_statusLabel->show();
}

void ManagementDialog::onExportReport()
{
    const QString suggested =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + "/ticket_report_" + QDate::currentDate().toString("yyyyMMdd") + ".csv";

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export report"), suggested, tr("CSV files (*.csv)"));

    if (path.isEmpty())
        return;

    QString error;
    if (CsvExporter::exportToFile(m_ticketModel, path, &error))
        QMessageBox::information(this, tr("Export"), tr("Report exported."));
    else
        QMessageBox::warning(this, tr("Export failed"), error);
}
