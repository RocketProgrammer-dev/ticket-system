#pragma once

#include <QDialog>

class QLineEdit;
class QLabel;

// Lets the user point the client at a different server without a rebuild.
// Necessary in practice: the machine you develop on and the machine the
// company runs the API on are not the same.

class ServerSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ServerSettingsDialog(QWidget *parent = nullptr);

private slots:
    void onAccept();

private:
    QLineEdit *m_urlEdit;
    QLabel    *m_errorLabel;
};
