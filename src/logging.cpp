#include "logging.h"

#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QMutex>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QDebug>

namespace {

QString g_logPath;
QFile   g_logFile;

// Qt can emit messages from any thread — network replies, timers, workers.
// Two threads writing to one QTextStream at once produces interleaved garbage,
// so every write takes this lock.
QMutex g_mutex;

// Roll the file over rather than letting it grow without limit. A log nobody
// can open is a log nobody reads.
constexpr qint64 kMaxLogSize = 2 * 1024 * 1024;   // 2 MB

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "?????";
}

void rotateIfTooLarge()
{
    if (g_logFile.size() < kMaxLogSize)
        return;

    g_logFile.close();

    // Keep exactly one previous file. Enough to investigate a crash that
    // happened just before a restart, without accumulating forever.
    const QString backup = g_logPath + ".1";
    QFile::remove(backup);
    QFile::rename(g_logPath, backup);

    g_logFile.setFileName(g_logPath);

    // Checking the result matters here: if reopening fails, every later write
    // silently does nothing and the application loses its log with no
    // indication. Qt marks open() nodiscard for exactly this reason.
    if (!g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "Could not reopen log file after rotation: %s\n",
                qPrintable(g_logPath));
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context,
                    const QString &message)
{
    QMutexLocker locker(&g_mutex);

    if (!g_logFile.isOpen())
        return;

    rotateIfTooLarge();

    QTextStream out(&g_logFile);
    out.setEncoding(QStringConverter::Utf8);

    // ISO date first so the file sorts and greps naturally.
    out << QDateTime::currentDateTime().toString(Qt::ISODate)
        << " [" << levelName(type) << "] "
        << message;

    // File and line are only meaningful for warnings and errors, and only in
    // debug builds — release builds strip them, leaving nulls.
    if (type >= QtWarningMsg && context.file)
        out << "  (" << context.file << ":" << context.line << ")";

    out << "\n";
    out.flush();

    // A fatal message means the app is about to abort, so make sure the bytes
    // are on disk before that happens.
    if (type == QtFatalMsg)
        g_logFile.flush();
}

} // namespace


void Logging::install()
{
    // AppDataLocation is the correct place on every platform: %APPDATA% on
    // Windows, ~/.local/share on Linux. Writing next to the .exe fails on
    // Windows, where Program Files is not user-writable.
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    QDir().mkpath(dir);
    g_logPath = dir + "/ticketapp.log";

    g_logFile.setFileName(g_logPath);

    if (!g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        // Can't log the failure to log, so fall back to stderr and carry on.
        // An unwritable log directory is not a reason to refuse to start.
        fprintf(stderr, "Could not open log file: %s\n", qPrintable(g_logPath));
        return;
    }

    qInstallMessageHandler(messageHandler);

    qInfo() << "--- Application started ---"
            << QCoreApplication::applicationName();
}

QString Logging::logFilePath()
{
    return g_logPath;
}
