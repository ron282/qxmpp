// SPDX-FileCopyrightText: 2016 Niels Ole Salscheider <niels_ole@salscheider-online.de>
// SPDX-FileCopyrightText: 2022 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "QXmppMamManager.h"

#include "QXmppClient.h"
#include "QXmppConstants_p.h"
#include "QXmppDataForm.h"
#include "QXmppE2eeExtension.h"
#include "QXmppMamIq.h"
#include "QXmppMessage.h"
#include "QXmppPromise.h"
#include "QXmppUtils.h"

#include <unordered_map>

#include <QDomElement>

using namespace QXmpp;
using namespace QXmpp::Private;

template<typename T, typename Converter>
auto transform(const T &input, Converter convert)
{
    using Output = std::decay_t<decltype(convert(*input.begin()))>;
    QVector<Output> output;
    output.reserve(input.size());
    std::transform(input.begin(), input.end(), std::back_inserter(output), std::move(convert));
    return output;
}

template<typename T>
auto sum(const T &c)
{
    return std::accumulate(c.begin(), c.end(), 0);
}

struct MamMessage
{
    QDomElement element;
    std::optional<QDateTime> delay;
};

enum EncryptedType { Unencrypted,
                     Encrypted };

QXmppMessage parseMamMessage(const MamMessage &mamMessage, EncryptedType encrypted)
{
    QXmppMessage m;
    m.parse(mamMessage.element, encrypted == Encrypted ? ScePublic : SceAll);
    if (mamMessage.delay) {
        m.setStamp(*mamMessage.delay);
    }
    return m;
}

std::optional<std::tuple<MamMessage, QString>> parseMamMessageResult(const QDomElement &messageEl)
{
    auto resultElement = messageEl.firstChildElement("result");
    if (resultElement.isNull() || resultElement.namespaceURI() != ns_mam) {
        return {};
    }

    auto forwardedElement = resultElement.firstChildElement("forwarded");
    if (forwardedElement.isNull() || forwardedElement.namespaceURI() != ns_forwarding) {
        return {};
    }

    auto queryId = resultElement.attribute("queryid");

    auto messageElement = forwardedElement.firstChildElement("message");
    if (messageElement.isNull()) {
        return {};
    }

    auto parseDelay = [](const auto &forwardedEl) -> std::optional<QDateTime> {
        auto delayEl = forwardedEl.firstChildElement("delay");
        if (!delayEl.isNull() && delayEl.namespaceURI() == ns_delayed_delivery) {
            return QXmppUtils::datetimeFromString(delayEl.attribute("stamp"));
        }
        return {};
    };

    return { { MamMessage { messageElement, parseDelay(forwardedElement) }, queryId } };
}

struct RetrieveRequestState
{
    QXmppPromise<QXmppMamManager::RetrieveResult> promise;
    QXmppMamResultIq iq;
    QVector<MamMessage> messages;
    QVector<QXmppMessage> processedMessages;
    uint runningDecryptionJobs = 0;

    void finish()
    {
        Q_ASSERT(messages.count() == processedMessages.count());
        promise.finish(
            QXmppMamManager::RetrievedMessages {
                std::move(iq),
                std::move(processedMessages) });
    }
};

class QXmppMamManagerPrivate
{
public:
    // std::string because older Qt 5 versions don't add std::hash support for QString
    std::unordered_map<std::string, RetrieveRequestState> ongoingRequests;
};

///
/// \struct QXmppMamManager::RetrievedMessages
///
/// \brief Contains all retrieved messages and the result IQ that can be used for pagination.
///
/// \since QXmpp 1.5
///

///
/// \var QXmppMamManager::RetrievedMessages::result
///
/// The returned result IQ from the MAM server.
///

///
/// \var QXmppMamManager::RetrievedMessages::messages
///
/// A vector of retrieved QXmppMessages.
///

///
/// \typedef QXmppMamManager::RetrieveResult
///
/// Contains RetrievedMessages or an QXmppError.
///
/// \since QXmpp 1.5
///

QXmppMamManager::QXmppMamManager()
    : d(std::make_unique<QXmppMamManagerPrivate>())
{
}

