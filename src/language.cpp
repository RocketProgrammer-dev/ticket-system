#include "language.h"

#include <QApplication>
#include <QTranslator>
#include <QSettings>
#include <QLocale>
#include <QLibraryInfo>
#include <QDebug>

namespace {
constexpr auto kSettingsKey = "ui/language";
}

QList<Language::Entry> Language::available()
{
    return {
        { "en", QStringLiteral("English") },
        // Written as a literal, not tr(): a language menu should always show
        // each language in its own tongue, never translated into the current
        // one. "Turkish" in a Turkish menu would be wrong.
        { "tr", QStringLiteral("Türkçe") }
    };
}

QString Language::currentCode()
{
    QSettings settings;
    const QString saved = settings.value(kSettingsKey).toString();

    if (!saved.isEmpty())
        return saved;

    // Nothing saved yet — try the system language. QLocale::system().name()
    // gives something like "tr_TR"; we only want the part before the
    // underscore.
    const QString systemLang = QLocale::system().name().section('_', 0, 0);

    for (const Entry &e : available()) {
        if (e.code == systemLang)
            return systemLang;
    }

    return QStringLiteral("en");
}

void Language::setCurrentCode(const QString &code)
{
    QSettings settings;
    settings.setValue(kSettingsKey, code);
}

void Language::install(QApplication &app, const QString &code)
{
    // --- Qt's own strings -------------------------------------------------
    // Translates standard buttons and dialogs. Without this you get "Tamam"
    // next to "Cancel", which looks broken.
    auto *qtTranslator = new QTranslator(&app);
    const QString qtPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);

    if (qtTranslator->load("qtbase_" + code, qtPath))
        app.installTranslator(qtTranslator);
    // Not an error if this fails: Qt ships no "qtbase_en" because English is
    // its source language.

    // --- Our strings ------------------------------------------------------
    auto *appTranslator = new QTranslator(&app);

    // ":/i18n/..." reads from resources compiled into the executable by
    // qt_add_translations, so there is no file to lose during installation.
    if (appTranslator->load(":/i18n/ticketapp_" + code + ".qm")) {
        app.installTranslator(appTranslator);
    } else {
        // Falling back to a file next to the executable is handy while
        // developing, before the resource has been built.
        if (appTranslator->load("ticketapp_" + code, app.applicationDirPath()))
            app.installTranslator(appTranslator);
        else
            qWarning() << "No translation found for" << code << "- using English.";
    }
}
