#include "usereditdialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>

UserEditDialog::UserEditDialog(const AdminUser *existing,
                               const QList<DepartmentInfo> &departments,
                               QWidget *parent)
    : QDialog(parent)
    , m_creating(existing == nullptr)
{
    setWindowTitle(m_creating ? tr("New user") : tr("Edit user"));
    setMinimumWidth(420);

    m_usernameEdit = new QLineEdit(this);
    m_fullNameEdit = new QLineEdit(this);
    m_emailEdit    = new QLineEdit(this);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(
        m_creating ? tr("At least 8 characters")
                   : tr("Leave blank to keep the current password"));

    m_roleCombo = new QComboBox(this);
    m_roleCombo->addItem(tr("Staff"),      static_cast<int>(UserRole::Staff));
    m_roleCombo->addItem(tr("Technician"), static_cast<int>(UserRole::Technician));
    m_roleCombo->addItem(tr("Manager"),    static_cast<int>(UserRole::Manager));

    m_departmentCombo = new QComboBox(this);
    m_departmentCombo->addItem(tr("(none)"), 0);
    for (const DepartmentInfo &d : departments) {
        if (d.isActive)
            m_departmentCombo->addItem(d.name, d.id);
    }

    m_activeCheck = new QCheckBox(tr("Active"), this);
    m_activeCheck->setChecked(true);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet("color: #b00020;");
    m_errorLabel->setWordWrap(true);

    if (existing) {
        m_existingId = existing->id;
        m_usernameEdit->setText(existing->username);
        m_usernameEdit->setEnabled(false);   // usernames are identifiers, not labels
        m_fullNameEdit->setText(existing->fullName);
        m_emailEdit->setText(existing->email);
        m_roleCombo->setCurrentIndex(static_cast<int>(existing->role));
        m_activeCheck->setChecked(existing->isActive);

        const int deptIndex = m_departmentCombo->findData(existing->departmentId);
        if (deptIndex >= 0)
            m_departmentCombo->setCurrentIndex(deptIndex);
    }

    auto *form = new QFormLayout;
    form->addRow(tr("Username:"),   m_usernameEdit);
    form->addRow(tr("Full name:"),  m_fullNameEdit);
    form->addRow(tr("Email:"),      m_emailEdit);
    form->addRow(tr("Password:"),   m_passwordEdit);
    form->addRow(tr("Role:"),       m_roleCombo);
    form->addRow(tr("Department:"), m_departmentCombo);
    form->addRow(QString(),         m_activeCheck);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &UserEditDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(m_errorLabel);
    root->addWidget(buttons);
}

void UserEditDialog::onAccept()
{
    if (m_usernameEdit->text().trimmed().isEmpty()) {
        m_errorLabel->setText(tr("Username is required."));
        return;
    }
    if (m_fullNameEdit->text().trimmed().isEmpty()) {
        m_errorLabel->setText(tr("Full name is required."));
        return;
    }
    // Only enforced when creating: on edit, blank means "unchanged".
    if (m_creating && m_passwordEdit->text().length() < 8) {
        m_errorLabel->setText(tr("Password must be at least 8 characters."));
        return;
    }
    if (!m_passwordEdit->text().isEmpty() && m_passwordEdit->text().length() < 8) {
        m_errorLabel->setText(tr("Password must be at least 8 characters."));
        return;
    }

    accept();
}

AdminUser UserEditDialog::user() const
{
    AdminUser u;
    u.id           = m_existingId;
    u.username     = m_usernameEdit->text().trimmed();
    u.fullName     = m_fullNameEdit->text().trimmed();
    u.email        = m_emailEdit->text().trimmed();
    u.role         = static_cast<UserRole>(m_roleCombo->currentData().toInt());
    u.departmentId = m_departmentCombo->currentData().toInt();
    u.isActive     = m_activeCheck->isChecked();
    return u;
}

QString UserEditDialog::password() const
{
    return m_passwordEdit->text();
}
