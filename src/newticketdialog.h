#pragma once

#include <QDialog>
#include <QDate>
#include <QString>

#include "ticket.h"

class QLineEdit;
class QTextEdit;
class QComboBox;
class QDateEdit;
class QCheckBox;
class QLabel;

// A form dialog. Unlike TicketDetailDialog this one is modal and returns a
// result: the caller shows it, and if the user accepts, reads the values out
// through the getters below.
//
// The dialog does no networking. It collects input and validates it; sending
// is MainWindow's job, because MainWindow is the one that owns the ApiClient.

class NewTicketDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewTicketDialog(QWidget *parent = nullptr);

    QString        title() const;
    QString        description() const;
    TicketPriority priority() const;

    // Returns an invalid QDate when the user left the deadline unchecked,
    // which maps to NULL in the database.
    QDate dueDate() const;

private slots:
    void onAccept();

private:
    QLineEdit *m_titleEdit;
    QTextEdit *m_descriptionEdit;
    QComboBox *m_priorityCombo;
    QCheckBox *m_dueDateCheck;
    QDateEdit *m_dueDateEdit;
    QLabel    *m_errorLabel;
};