QXmppMamManager::~QXmppMamManager() = default;

/// \cond
QStringList QXmppMamManager::discoveryFeatures() const
{
    // XEP-0313: Message Archive Management
    return QStringList() << ns_mam;
}

bool QXmppMamManager::handleStanza(const QDomElement &element)
{
    if (element.tagName() == "message") {
        if (auto result = parseMamMessageResult(element)) {
            auto &[message, queryId] = *result;

            auto itr = d->ongoingRequests.find(queryId.toStdString());
            if (itr != d->ongoingRequests.end()) {
                // future-based API
                itr->second.messages.append(std::move(message));
            } else {
                // signal-based API
                Q_EMIT archivedMessageReceived(queryId, parseMamMessage(message, Unencrypted));
            }
            return true;
        }
    } else if (QXmppMamResultIq::isMamResultIq(element)) {
        QXmppMamResultIq result;
        result.parse(element);
        Q_EMIT resultsRecieved(result.id(), result.resultSetReply(), result.complete());
        return true;
    }

    return false;
}
/// \endcond

static QXmppMamQueryIq buildRequest(const QString &to,
                                    const QString &node,
                                    const QString &jid,
                                    const QDateTime &start,
                                    const QDateTime &end,
                                    const QXmppResultSetQuery &resultSetQuery)
{
    QList<QXmppDataForm::Field> fields;

    QXmppDataForm::Field hiddenField(QXmppDataForm::Field::HiddenField);
    hiddenField.setKey("FORM_TYPE");
    hiddenField.setValue(ns_mam);
    fields << hiddenField;

    if (!jid.isEmpty()) {
        QXmppDataForm::Field jidField;
        jidField.setKey("with");
        jidField.setValue(jid);
        fields << jidField;
    }

    if (start.isValid()) {
        QXmppDataForm::Field startField;
        startField.setKey("start");
        startField.setValue(QXmppUtils::datetimeToString(start));
        fields << startField;
    }

    if (end.isValid()) {
        QXmppDataForm::Field endField;
        endField.setKey("end");
        endField.setValue(QXmppUtils::datetimeToString(end));
        fields << endField;
    }

    QXmppDataForm form;
    form.setType(QXmppDataForm::Submit);
    form.setFields(fields);

    QXmppMamQueryIq queryIq;
    QString queryId = queryIq.id(); /* reuse the IQ id as query id */
    queryIq.setTo(to);
    queryIq.setNode(node);
    queryIq.setQueryId(queryId);
    queryIq.setForm(form);
    queryIq.setResultSetQuery(resultSetQuery);
    return queryIq;
}

///
/// Retrieves archived messages. For each received message, the
/// archiveMessageReceived() signal is emitted. Once all messages are received,
/// the resultsRecieved() signal is emitted. It returns a result set that can
/// be used to page through the results.
/// The number of results may be limited by the server.
///
/// \warning This API does not work with end-to-end encrypted messages. You can
/// use the new QFuture-based API (retrieveMessages()) for that.
///
/// \param to Optional entity that should be queried. Leave this empty to query
///           the local archive.
/// \param node Optional node that should be queried. This is used when querying
///             a pubsub node.
/// \param jid Optional JID to filter the results.
/// \param start Optional start time to filter the results.
/// \param end Optional end time to filter the results.
/// \param resultSetQuery Optional Result Set Management query. This can be used
///                       to limit the number of results and to page through the
///                       archive.
/// \return query id of the request. This can be used to associate the
///         corresponding resultsRecieved signal.
///
QString QXmppMamManager::retrieveArchivedMessages(const QString &to,
                                                  const QString &node,
                                                  const QString &jid,
                                                  const QDateTime &start,
                                                  const QDateTime &end,
                                                  const QXmppResultSetQuery &resultSetQuery)
{
    auto queryIq = buildRequest(to, node, jid, start, end, resultSetQuery);
    client()->sendPacket(queryIq);
    return queryIq.id();
}

