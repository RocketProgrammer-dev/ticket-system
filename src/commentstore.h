#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QSet>

#include "comment.h"

class ApiClient;

// Still the single owner of comments, but now backed by the server instead of
// a hardcoded list. The dialog's code did not change at all when this was
// swapped over — that's what the store was for.

class CommentStore : public QObject
{
    Q_OBJECT

public:
    explicit CommentStore(ApiClient *api, QObject *parent = nullptr);

    // Returns whatever is cached right now, possibly empty. Pair it with
    // ensureLoaded() and the commentsChanged signal.
    QList<Comment> commentsFor(int ticketId) const;

    // Requests this ticket's comments from the server if we haven't already.
    void ensureLoaded(int ticketId);

    // Sends the comment. Nothing appears locally until the server confirms,
    // so what you see is always what was actually stored.
    void addComment(int ticketId, const QString &text, bool isInternal);

signals:
    void commentsChanged(int ticketId);

private slots:
    void onCommentsReceived(int ticketId, const QList<Comment> &comments);
    void onCommentPosted(int ticketId);

private:
    ApiClient *m_api;                       // not owned
    QHash<int, QList<Comment>> m_byTicket;
    QSet<int> m_requested;                  // avoids re-fetching on every open
};
