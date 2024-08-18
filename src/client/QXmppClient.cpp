// SPDX-FileCopyrightText: 2009 Manjeet Dahiya <manjeetdahiya@gmail.com>
// SPDX-FileCopyrightText: 2019 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "QXmppClient.h"

#include "QXmppClientExtension.h"
#include "QXmppClient_p.h"
#include "QXmppConstants_p.h"
#include "QXmppDiscoveryIq.h"
#include "QXmppDiscoveryManager.h"
#include "QXmppE2eeExtension.h"
#include "QXmppE2eeMetadata.h"
#include "QXmppEntityTimeManager.h"
#include "QXmppFutureUtils_p.h"
#include "QXmppLogger.h"
#include "QXmppMessage.h"
#include "QXmppMessageHandler.h"
#include "QXmppPacket_p.h"
#include "QXmppPromise.h"
#include "QXmppRosterManager.h"
#include "QXmppStreamManagement_p.h"
#include "QXmppTask.h"
#include "QXmppUtils.h"
#include "QXmppVCardManager.h"
#include "QXmppVersionManager.h"

#include "StringLiterals.h"
#include "XmppSocket.h"

#include <chrono>

#include <QDomElement>
#include <QSslSocket>
#include <QTimer>

using namespace std::chrono_literals;
using namespace QXmpp::Private;
using MessageEncryptResult = QXmppE2eeExtension::MessageEncryptResult;
using IqEncryptResult = QXmppE2eeExtension::IqEncryptResult;
using IqDecryptResult = QXmppE2eeExtension::IqDecryptResult;

static bool isIqResponse(const QDomElement &el)
{
    auto type = el.attribute(u"type"_s);
    return el.tagName() == u"iq" && (type == u"result" || type == u"error");
}

/// \cond
QXmppClientPrivate::QXmppClientPrivate(QXmppClient *qq)
    : clientPresence(QXmppPresence::Available),
      logger(nullptr),
      stream(nullptr),
      encryptionExtension(nullptr),
      receivedConflict(false),
      reconnectionTries(0),
      reconnectionTimer(nullptr),
      q(qq)
{
}

void QXmppClientPrivate::addProperCapability(QXmppPresence &presence)
{
    auto *ext = q->findExtension<QXmppDiscoveryManager>();
    if (ext) {
        presence.setCapabilityHash(u"sha-1"_s);
        presence.setCapabilityNode(ext->clientCapabilitiesNode());
        presence.setCapabilityVer(ext->capabilities().verificationString());
    }
}

std::chrono::milliseconds QXmppClientPrivate::getNextReconnectTime() const
{
    if (reconnectionTries < 5) {
        return 10s;
    } else if (reconnectionTries < 10) {
        return 20s;
    } else if (reconnectionTries < 15) {
        return 40s;
    } else {
        return 60s;
    }
}

QStringList QXmppClientPrivate::discoveryFeatures()
{
    return {
        // XEP-0004: Data Forms
        ns_data.toString(),
        // XEP-0059: Result Set Management
        ns_rsm.toString(),
        // XEP-0066: Out of Band Data
        ns_oob.toString(),
        // XEP-0071: XHTML-IM
        ns_xhtml_im.toString(),
        // XEP-0085: Chat State Notifications
        ns_chat_states.toString(),
        // XEP-0115: Entity Capabilities
        ns_capabilities.toString(),
        // XEP-0249: Direct MUC Invitations
        ns_conference.toString(),
        // XEP-0308: Last Message Correction
        ns_message_correct.toString(),
        // XEP-0333: Chat Markers
        ns_chat_markers.toString(),
        // XEP-0334: Message Processing Hints
        ns_message_processing_hints.toString(),
        // XEP-0359: Unique and Stable Stanza IDs
        ns_sid.toString(),
        // XEP-0367: Message Attaching
        ns_message_attaching.toString(),
        // XEP-0380: Explicit Message Encryption
        ns_eme.toString(),
        // XEP-0382: Spoiler messages
        ns_spoiler.toString(),
        // XEP-0428: Fallback Indication
        ns_fallback_indication.toString(),
        // XEP-0444: Message Reactions
        ns_reactions.toString(),
    };
}

