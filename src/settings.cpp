#include "settings.h"

#include <QSettings>

namespace {
constexpr auto kServerUrl  = "server/url";
constexpr auto kGeometry   = "ui/mainWindowGeometry";
constexpr auto kLastUser   = "ui/lastUsername";

// Used on first run, before anyone has configured anything.
constexpr auto kDefaultServerUrl = "http://localhost:5000";
}

QString Settings::serverUrl()
{
    QSettings settings;
    return settings.value(kServerUrl, kDefaultServerUrl).toString();
}

void Settings::setServerUrl(const QString &url)
{
    QSettings settings;

    // Strip a trailing slash: the client appends paths like "/api/tickets",
    // and "http://host//api/tickets" is a 404 on some servers.
    QString cleaned = url.trimmed();
    while (cleaned.endsWith('/'))
        cleaned.chop(1);

    settings.setValue(kServerUrl, cleaned);
}

QByteArray Settings::mainWindowGeometry()
{
    QSettings settings;
    return settings.value(kGeometry).toByteArray();
}

void Settings::setMainWindowGeometry(const QByteArray &geometry)
{
    QSettings settings;
    settings.setValue(kGeometry, geometry);
}

QString Settings::lastUsername()
{
    QSettings settings;
    return settings.value(kLastUser).toString();
}

void Settings::setLastUsername(const QString &username)
{
    QSettings settings;
    settings.setValue(kLastUser, username);
}
