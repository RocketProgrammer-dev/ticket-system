#pragma once

#include <QString>
#include <QDateTime>
#include <QObject>
#include <QLocale>

// Requirement 11.
struct Attachment
{
    int       id = 0;
    QString   name;
    qint64    sizeBytes = 0;
    QString   mimeType;
    QString   uploadedBy;
    QDateTime uploadedAt;
};

// "245123 bytes" is unreadable. Qt has QLocale::formattedDataSize for exactly
// this, and it respects the user's locale — which matters since the app is
// bilingual.
inline QString formatFileSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes);
}
