#include "loginwindow.h"
#include "apiclient.h"
#include "settings.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QShowEvent>

LoginWindow::LoginWindow(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setWindowTitle(tr("Ticket System - Sign in"));
    setMinimumWidth(340);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(tr("Username"));

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText(tr("Password"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    m_loginButton = new QPushButton(tr("Sign in"), this);
    m_loginButton->setDefault(true);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #b00020;");
    m_statusLabel->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("User:"),     m_usernameEdit);
    form->addRow(tr("Password:"), m_passwordEdit);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(m_loginButton);
    root->addWidget(m_statusLabel);
    root->addStretch();

    connect(m_loginButton, &QPushButton::clicked,
            this, &LoginWindow::onLoginClicked);

    connect(m_passwordEdit, &QLineEdit::returnPressed,
            this, &LoginWindow::onLoginClicked);

    connect(m_api, &ApiClient::loginFailed,
            this, &LoginWindow::onLoginFailed);
}

void LoginWindow::onLoginClicked()
{
    const QString user = m_usernameEdit->text().trimmed();
    const QString pass = m_passwordEdit->text();

    if (user.isEmpty() || pass.isEmpty()) {
        m_statusLabel->setText(tr("Enter a username and password."));
        return;
    }

    m_statusLabel->clear();
    setBusy(true);

    // Saved on attempt rather than on success: if the password was wrong, the
    // username is probably still the one they want on the retry.
    Settings::setLastUsername(user);

    // Returns immediately. Success is handled in main.cpp, which opens the
    // main window; failure comes back to onLoginFailed below.
    m_api->login(user, pass);
}

void LoginWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    // After a successful login this window is closed, not destroyed — so it
    // still carries the disabled "Signing in..." button from last time. Reset
    // it here rather than on login success, because this covers every way the
    // window can reappear.
    setBusy(false);

    // Never leave a password sitting in a field for the next person to use
    // the machine.
    m_passwordEdit->clear();
    m_statusLabel->clear();

    // Remember who signed in last, so a daily user only types a password.
    if (m_usernameEdit->text().isEmpty())
        m_usernameEdit->setText(Settings::lastUsername());

    // Focus the first EMPTY field, not always the password. On a fresh
    // install the username is blank, and starting the cursor past it means a
    // keyboard user has to shift-tab backwards to reach it.
    if (m_usernameEdit->text().isEmpty())
        m_usernameEdit->setFocus();
    else
        m_passwordEdit->setFocus();
}

void LoginWindow::onLoginFailed(const QString &message)
{
    setBusy(false);
    m_statusLabel->setText(message);
}

void LoginWindow::setBusy(bool busy)
{
    // Disabling the button prevents a second request while the first is still
    // in flight — easy to trigger by hitting Enter twice on a slow connection.
    m_loginButton->setEnabled(!busy);
    m_loginButton->setText(busy ? tr("Signing in...") : tr("Sign in"));
}
