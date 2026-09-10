#pragma once

#include <QString>

// Thin wrapper over QSettings. The point isn't to hide QSettings — it's to
// keep the KEY STRINGS in one place. Scattered string literals like
// settings.value("server/url") are a classic source of silent bugs: misspell
// one and you get an empty default with no error anywhere.

namespace Settings {

QString serverUrl();
void    setServerUrl(const QString &url);

// Remembering window size is a small thing that makes an app feel finished.
QByteArray mainWindowGeometry();
void       setMainWindowGeometry(const QByteArray &geometry);

QString lastUsername();
void    setLastUsername(const QString &username);

} // namespace Settings
