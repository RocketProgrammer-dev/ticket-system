cmake_minimum_required(VERSION 3.19)
project(TicketApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# AUTOMOC runs Qt's meta-object compiler for you.
# Any class with the Q_OBJECT macro needs this. Forget it and you get
# confusing "undefined reference to vtable" linker errors.
set(CMAKE_AUTOMOC ON)

# Compiles .qrc files, which embed images into the executable so there are no
# loose image files to lose during deployment.
set(CMAKE_AUTORCC ON)

# Widgets = the classic desktop UI module. Network = HTTP for your REST calls.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network LinguistTools)

# The .rc file is only meaningful on Windows; adding it on Linux would fail
# to compile, so it is appended conditionally below rather than listed here.
add_executable(TicketApp
    resources/resources.qrc
    src/main.cpp
    src/loginwindow.cpp
    src/loginwindow.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/ticket.h
    src/ticketmodel.cpp
    src/ticketmodel.h
    src/ticketquery.h
    src/apiclient.cpp
    src/apiclient.h
    src/comment.h
    src/ticketdetaildialog.cpp
    src/ticketdetaildialog.h
    src/csvexporter.cpp
    src/csvexporter.h
    src/userrole.h
    src/commentstore.cpp
    src/commentstore.h
    src/newticketdialog.cpp
    src/newticketdialog.h
    src/language.cpp
    src/language.h
    src/user.h
    src/barchartwidget.cpp
    src/barchartwidget.h
    src/managementdialog.cpp
    src/managementdialog.h
    src/admintypes.h
    src/attachment.h
    src/audittypes.h
    src/audittab.cpp
    src/audittab.h
    src/admintab.cpp
    src/admintab.h
    src/usereditdialog.cpp
    src/usereditdialog.h
    src/timeentry.h
    src/notification.h
    src/notificationsdialog.cpp
    src/notificationsdialog.h
    src/logging.cpp
    src/logging.h
    src/settings.cpp
    src/settings.h
)

# Embeds the application icon and version information into the executable.
# CMake compiles .rc files automatically with the platform resource compiler,
# so no extra tooling is needed.
if(WIN32)
    target_sources(TicketApp PRIVATE resources/appicon.rc)
endif()

target_link_libraries(TicketApp PRIVATE Qt6::Widgets Qt6::Network)

# On Windows, WIN32_EXECUTABLE ON means "GUI application": no console window
# opens alongside it. OFF gives you a console, which is useful in development
# for seeing qDebug() output but looks broken to a user.
#
# The generator expression gives both: a console in Debug builds, none in
# Release. Release output goes to the log file instead (see logging.cpp), so
# nothing is lost.
set_target_properties(TicketApp PROPERTIES
    WIN32_EXECUTABLE $<CONFIG:Release>
)


# ---------------------------------------------------------------------
#  Translations (i18n)
# ---------------------------------------------------------------------
# Every user-facing string in this project is wrapped in tr(). That is what
# makes translation possible at all — lupdate scans the source for tr() calls
# and collects them into .ts files. A string built with string concatenation
# or hardcoded outside tr() is invisible to it.
#
# qt_add_translations does three things:
#   1. adds an "update_translations" build target that runs lupdate
#   2. compiles each .ts into a binary .qm at build time (lrelease)
#   3. embeds the .qm files into the executable as Qt resources
#
# Embedding matters for requirement 27: the installer ships one .exe rather
# than an .exe plus a folder of loose language files that can go missing.
# Qt 6.7 changed this signature: the target now goes after a TARGETS keyword.
# The old positional form fails with a confusing "non-existent target (" error
# on newer Qt, because the argument parser reads the target name as part of
# something else.
qt_add_translations(TARGETS TicketApp
    TS_FILES
        translations/ticketapp_en.ts
        translations/ticketapp_tr.ts
    RESOURCE_PREFIX "/i18n"
)