void QXmppClientPrivate::onErrorOccurred(const QString &text, const QXmppOutgoingClient::ConnectionError &err, QXmppClient::Error oldError)
{
    if (q->configuration().autoReconnectionEnabled()) {
        if (oldError == QXmppClient::XmppStreamError) {
            // if we receive a resource conflict, inhibit reconnection
            if (stream->xmppStreamError() == QXmppStanza::Error::Conflict) {
                receivedConflict = true;
            }
        } else if (oldError == QXmppClient::SocketError && !receivedConflict) {
            // schedule reconnect
#if QT_VERSION < QT_VERSION_CHECK(5,8,0)
			reconnectionTimer->start(getNextReconnectTime().count());
#else
			reconnectionTimer->start(getNextReconnectTime());
#endif
		} else if (oldError == QXmppClient::KeepAliveError) {
            // if we got a keepalive error, reconnect in one second
#if QT_VERSION < QT_VERSION_CHECK(5,8,0)
			reconnectionTimer->start(1000);
#else
            reconnectionTimer->start(1s);
#endif
        }
    }

    // notify managers
    Q_EMIT q->error(oldError);
    Q_EMIT q->errorOccurred(QXmppError {
        text,
        visit([](const auto &value) { return std::any(value); }, err),
    });
}
/// \endcond

#if defined(SFOS)
namespace QXmpp {  namespace Private  {  namespace StanzaPipeline {
#else
namespace QXmpp::Private::StanzaPipeline {
#endif

bool process(const QList<QXmppClientExtension *> &extensions, const QDomElement &element, const std::optional<QXmppE2eeMetadata> &e2eeMetadata)
{
    const bool unencrypted = !e2eeMetadata.has_value();
    for (auto *extension : extensions) {
        // e2e encrypted stanzas are not passed to the old handleStanza() overload, because such
        // managers are likely not handling the encrypted contents correctly (e.g. sending
        // unencrypted replies and thereby leaking information).
        if (extension->handleStanza(element, e2eeMetadata) ||
            (unencrypted && extension->handleStanza(element))) {
            return true;
        }
    }
    return false;
}

#if defined(SFOS)
}  }  } // namespace QXmpp  Private  StanzaPipeline
#else
}  // namespace QXmpp::Private::StanzaPipeline
#endif

#if defined(SFOS)
namespace QXmpp  {  namespace Private { namespace MessagePipeline {
#else
namespace QXmpp::Private::MessagePipeline {
#endif

bool process(QXmppClient *client, const QList<QXmppClientExtension *> &extensions, QXmppMessage &&message)
{
    for (auto *extension : extensions) {
        if (auto *messageHandler = dynamic_cast<QXmppMessageHandler *>(extension)) {
            if (messageHandler->handleMessage(message)) {
                return true;
            }
        }
    }
    return false;
}

bool process(QXmppClient *client, const QList<QXmppClientExtension *> &extensions, QXmppE2eeExtension *e2eeExt, const QDomElement &element)
{
    if (element.tagName() != u"message") {
        return false;
    }
    QXmppMessage message;
    if (e2eeExt) {
        message.parse(element, e2eeExt->isEncrypted(element) ? ScePublic : SceSensitive);
    } else {
        message.parse(element);
    }
    return process(client, extensions, std::move(message));
}

#if defined(SFOS)
}  }  } // namespace QXmpp  Private  MessagePipeline
#else
}  // namespace QXmpp::Private::MessagePipeline
#endif

