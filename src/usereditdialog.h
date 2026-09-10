#pragma once

#include <QDialog>
#include <QList>

#include "admintypes.h"

class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;

// Used for both creating and editing. `existing` being null means "create":
// the same form serves both, which halves the code and guarantees the two
// paths validate identically.

class UserEditDialog : public QDialog
{
    Q_OBJECT

public:
    UserEditDialog(const AdminUser *existing,
                   const QList<DepartmentInfo> &departments,
                   QWidget *parent = nullptr);

    AdminUser user() const;

    // Empty when the manager left the password field blank, meaning "keep the
    // current password". Only sent when it has a value.
    QString password() const;

    bool isCreating() const { return m_creating; }

private slots:
    void onAccept();

private:
    bool m_creating;
    int  m_existingId = 0;

    QLineEdit *m_usernameEdit;
    QLineEdit *m_fullNameEdit;
    QLineEdit *m_emailEdit;
    QLineEdit *m_passwordEdit;
    QComboBox *m_roleCombo;
    QComboBox *m_departmentCombo;
    QCheckBox *m_activeCheck;
    QLabel    *m_errorLabel;
};
