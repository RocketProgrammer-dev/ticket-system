#pragma once

class QString;

// Requirement 24: errors go to a log file.
//
// The trick here is that you do NOT change any existing code. Every qDebug(),
// qWarning() and qCritical() already scattered through the project starts
// writing to the file the moment this is installed, because Qt routes them all
// through one handler that you can replace.

namespace Logging {

// Call once from main(), before anything else that might log.
void install();

// Where the log ended up — shown in an About dialog or printed on startup so
// a user can actually find it when asked to send it in.
QString logFilePath();

} // namespace Logging