///
/// \class QXmppClient
///
/// \brief Main class for starting and managing connections to XMPP servers.
///
/// It provides the user all the required functionality to connect to the
/// server and perform operations afterwards.
///
/// This class will provide the handle/reference to QXmppRosterManager
/// (roster management), QXmppVCardManager (vCard manager), and
/// QXmppVersionManager (software version information).
///
/// By default, the client will automatically try reconnecting to the server.
/// You can change that behaviour using
/// QXmppConfiguration::setAutoReconnectionEnabled().
///
/// Not all the managers or extensions have been enabled by default. One can
/// enable/disable the managers using the functions \c addExtension() and
/// \c removeExtension(). \c findExtension() can be used to find a
/// reference/pointer to a particular instantiated and enabled manager.
///
/// List of managers enabled by default:
/// - QXmppRosterManager
/// - QXmppVCardManager
/// - QXmppVersionManager
/// - QXmppDiscoveryManager
/// - QXmppEntityTimeManager
///
/// ## Connection details
///
/// If no explicit host and port are configured, the client will look up the SRV records of the
/// domain of the configured JID. Since QXmpp 1.8 both TCP and direct TLS records are looked up
/// and connection via direct TLS is preferred as it saves the extra round trip from STARTTLS. See
/// also \xep{0368, SRV records for XMPP over TLS}.
///
/// On connection errors the other SRV records are tested too (if multiple are available).
///
/// For servers without SRV records or if looking up the records did not succeed, domain and the
/// default port of 5223 (TLS) and 5222 (TCP/STARTTLS) are tried.
///
/// ## Usage of FAST token-based authentication
///
/// QXmpp uses \xep{0484, Fast Authentication Streamlining Tokens} if enabled and supported by the
/// server. FAST tokens can be requested after a first time authentication using a password or
/// another strong authentication mechanism. The tokens can then be used to log in, without a
/// password. The tokens are linked to a specific device ID (set via the SASL 2 user agent) and
/// only this device can use the token. Tokens also expire and are rotated by the server.
///
/// The advantage of this mechanism is that a client does not necessarily need to store the
/// password of an account and in the future clients that are logged in could be listed and logged
/// out manually. FAST also allows for performance improvements as it only requires one round trip
/// for authentication (and may be included in TLS 0-RTT data although that is not implemented in
/// QXmpp) while other mechanisms like SCRAM need multiple round trips.
///
/// FAST itself is enabled by default (see QXmppConfiguration::useFastTokenAuthentication()), but
/// you also need to set a SASL user agent with a stable device ID, so FAST can be used.
/// After that you can login and use QXmppCredentials to serialize the token data and store it
/// permanently. Note that the token may change over time, though.
///
/// \ingroup Core
///

///
/// \typedef QXmppClient::IqResult
///
/// Result of an IQ request, either contains the QDomElement of the IQ reponse (in case of an
/// 'result' IQ type) or it contains an QXmppError with a QXmppStanza::Error (on type 'error') or
/// a QXmpp::SendError.
///
/// \since QXmpp 1.5
///

///
/// \typedef QXmppClient::EmptyResult
///
/// Result of a generic request without a return value. Contains Success in case
/// everything went well. If the returned IQ contained an error a
/// QXmppStanza::Error is reported.
///
/// \since QXmpp 1.5
///

///
/// Creates a QXmppClient object.
///
/// \param initialExtensions can be used to set the initial set of extensions.
/// \param parent is passed to the QObject's constructor. The default value is 0.
///
/// \since QXmpp 1.6
///
QXmppClient::QXmppClient(InitialExtensions initialExtensions, QObject *parent)
    : QXmppLoggable(parent),
      d(new QXmppClientPrivate(this))
{
    d->stream = new QXmppOutgoingClient(this);
    d->addProperCapability(d->clientPresence);

    connect(d->stream, &QXmppOutgoingClient::elementReceived,
            this, &QXmppClient::_q_elementReceived);

    connect(d->stream, &QXmppOutgoingClient::messageReceived,
            this, &QXmppClient::messageReceived);

    connect(d->stream, &QXmppOutgoingClient::presenceReceived,
            this, &QXmppClient::presenceReceived);

    connect(d->stream, &QXmppOutgoingClient::iqReceived,
            this, &QXmppClient::iqReceived);

    connect(d->stream, &QXmppOutgoingClient::sslErrors,
            this, &QXmppClient::sslErrors);

    connect(d->stream->socket(), &QAbstractSocket::stateChanged,
            this, &QXmppClient::_q_socketStateChanged);

    connect(d->stream, &QXmppOutgoingClient::connected,
            this, &QXmppClient::_q_streamConnected);

    connect(d->stream, &QXmppOutgoingClient::disconnected,
            this, &QXmppClient::_q_streamDisconnected);

    connect(d->stream, &QXmppOutgoingClient::errorOccurred, this, [this](const auto &text, const auto &error, auto oldError) {
        d->onErrorOccurred(text, error, oldError);
    });

    // reconnection
    d->reconnectionTimer = new QTimer(this);
    d->reconnectionTimer->setSingleShot(true);
    connect(d->reconnectionTimer, &QTimer::timeout,
            this, &QXmppClient::_q_reconnect);

    // logging
    setLogger(QXmppLogger::getLogger());

    switch (initialExtensions) {
    case NoExtensions:
        break;
    case BasicExtensions:
        addNewExtension<QXmppRosterManager>(this);
        addNewExtension<QXmppVCardManager>();
        addNewExtension<QXmppVersionManager>();
        addNewExtension<QXmppEntityTimeManager>();
        addNewExtension<QXmppDiscoveryManager>();
        break;
    }
}

