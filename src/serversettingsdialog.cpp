#include "serversettingsdialog.h"
#include "settings.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QDialogButtonBox>
#include <QUrl>

ServerSettingsDialog::ServerSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Server settings"));
    setMinimumWidth(420);

    m_urlEdit = new QLineEdit(Settings::serverUrl(), this);
    m_urlEdit->setPlaceholderText("http://server-name:5000");

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet("color: #b00020;");
    m_errorLabel->setWordWrap(true);

    auto *hint = new QLabel(
        tr("The change takes effect the next time the application starts."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #757575;");

    auto *form = new QFormLayout;
    form->addRow(tr("API address:"), m_urlEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ServerSettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(hint);
    root->addWidget(m_errorLabel);
    root->addWidget(buttons);
}

void ServerSettingsDialog::onAccept()
{
    const QString text = m_urlEdit->text().trimmed();

    // QUrl accepts almost anything, so check the parts that actually matter:
    // a scheme we can speak and a host to speak it to. Without this, a typo
    // like "localhost:5000" (no scheme) fails later with a confusing error.
    const QUrl url(text);

    if (!url.isValid() || url.host().isEmpty()) {
        m_errorLabel->setText(tr("Enter a full address, for example "
                                 "http://localhost:5000"));
        return;
    }

    if (url.scheme() != "http" && url.scheme() != "https") {
        m_errorLabel->setText(tr("The address must start with http:// or https://"));
        return;
    }

    Settings::setServerUrl(text);
    accept();
}
