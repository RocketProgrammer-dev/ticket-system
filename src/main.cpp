#include <QApplication>
#include <QIcon>

#include "logging.h"
#include "settings.h"
#include "language.h"
#include "apiclient.h"
#include "loginwindow.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // QSettings needs these to know where to store things. On Windows that's
    // the registry; on Linux, ~/.config/<org>/<app>.conf. Set them before any
    // QSettings object is created, or the first one writes to the wrong place.
    // The .rc file gives the EXECUTABLE its icon in Explorer and the taskbar.
    // This gives the WINDOWS their icon inside the app, which is a separate
    // thing — and it is what makes the icon appear on Linux too, where .rc
    // files mean nothing.
    QApplication::setWindowIcon(QIcon(":/icons/appicon.png"));

    QApplication::setOrganizationName("TicketSystem");
    QApplication::setApplicationName("TicketApp");

    // Must happen BEFORE any window is constructed: Qt resolves tr() calls at
    // construction time, so a translator installed afterwards has no effect on
    // widgets that already exist.
    // Install the log handler early: everything after this point, including
    // startup failures, ends up in the file.
    Logging::install();

    Language::install(app, Language::currentCode());

    // No longer hardcoded: configurable via Settings > Server, and stored in
    // the registry on Windows or ~/.config on Linux.
    ApiClient api(Settings::serverUrl());

    LoginWindow login(&api);
    MainWindow *mainWindow = nullptr;

    // main() owns the transition between screens. The login window doesn't
    // know a ticket list exists, and the ticket list doesn't know about login.
    QObject::connect(&api, &ApiClient::loginSucceeded,
                     [&](const QString &username, UserRole role) {
                         mainWindow = new MainWindow(username, role, &api);
                         mainWindow->setAttribute(Qt::WA_DeleteOnClose);

                         QObject::connect(mainWindow, &MainWindow::logoutRequested,
                                          [&]() {
                             api.logout();   // discard the token

                             // Show the login window BEFORE closing the main
                             // one. Qt quits when the last window closes, so
                             // the reverse order would exit the application.
                             login.show();
                             login.raise();
                             login.activateWindow();

                             mainWindow->close();   // WA_DeleteOnClose frees it
                             mainWindow = nullptr;
                         });

                         mainWindow->show();
                         login.close();
                     });

    login.show();
    return app.exec();
}