///
/// Creates a QXmppClient object.
/// \param parent is passed to the QObject's constructor.
///
QXmppClient::QXmppClient(QObject *parent)
    : QXmppClient(BasicExtensions, parent)
{
}

QXmppClient::~QXmppClient() = default;

///
/// \fn QXmppClient::addNewExtension()
///
/// Creates a new extension and adds it to the client.
///
/// \returns the newly created extension
///
/// \since QXmpp 1.5
///

/// Registers a new \a extension with the client.
bool QXmppClient::addExtension(QXmppClientExtension *extension)
{
    return insertExtension(d->extensions.size(), extension);
}

/// Registers a new \a extension with the client at the given \a index.
bool QXmppClient::insertExtension(int index, QXmppClientExtension *extension)
{
    if (d->extensions.contains(extension)) {
        qWarning("Cannot add extension, it has already been added");
        return false;
    }

    extension->setParent(this);
    d->extensions.insert(index, extension);
    extension->setClient(this);
    return true;
}

///
/// Unregisters the given extension from the client. If the extension
/// is found, it will be destroyed.
///
bool QXmppClient::removeExtension(QXmppClientExtension *extension)
{
    if (d->extensions.contains(extension)) {
        d->extensions.removeAll(extension);
        extension->setClient(nullptr);
        delete extension;
        return true;
    } else {
        qWarning("Cannot remove extension, it was never added");
        return false;
    }
}

///
/// Returns the currently used encryption extension.
///
/// \since QXmpp 1.5
///
QXmppE2eeExtension *QXmppClient::encryptionExtension() const
{
    return d->encryptionExtension;
}

///
/// Sets the extension to be used for end-to-end-encryption.
///
/// \since QXmpp 1.5
///
void QXmppClient::setEncryptionExtension(QXmppE2eeExtension *extension)
{
    d->encryptionExtension = extension;
}

/// Returns a list containing all the client's extensions.
QList<QXmppClientExtension *> QXmppClient::extensions() const
{
    return d->extensions;
}

/// Returns a modifiable reference to the current configuration of QXmppClient.
QXmppConfiguration &QXmppClient::configuration()
{
    return d->stream->configuration();
}

///
/// Attempts to connect to the XMPP server. Server details and other configurations
/// are specified using the config parameter. Use signals connected(), error(QXmppClient::Error)
/// and disconnected() to know the status of the connection.
///
/// \param config Specifies the configuration object for connecting the XMPP server.
/// This contains the host name, user, password etc. See QXmppConfiguration for details.
/// \param initialPresence The initial presence which will be set for this user
/// after establishing the session. The default value is QXmppPresence::Available
///
void QXmppClient::connectToServer(const QXmppConfiguration &config,
                                  const QXmppPresence &initialPresence)
{
    // reset package cache from last connection
    if (d->stream->configuration().jidBare() != config.jidBare()) {
        d->stream->streamAckManager().resetCache();
    }

    d->stream->configuration() = config;
    d->clientPresence = initialPresence;
    d->addProperCapability(d->clientPresence);

    d->stream->connectToHost();
}

///
/// Overloaded function to simply connect to an XMPP server with a JID and password.
///
/// \param jid JID for the account.
/// \param password Password for the account.
///
void QXmppClient::connectToServer(const QString &jid, const QString &password)
{
    QXmppConfiguration config;
    config.setJid(jid);
    config.setPassword(password);
    connectToServer(config);
}

///
/// After successfully connecting to the server use this function to send
/// stanzas to the server. This function can solely be used to send various kind
/// of stanzas to the server. QXmppStanza is a parent class of all the stanzas
/// QXmppMessage, QXmppPresence, QXmppIq, QXmppBind, QXmppRosterIq, QXmppSession
/// and QXmppVCard.
///
/// This function does not end-to-end encrypt the packets.
///
/// \return Returns true if the packet was sent, false otherwise.
///
/// Following code snippet illustrates how to send a message using this function:
/// \code
/// QXmppMessage message(from, to, message);
/// client.sendPacket(message);
/// \endcode
///
/// \param packet A valid XMPP stanza. It can be an iq, a message or a presence stanza.
///
bool QXmppClient::sendPacket(const QXmppNonza &packet)
{
    return d->stream->streamAckManager().sendPacketCompat(packet);
}

