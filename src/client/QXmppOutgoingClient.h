// SPDX-FileCopyrightText: 2010 Manjeet Dahiya <manjeetdahiya@gmail.com>
// SPDX-FileCopyrightText: 2010 Jeremy Lainé <jeremy.laine@m4x.org>
// SPDX-FileCopyrightText: 2020 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef QXMPPOUTGOINGCLIENT_H
#define QXMPPOUTGOINGCLIENT_H

#include "QXmppAuthenticationError.h"
#include "QXmppBindError.h"
#include "QXmppClient.h"
#include "QXmppStanza.h"
#include "QXmppStreamError.h"

#include <QAbstractSocket>

class QDomElement;
class QSslError;
class QSslSocket;

class QXmppConfiguration;
class QXmppPresence;
class QXmppIq;
class QXmppMessage;
class QXmppStreamFeatures;
class QXmppOutgoingClientPrivate;
class TestClient;

#if defined(SFOS)
namespace QXmpp {
	namespace Private {
#else
namespace QXmpp::Private {
#endif
class C2sStreamManager;
class OutgoingIqManager;
class PingManager;
class StreamAckManager;
class XmppSocket;

enum HandleElementResult {
    Accepted,
    Rejected,
    Finished,
};
}  // namespace QXmpp::Private

#if defined(SFOS)
namespace QXmpp { namespace Private { namespace Sasl2 {
#else
namespace QXmpp::Private::Sasl2 {
#endif
struct StreamFeature;
#if defined(SFOS)
} }
#else
}
#endif

// The QXmppOutgoingClient class represents an outgoing XMPP stream to an XMPP server.
class QXMPP_EXPORT QXmppOutgoingClient : public QXmppLoggable
{
    Q_OBJECT

public:
    using IqResult = std::variant<QDomElement, QXmppError>;
    using ConnectionError = std::variant<QAbstractSocket::SocketError, QXmpp::TimeoutError, QXmpp::StreamError, QXmpp::AuthenticationError, QXmpp::BindError>;

    explicit QXmppOutgoingClient(QObject *parent);
    ~QXmppOutgoingClient() override;

    void connectToHost();
    void disconnectFromHost();
    bool isAuthenticated() const;
    bool isConnected() const;
    bool isClientStateIndicationEnabled() const;
    QXmppTask<IqResult> sendIq(QXmppIq &&);

    /// Returns the used socket
    QSslSocket *socket() const;
    QXmppStanza::Error::Condition xmppStreamError();

    QXmppConfiguration &configuration();

    QXmpp::Private::XmppSocket &xmppSocket() const;
    QXmpp::Private::StreamAckManager &streamAckManager() const;
    QXmpp::Private::OutgoingIqManager &iqManager() const;
    QXmpp::Private::C2sStreamManager &c2sStreamManager();

    /// This signal is emitted when the stream is connected.
    Q_SIGNAL void connected();

    /// This signal is emitted when the stream is disconnected.
    Q_SIGNAL void disconnected();

    /// This signal is emitted when an error is encountered.
    Q_SIGNAL void errorOccurred(const QString &text, const QXmppOutgoingClient::ConnectionError &details, QXmppClient::Error oldError);

    /// This signal is emitted when an element is received.
    Q_SIGNAL void elementReceived(const QDomElement &element, bool &handled);

    /// This signal is emitted when a presence is received.
    Q_SIGNAL void presenceReceived(const QXmppPresence &);

    /// This signal is emitted when a message is received.
    Q_SIGNAL void messageReceived(const QXmppMessage &);

    /// This signal is emitted when an IQ response (type result or error) has
    /// been received that was not handled by elementReceived().
    Q_SIGNAL void iqReceived(const QXmppIq &);

    /// This signal is emitted when SSL errors are encountered.
    Q_SIGNAL void sslErrors(const QList<QSslError> &errors);

private:
    void handleStart();
    void handlePacketReceived(const QDomElement &element);
    QXmpp::Private::HandleElementResult handleElement(const QDomElement &nodeRecv);
    void handleStream(const QDomElement &element);

    void _q_socketDisconnected();
    void socketError(QAbstractSocket::SocketError);
    void socketSslErrors(const QList<QSslError> &);

    void startSasl2Auth(const QXmpp::Private::Sasl2::StreamFeature &sasl2Feature);
    void startNonSaslAuth();
    void startResourceBinding();
    void onSMResumeFinished();
    void onSMEnableFinished();
    void throwKeepAliveError();

    // for unit tests, see TestClient
    void enableStreamManagement(bool resetSequenceNumber);
    bool handleIqResponse(const QDomElement &);

    friend class QXmppOutgoingClientPrivate;
    friend class QXmpp::Private::PingManager;
    friend class QXmpp::Private::C2sStreamManager;
    friend class TestClient;

    const std::unique_ptr<QXmppOutgoingClientPrivate> d;
};

#if defined(SFOS)
namespace QXmpp {
	namespace Private {
#else
namespace QXmpp::Private {
#endif

class C2sStreamManager
{
public:
    explicit C2sStreamManager(QXmppOutgoingClient *q);

    bool handleElement(const QDomElement &);
    bool hasResumeAddress() const { return m_canResume && !m_resumeHost.isEmpty() && m_resumePort; }
    std::pair<QString, quint16> resumeAddress() const { return { m_resumeHost, m_resumePort }; }
    void onStreamStart();
    void onStreamFeatures(const QXmppStreamFeatures &);
    void onDisconnecting();
    bool canResume() const { return m_canResume; }
    bool enabled() const { return m_enabled; }
    bool streamResumed() const { return m_streamResumed; }
    bool canRequestResume() const { return m_smAvailable && m_canResume; }
    void requestResume();
    bool canRequestEnable() const { return m_smAvailable; }
    void requestEnable();

private:
    friend class ::TestClient;

    bool setResumeAddress(const QString &address);
    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setResumed(bool resumed) { m_streamResumed = resumed; }

    QXmppOutgoingClient *q;

    bool m_smAvailable = false;
    QString m_smId;
    bool m_canResume = false;
    bool m_isResuming = false;
    QString m_resumeHost;
    quint16 m_resumePort = 0;
    bool m_enabled = false;
    bool m_streamResumed = false;
};

#if defined(SFOS)
} }  // namespace QXmpp Private
#else
}  // namespace QXmpp::Private
#endif

#endif  // QXMPPOUTGOINGCLIENT_H
