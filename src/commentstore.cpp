#include "commentstore.h"
#include "apiclient.h"

CommentStore::CommentStore(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
    connect(m_api, &ApiClient::commentsReceived,
            this, &CommentStore::onCommentsReceived);

    connect(m_api, &ApiClient::commentPosted,
            this, &CommentStore::onCommentPosted);
}

QList<Comment> CommentStore::commentsFor(int ticketId) const
{
    return m_byTicket.value(ticketId);
}

void CommentStore::ensureLoaded(int ticketId)
{
    if (m_requested.contains(ticketId))
        return;

    m_requested.insert(ticketId);
    m_api->fetchComments(ticketId);
}

void CommentStore::addComment(int ticketId, const QString &text, bool isInternal)
{
    m_api->postComment(ticketId, text, isInternal);
}

void CommentStore::onCommentsReceived(int ticketId, const QList<Comment> &comments)
{
    m_byTicket[ticketId] = comments;
    emit commentsChanged(ticketId);
}

void CommentStore::onCommentPosted(int ticketId)
{
    // The POST succeeded; now re-read the list so we display exactly what the
    // server has, including the id and timestamp it assigned.
    m_requested.remove(ticketId);
    ensureLoaded(ticketId);
}