///
/// Sends a packet and reports the result via QXmppTask.
///
/// If stream management is enabled, the task continues to be active until the
/// server acknowledges the packet. On success, QXmpp::SendSuccess with
/// acknowledged == true is reported and the task finishes.
///
/// If connection errors occur, the packet is resent if possible. If
/// reconnecting is not possible, an error is reported.
///
/// \warning THIS API IS NOT FINALIZED YET!
///
/// \returns A QXmppTask that makes it possible to track the state of the packet.
///
/// \since QXmpp 1.5
///
QXmppTask<QXmpp::SendResult> QXmppClient::sendSensitive(QXmppStanza &&stanza, const std::optional<QXmppSendStanzaParams> &params)
{
    const auto sendEncrypted = [this](auto &&task) {
        QXmppPromise<QXmpp::SendResult> interface;
        task.then(this, [this, interface](auto &&result) mutable {
            std::visit(overloaded {
                           [&](std::unique_ptr<QXmppMessage> &&message) {
                               QByteArray xml;
                               QXmlStreamWriter writer(&xml);
                               message->toXml(&writer, QXmpp::ScePublic);

                               d->stream->streamAckManager().send(QXmppPacket(xml, true, std::move(interface)));
                           },
                           [&](std::unique_ptr<QXmppIq> &&iq) {
                               d->stream->streamAckManager().send(QXmppPacket(*iq, std::move(interface)));
                           },
                           [&](QXmppError &&error) {
                               interface.finish(std::move(error));
                           } },
                       std::move(result));
        });

        return interface.task();
    };

    if (d->encryptionExtension) {
        if (dynamic_cast<QXmppMessage *>(&stanza)) {
            return sendEncrypted(
                d->encryptionExtension->encryptMessage(
                    std::move(dynamic_cast<QXmppMessage &&>(stanza)), params));
        } else if (dynamic_cast<QXmppIq *>(&stanza)) {
            return sendEncrypted(
                d->encryptionExtension->encryptIq(
                    std::move(dynamic_cast<QXmppIq &&>(stanza)), params));
        }
    }
    return d->stream->streamAckManager().send(stanza);
}

///
/// Sends a packet always without end-to-end-encryption.
///
/// This does the same as send(), but does not do any end-to-end encryption on
/// the stanza.
///
/// \warning THIS API IS NOT FINALIZED YET!
///
/// \returns A QXmppTask that makes it possible to track the state of the packet.
///
/// \since QXmpp 1.5
///
QXmppTask<QXmpp::SendResult> QXmppClient::send(QXmppStanza &&stanza, const std::optional<QXmppSendStanzaParams> &)
{
    return d->stream->streamAckManager().send(stanza);
}

///
/// Sends the stanza with the same encryption as \p e2eeMetadata.
///
/// When there is no e2eeMetadata given this always sends the stanza without
/// end-to-end encryption.
/// Intended to be used for replies to IQs and messages.
///
/// \since QXmpp 1.5
///
QXmppTask<QXmpp::SendResult> QXmppClient::reply(QXmppStanza &&stanza, const std::optional<QXmppE2eeMetadata> &e2eeMetadata, const std::optional<QXmppSendStanzaParams> &params)
{
    // This should pick the right e2ee manager as soon as multiple encryptions
    // in parallel are supported.
    if (e2eeMetadata) {
        return sendSensitive(std::move(stanza), params);
    }
    return send(std::move(stanza), params);
}

///
/// Sends an IQ packet and returns the response asynchronously.
///
/// This is useful for further processing and parsing of the returned
/// QDomElement. If you don't expect a special response, you may want use
/// sendGenericIq().
///
/// IQs of type 'error' are parsed automatically and returned as QXmppError with a contained
/// QXmppStanza::Error.
///
/// This does not do any end-to-encryption on the IQ.
///
/// \sa sendSensitiveIq()
///
/// \warning THIS API IS NOT FINALIZED YET!
///
/// \since QXmpp 1.5
///
QXmppTask<QXmppClient::IqResult> QXmppClient::sendIq(QXmppIq &&iq, const std::optional<QXmppSendStanzaParams> &)
{
    return d->stream->sendIq(std::move(iq));
}

