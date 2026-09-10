#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;
class ApiClient;

// The role dropdown is gone. The server decides your role and signs it into
// the token; a client that picks its own permissions is not a permission
// system. That combo box was development scaffolding and its removal is the
// visible part of requirement 20 being real.

class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(ApiClient *api, QWidget *parent = nullptr);

protected:
    // Called every time the window becomes visible, including when it is
    // shown again after a logout. That is the right place to reset transient
    // UI state, because the constructor only runs once.
    void showEvent(QShowEvent *event) override;

private slots:
    void onLoginClicked();
    void onLoginFailed(const QString &message);

private:
    void setBusy(bool busy);

    ApiClient   *m_api;        // not owned
    QLineEdit   *m_usernameEdit;
    QLineEdit   *m_passwordEdit;
    QPushButton *m_loginButton;
    QLabel      *m_statusLabel;
};
