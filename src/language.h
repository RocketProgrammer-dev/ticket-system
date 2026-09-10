#pragma once

#include <QString>
#include <QList>

class QApplication;
class QTranslator;

// Loading a translation is three steps that are easy to get subtly wrong, so
// they live here rather than being scattered through main().
//
// A caveat worth knowing up front: Qt applies translations when a widget is
// CONSTRUCTED. Installing a translator later leaves already-built windows in
// the old language. Handling that properly means implementing retranslateUi()
// on every window and calling it on a LanguageChange event — a lot of work.
// The pragmatic alternative, used here, is to ask the user to restart. Most
// desktop applications do exactly this.

namespace Language {

struct Entry {
    QString code;         // "en", "tr"
    QString displayName;  // "English", "Türkçe"
};

// The languages this build ships with.
QList<Entry> available();

// Reads the saved choice from QSettings. Falls back to the system language if
// nothing was saved, and to English if the system language isn't available —
// so a Turkish Windows install gets Turkish on first run without configuring
// anything.
QString currentCode();

// Persists the choice. Takes effect on the next start.
void setCurrentCode(const QString &code);

// Installs the app translation AND Qt's own, which is what translates the
// standard dialog buttons ("OK", "Cancel", "Save") and the file dialog. Miss
// the second one and you get a half-translated UI, which looks worse than
// none at all.
//
// The translators are heap-allocated and parented to the application, so they
// live as long as the app does. A translator that goes out of scope is
// silently uninstalled — a classic source of "my translation stopped working".
void install(QApplication &app, const QString &code);

} // namespace Language