///
/// Tries to encrypt and send an IQ packet and returns the response
/// asynchronously.
///
/// This can be used for sensitive IQ requests performed from client to client.
/// Most IQ requests like service discovery requests cannot be end-to-end
/// encrypted or it only makes little sense to do so. This is why the default
/// sendIq() does not do any additional end-to-end encryption.
///
/// IQs of type 'error' are parsed automatically and returned as QXmppError with a contained
/// QXmppStanza::Error.
///
/// \warning THIS API IS NOT FINALIZED YET!
///
/// \since QXmpp 1.5
///
QXmppTask<QXmppClient::IqResult> QXmppClient::sendSensitiveIq(QXmppIq &&iq, const std::optional<QXmppSendStanzaParams> &params)
{
    if (d->encryptionExtension) {
        QXmppPromise<IqResult> p;
        auto task = p.task();
        d->encryptionExtension->encryptIq(std::move(iq), params).then(this, [this, p = std::move(p)](IqEncryptResult result) mutable {
            std::visit(overloaded {
                           [&](std::unique_ptr<QXmppIq> &&iq) {
                               // success (encrypted)
                               d->stream->sendIq(std::move(*iq)).then(this, [this, p = std::move(p)](auto &&result) mutable {
                                   // iq sent, response received
                                   std::visit(overloaded {
                                                  [&](QDomElement &&el) {
                                                      if (!isIqResponse(el)) {
                                                          p.finish(QXmppError {
                                                              u"Invalid IQ response received."_s,
                                                              QXmpp::SendError::EncryptionError });
                                                          return;
                                                      }
                                                      if (!d->encryptionExtension) {
                                                          p.finish(QXmppError {
                                                              u"No decryption extension found."_s,
                                                              QXmpp::SendError::EncryptionError });
                                                          return;
                                                      }
                                                      // try to decrypt the result (should be encrypted)
                                                      d->encryptionExtension->decryptIq(el).then(this, [p = std::move(p), encryptedEl = el](IqDecryptResult result) mutable {
                                                          std::visit(overloaded {
                                                                         [&](QDomElement &&decryptedEl) {
                                                                             p.finish(decryptedEl);
                                                                         },
                                                                         [&](QXmppE2eeExtension::NotEncrypted) {
                                                                             // the IQ response from the other entity was not encrypted
                                                                             // then report IQ response without modifications
                                                                             // TODO: should we return a QXmppError instead?
                                                                             p.finish(std::move(encryptedEl));
                                                                         },
                                                                         [&](QXmppError &&error) {
                                                                             p.finish(error);
                                                                         } },
                                                                     std::move(result));
                                                      });
                                                  },
                                                  [&](QXmppError &&e) {
                                                      p.finish(std::move(e));
                                                  } },
                                              std::move(result));
                               });
                           },
                           [&](QXmppError &&error) {
                               // error (encryption)
                               p.finish(std::move(error));
                           } },
                       std::move(result));
        });

        return task;
    }
    return d->stream->sendIq(std::move(iq));
}

///
/// Sends an IQ and returns possible stanza errors.
///
/// If you want to parse a special IQ response in the result case, you can use
/// sendIq() and parse the returned QDomElement.
///
/// \returns Returns QXmpp::Success (on response type 'result') or the contained
/// QXmppStanza::Error (on response type 'error')
///
/// \warning THIS API IS NOT FINALIZED YET!
///
/// \since QXmpp 1.5
///
QXmppTask<QXmppClient::EmptyResult> QXmppClient::sendGenericIq(QXmppIq &&iq, const std::optional<QXmppSendStanzaParams> &)
{
    return chainIq(sendIq(std::move(iq)), this, [](const QXmppIq &) -> EmptyResult {
        return QXmpp::Success();
    });
}

///
/// Disconnects the client and the current presence of client changes to
/// QXmppPresence::Unavailable and status text changes to "Logged out".
///
/// \note Make sure that the clientPresence is changed to
/// QXmppPresence::Available, if you are again calling connectToServer() after
/// calling the disconnectFromServer() function.
///
void QXmppClient::disconnectFromServer()
{
    // cancel reconnection
    d->reconnectionTimer->stop();

    d->clientPresence.setType(QXmppPresence::Unavailable);
    d->clientPresence.setStatusText(u"Logged out"_s);
    if (d->stream->isConnected()) {
        sendPacket(d->clientPresence);
    }

    d->stream->disconnectFromHost();
}

/// Returns true if the client has authenticated with the XMPP server.
bool QXmppClient::isAuthenticated() const
{
    return d->stream->isAuthenticated();
}