///
/// Retrieves archived messages and reports all messages at once via a QFuture.
///
/// This function tries to decrypt encrypted messages.
///
/// The number of results may be limited by the server.
///
/// \param to Optional entity that should be queried. Leave this empty to query
///           the local archive.
/// \param node Optional node that should be queried. This is used when querying
///             a pubsub node.
/// \param jid Optional JID to filter the results.
/// \param start Optional start time to filter the results.
/// \param end Optional end time to filter the results.
/// \param resultSetQuery Optional Result Set Management query. This can be used
///                       to limit the number of results and to page through the
///                       archive.
/// \return query id of the request. This can be used to associate the
///         corresponding resultsRecieved signal.
///
/// \since QXmpp 1.5
///
QXmppTask<QXmppMamManager::RetrieveResult> QXmppMamManager::retrieveMessages(const QString &to, const QString &node, const QString &jid, const QDateTime &start, const QDateTime &end, const QXmppResultSetQuery &resultSetQuery)
{
    auto queryIq = buildRequest(to, node, jid, start, end, resultSetQuery);
    auto queryId = queryIq.queryId();

    auto [itr, inserted] = d->ongoingRequests.insert({ queryIq.queryId().toStdString(), RetrieveRequestState() });
    Q_ASSERT(inserted);

    // create task here; promise could finish immediately after client()->sendIq()
    auto task = itr->second.promise.task();

    // retrieve messages
    client()->sendIq(std::move(queryIq)).then(this, [this, queryId](QXmppClient::IqResult result) {
        auto itr = d->ongoingRequests.find(queryId.toStdString());
        if (itr == d->ongoingRequests.end()) {
            return;
        }
        auto &state = itr->second;

        // handle IQ sending errors
        if (std::holds_alternative<QXmppError>(result)) {
            state.promise.finish(std::get<QXmppError>(result));
            d->ongoingRequests.erase(itr);
            return;
        }

        // parse IQ
        auto &iq = state.iq;
        iq.parse(std::get<QDomElement>(result));

        // handle MAM error result IQ
        if (iq.type() == QXmppIq::Error) {
            state.promise.finish(QXmppError { iq.error().text(), iq.error() });
            d->ongoingRequests.erase(itr);
            return;
        }

        // decrypt encrypted messages
        if (auto *e2eeExt = client()->encryptionExtension()) {
            // initialize processed messages (we need random access because
            // decryptMessage() may finish in random order)
            state.processedMessages.resize(state.messages.size());

            // check for encrypted messages (once)
            auto messagesEncrypted = transform(state.messages, [&](const auto &m) {
                return e2eeExt->isEncrypted(m.element);
            });
            auto encryptedCount = sum(messagesEncrypted);

            // We can't do this on the fly (with ++ and --) in the for loop
            // because some decryptMessage() jobs could finish instantly
            state.runningDecryptionJobs = encryptedCount;

            int size = state.messages.size();
            for (auto i = 0; i < size; i++) {
                if (!messagesEncrypted[i]) {
                    continue;
                }

                e2eeExt->decryptMessage(parseMamMessage(state.messages.at(i), Encrypted)).then(this, [this, i, queryId](auto result) {
                    auto itr = d->ongoingRequests.find(queryId.toStdString());
                    Q_ASSERT(itr != d->ongoingRequests.end());

                    auto &state = itr->second;

                    // store decrypted message, fallback to encrypted message
                    if (std::holds_alternative<QXmppMessage>(result)) {
                        state.processedMessages[i] = std::get<QXmppMessage>(std::move(result));
                    } else {
                        warning(QStringLiteral("Error decrypting message."));
                        state.processedMessages[i] = parseMamMessage(state.messages[i], Unencrypted);
                    }

                    // finish promise if this was the last job
                    state.runningDecryptionJobs--;
                    if (state.runningDecryptionJobs == 0) {
                        state.finish();
                        d->ongoingRequests.erase(itr);
                    }
                });
            }

            // finishing the promise is done after decryptMessage()
            if (encryptedCount > 0) {
                return;
            }
        }

        // for the case without decryption, finish here
        state.processedMessages = transform(state.messages, [](const auto &m) {
            return parseMamMessage(m, Unencrypted);
        });
        state.finish();
        d->ongoingRequests.erase(itr);
    });

    return task;
}
