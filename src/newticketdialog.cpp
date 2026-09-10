#include "newticketdialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QDateEdit>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>   // needed: buttons->button() returns a QPushButton*

NewTicketDialog::NewTicketDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New ticket"));
    setMinimumWidth(460);

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setMaxLength(200);   // matches VARCHAR(200) in schema.sql
    m_titleEdit->setPlaceholderText(tr("Short summary of the problem"));

    m_descriptionEdit = new QTextEdit(this);
    m_descriptionEdit->setPlaceholderText(
        tr("What happened, what you expected, and how to reproduce it"));
    m_descriptionEdit->setMinimumHeight(120);

    m_priorityCombo = new QComboBox(this);
    m_priorityCombo->addItem(priorityToString(TicketPriority::Low),
                             static_cast<int>(TicketPriority::Low));
    m_priorityCombo->addItem(priorityToString(TicketPriority::Normal),
                             static_cast<int>(TicketPriority::Normal));
    m_priorityCombo->addItem(priorityToString(TicketPriority::High),
                             static_cast<int>(TicketPriority::High));
    m_priorityCombo->addItem(priorityToString(TicketPriority::Critical),
                             static_cast<int>(TicketPriority::Critical));
    m_priorityCombo->setCurrentIndex(1);   // Normal

    // Deadlines are optional, so the date field needs an "off" state.
    // A checkbox is clearer than a magic empty date.
    m_dueDateCheck = new QCheckBox(tr("Set a due date"), this);

    m_dueDateEdit = new QDateEdit(QDate::currentDate().addDays(7), this);
    m_dueDateEdit->setCalendarPopup(true);
    m_dueDateEdit->setDisplayFormat("dd.MM.yyyy");
    m_dueDateEdit->setMinimumDate(QDate::currentDate());
    m_dueDateEdit->setEnabled(false);

    connect(m_dueDateCheck, &QCheckBox::toggled,
            m_dueDateEdit, &QDateEdit::setEnabled);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet("color: #b00020;");
    m_errorLabel->setWordWrap(true);

    auto *dueRow = new QHBoxLayout;
    dueRow->addWidget(m_dueDateCheck);
    dueRow->addWidget(m_dueDateEdit);
    dueRow->addStretch();

    auto *form = new QFormLayout;
    form->addRow(tr("Subject:"),     m_titleEdit);
    form->addRow(tr("Description:"), m_descriptionEdit);
    form->addRow(tr("Priority:"),    m_priorityCombo);
    form->addRow(tr("Due date:"),    dueRow);

    // StandardButtons gives correct native button order and Esc-to-cancel.
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create ticket"));

    // Note this connects to our own onAccept, not straight to accept().
    // Validation has to run first, and a failed validation must NOT close
    // the dialog — which is why accept() is called conditionally inside.
    connect(buttons, &QDialogButtonBox::accepted, this, &NewTicketDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(m_errorLabel);
    root->addWidget(buttons);

    m_titleEdit->setFocus();
}

void NewTicketDialog::onAccept()
{
    if (m_titleEdit->text().trimmed().isEmpty()) {
        m_errorLabel->setText(tr("A subject is required."));
        m_titleEdit->setFocus();
        return;   // stays open
    }

    // The server validates this too. Client-side checks are for fast feedback,
    // never for correctness — a request can always arrive without them.
    accept();
}

QString NewTicketDialog::title() const
{
    return m_titleEdit->text().trimmed();
}

QString NewTicketDialog::description() const
{
    return m_descriptionEdit->toPlainText().trimmed();
}

TicketPriority NewTicketDialog::priority() const
{
    return static_cast<TicketPriority>(m_priorityCombo->currentData().toInt());
}

QDate NewTicketDialog::dueDate() const
{
    return m_dueDateCheck->isChecked() ? m_dueDateEdit->date() : QDate();
}