/// Returns true if the client is connected to the XMPP server.
bool QXmppClient::isConnected() const
{
    return d->stream->isConnected();
}

///
/// Returns true if the current client state is "active", false if it is
/// "inactive". See \xep{0352, Client State Indication} for details.
///
/// \since QXmpp 1.0
///
bool QXmppClient::isActive() const
{
    return d->stream->csiManager().state() == CsiManager::Active;
}

///
/// Sets the client state as described in \xep{0352, Client State Indication}.
///
/// Since QXmpp 1.8, the state is restored across reconnects. QXmpp will re-send the state of
/// 'inactive' on connection if that was set before. Stream resumptions are also handled.
///
/// \since QXmpp 1.0
///
void QXmppClient::setActive(bool active)
{
    d->stream->csiManager().setState(active ? CsiManager::Active : CsiManager::Inactive);
}

///
/// Returns the current \xep{0198}: Stream Management state of the connection.
///
/// Upon connection of the client this can be used to check whether the
/// previous stream has been resumed.
///
/// \since QXmpp 1.4
///
QXmppClient::StreamManagementState QXmppClient::streamManagementState() const
{
    if (d->stream->c2sStreamManager().enabled()) {
        if (d->stream->c2sStreamManager().streamResumed()) {
            return ResumedStream;
        }
        return NewStream;
    }
    return NoStreamManagement;
}

///
/// Utility function to send message to all the resources associated with the
/// specified bareJid. If there are no resources available, that is the contact
/// is offline or not present in the roster, it will still send a message to
/// the bareJid.
///
/// \note Usage of this method is discouraged because most modern clients use
/// carbon messages (\xep{0280}: Message Carbons) and MAM (\xep{0313}: Message
/// Archive Management) and so could possibly receive messages multiple times
/// or not receive them at all.
/// \c QXmppClient::sendPacket() should be used instead with a \c QXmppMessage.
///
/// \param bareJid bareJid of the receiving entity
/// \param message Message string to be sent.
///
void QXmppClient::sendMessage(const QString &bareJid, const QString &message)
{
    QXmppRosterManager *rosterManager = findExtension<QXmppRosterManager>();

    const QStringList resources = rosterManager
        ? rosterManager->getResources(bareJid)
        : QStringList();

    if (!resources.isEmpty()) {
        for (const auto &resource : resources) {
            sendPacket(
                QXmppMessage({}, bareJid + u"/"_s + resource, message));
        }
    } else {
        sendPacket(QXmppMessage({}, bareJid, message));
    }
}

QXmppOutgoingClient *QXmppClient::stream() const
{
    return d->stream;
}

QXmppClient::State QXmppClient::state() const
{
    if (d->stream->isConnected()) {
        return QXmppClient::ConnectedState;
    } else if (d->stream->socket()->state() != QAbstractSocket::UnconnectedState &&
               d->stream->socket()->state() != QAbstractSocket::ClosingState) {
        return QXmppClient::ConnectingState;
    } else {
        return QXmppClient::DisconnectedState;
    }
}

/// Returns the client's current presence.
QXmppPresence QXmppClient::clientPresence() const
{
    return d->clientPresence;
}

///
/// Changes the presence of the connected client.
///
/// The connection to the server will be updated accordingly:
///
/// \li If the presence type is QXmppPresence::Unavailable, the connection
/// to the server will be closed.
///
/// \li Otherwise, the connection to the server will be established
/// as needed.
///
/// \param presence QXmppPresence object
///
void QXmppClient::setClientPresence(const QXmppPresence &presence)
{
    d->clientPresence = presence;
    d->addProperCapability(d->clientPresence);

    if (presence.type() == QXmppPresence::Unavailable) {
        // cancel reconnection
        d->reconnectionTimer->stop();

        // NOTE: we can't call disconnect() because it alters
        // the client presence
        if (d->stream->isConnected()) {
            sendPacket(d->clientPresence);
        }

        d->stream->disconnectFromHost();
    } else if (d->stream->isConnected()) {
        sendPacket(d->clientPresence);
    } else {
        connectToServer(d->stream->configuration(), presence);
    }
}

/// Returns the socket error if error() is QXmppClient::SocketError.
QAbstractSocket::SocketError QXmppClient::socketError()
{
    return d->stream->socket()->error();
}

/// Returns the human-readable description of the last socket error if error() is QXmppClient::SocketError.
QString QXmppClient::socketErrorString() const
{
    return d->stream->socket()->errorString();
}

/// Returns the XMPP stream error if QXmppClient::Error is QXmppClient::XmppStreamError.
QXmppStanza::Error::Condition QXmppClient::xmppStreamError()
{
    return d->stream->xmppStreamError();
}

void QXmppClient::injectIq(const QDomElement &element, const std::optional<QXmppE2eeMetadata> &e2eeMetadata)
{
    if (element.tagName() != u"iq") {
        return;
    }
    if (!StanzaPipeline::process(d->extensions, element, e2eeMetadata)) {
        const auto iqType = element.attribute(u"type"_s);
        if (iqType == u"get" || iqType == u"set") {
            // send error IQ
            using Err = QXmppStanza::Error;

            QXmppIq iq(QXmppIq::Error);
            iq.setTo(element.attribute(u"from"_s));
            iq.setId(element.attribute(u"id"_s));
            const auto errMessage = e2eeMetadata.has_value()
                ? u"Feature not implemented or not supported with end-to-end encryption."_s
                : u"Feature not implemented."_s;
            iq.setError(Err(Err::Cancel, Err::FeatureNotImplemented, errMessage));
            reply(std::move(iq), e2eeMetadata);
        }
        // don't do anything for "result" and "error" IQs
    }
}

///
/// Processes the message with message handlers and emits messageReceived as a fallback.
///
bool QXmppClient::injectMessage(QXmppMessage &&message)
{
    auto handled = MessagePipeline::process(this, d->extensions, std::move(message));
    if (!handled) {
        // no extension handled the message
        Q_EMIT messageReceived(message);
    }
    return handled;
}

///
/// Give extensions a chance to handle incoming stanzas.
///
void QXmppClient::_q_elementReceived(const QDomElement &element, bool &handled)
{
    // The stanza comes directly from the XMPP stream, so it's not end-to-end
    // encrypted and there's no e2ee metadata (std::nullopt).
    handled = StanzaPipeline::process(d->extensions, element, std::nullopt) ||
    MessagePipeline::process(this, d->extensions, d->encryptionExtension, element);
}

void QXmppClient::_q_reconnect()
{
    if (d->stream->configuration().autoReconnectionEnabled()) {
        debug(u"Reconnecting to server"_s);
        d->stream->connectToHost();
    }
}

void QXmppClient::_q_socketStateChanged(QAbstractSocket::SocketState socketState)
{
    Q_UNUSED(socketState);
    Q_EMIT stateChanged(state());
}

/// At connection establishment, send initial presence.
void QXmppClient::_q_streamConnected(const QXmpp::Private::SessionBegin &session)
{
    d->receivedConflict = false;
    d->reconnectionTries = 0;

    // notify managers
    if (session.fastTokenChanged) {
        Q_EMIT credentialsChanged();
    }
    Q_EMIT connected();
    Q_EMIT stateChanged(QXmppClient::ConnectedState);

    // send initial presence
    if (d->stream->isAuthenticated() && streamManagementState() != ResumedStream) {
        sendPacket(d->clientPresence);
    }
}

void QXmppClient::_q_streamDisconnected()
{
    // notify managers
    Q_EMIT disconnected();
    Q_EMIT stateChanged(QXmppClient::DisconnectedState);
}

QXmppLogger *QXmppClient::logger() const
{
    return d->logger;
}

/// Sets the QXmppLogger associated with the current QXmppClient.
void QXmppClient::setLogger(QXmppLogger *logger)
{
    if (logger != d->logger) {
        if (d->logger) {
            disconnect(this, &QXmppLoggable::logMessage,
                       d->logger, &QXmppLogger::log);
            disconnect(this, &QXmppLoggable::setGauge,
                       d->logger, &QXmppLogger::setGauge);
            disconnect(this, &QXmppLoggable::updateCounter,
                       d->logger, &QXmppLogger::updateCounter);
        }

        d->logger = logger;
        if (d->logger) {
            connect(this, &QXmppLoggable::logMessage,
                    d->logger, &QXmppLogger::log);
            connect(this, &QXmppLoggable::setGauge,
                    d->logger, &QXmppLogger::setGauge);
            connect(this, &QXmppLoggable::updateCounter,
                    d->logger, &QXmppLogger::updateCounter);
        }

        Q_EMIT loggerChanged(d->logger);
    }
}
