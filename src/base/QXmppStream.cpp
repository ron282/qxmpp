// SPDX-FileCopyrightText: 2009 Manjeet Dahiya <manjeetdahiya@gmail.com>
// SPDX-FileCopyrightText: 2010 Jeremy Lainé <jeremy.laine@m4x.org>
// SPDX-FileCopyrightText: 2021 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "QXmppStream.h"

#include "QXmppConstants_p.h"
#include "QXmppError.h"
#include "QXmppNonza.h"
#include "QXmppStreamError_p.h"
#include "QXmppUtils_p.h"

#include "Stream.h"
#include "XmppSocket.h"
#include "qxmlstream.h"

#include <QDomDocument>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSslSocket>

using namespace QXmpp;
using namespace QXmpp::Private;

class QXmppStreamPrivate
{
public:
    QXmppStreamPrivate(QXmppStream *stream);

    XmppSocket socket;
};

QXmppStreamPrivate::QXmppStreamPrivate(QXmppStream *stream)
    : socket(stream)
{
}

///
/// Constructs a base XMPP stream.
///
/// \param parent
///
QXmppStream::QXmppStream(QObject *parent)
    : QXmppLoggable(parent),
      d(std::make_unique<QXmppStreamPrivate>(this))
{
    connect(&d->socket, &XmppSocket::started, this, &QXmppStream::handleStart);
    connect(&d->socket, &XmppSocket::stanzaReceived, this, &QXmppStream::handleStanza);
    connect(&d->socket, &XmppSocket::streamReceived, this, &QXmppStream::handleStream);
    connect(&d->socket, &XmppSocket::streamClosed, this, &QXmppStream::disconnectFromHost);
}

QXmppStream::~QXmppStream() = default;

///
/// Disconnects from the remote host.
///
void QXmppStream::disconnectFromHost()
{
    d->socket.disconnectFromHost();
}

///
/// Handles a stream start event, which occurs when the underlying transport
/// becomes ready (socket connected, encryption started).
///
void QXmppStream::handleStart()
{
}

///
/// Returns true if the stream is connected.
///
bool QXmppStream::isConnected() const
{
    return d->socket.isConnected();
}

///
/// Sends raw data to the peer.
///
/// \param data
///
bool QXmppStream::sendData(const QByteArray &data)
{
    return d->socket.sendData(data);
}

///
/// Sends an XMPP packet to the peer.
///
/// \param nonza
///
bool QXmppStream::sendPacket(const QXmppNonza &nonza)
{
    return d->socket.sendData(serializeXml(nonza));
}

///
/// Returns access to the XMPP socket.
///
XmppSocket &QXmppStream::xmppSocket() const
{
    return d->socket;
}

///
/// Returns the QSslSocket used for this stream.
///
QSslSocket *QXmppStream::socket() const
{
    return d->socket.socket();
}

///
/// Sets the QSslSocket used for this stream.
///
void QXmppStream::setSocket(QSslSocket *socket)
{
    d->socket.setSocket(socket);
}

#if defined(SFOS)
namespace QXmpp { namespace Private {
#else
namespace QXmpp::Private {
#endif

void StreamOpen::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartDocument();
    writer->writeStartElement(QSL65("stream:stream"));
    if (!from.isEmpty()) {
        writer->writeAttribute(QSL65("from"), from);
    }
    writer->writeAttribute(QSL65("to"), to);
    writer->writeAttribute(QSL65("version"), QSL65("1.0"));
    writer->writeDefaultNamespace(toString65(xmlns));
    writer->writeNamespace(toString65(ns_stream), QSL65("stream"));
    writer->writeCharacters({});
}

constexpr auto STREAM_ERROR_CONDITIONS = to_array<QStringView>({
    u"bad-format",
    u"bad-namespace-prefix",
    u"conflict",
    u"connection-timeout",
    u"host-gone",
    u"host-unknown",
    u"improper-addressing",
    u"internal-server-error",
    u"invalid-from",
    u"invalid-id",
    u"invalid-namespace",
    u"invalid-xml",
    u"not-authorized",
    u"not-well-formed",
    u"policy-violation",
    u"remote-connection-failed",
    u"reset",
    u"resource-constraint",
    u"restricted-xml",
    u"system-shutdown",
    u"undefined-condition",
    u"unsupported-encoding",
    u"unsupported-stanza-type",
    u"unsupported-version",
});

/// \cond
QString StreamErrorElement::streamErrorToString(StreamError e)
{
    return STREAM_ERROR_CONDITIONS.at(size_t(e)).toString();
}

std::variant<StreamErrorElement, QXmppError> StreamErrorElement::fromDom(const QDomElement &el)
{
    if (el.tagName() != u"error" || el.namespaceURI() != ns_stream) {
        return QXmppError { QStringLiteral("Invalid dom element."), {} };
    }

    std::optional<StreamErrorElement::Condition> condition;
    QString errorText;

    for (const auto &subEl : iterChildElements(el, {}, ns_stream_error)) {
        auto tagName = subEl.tagName();
        if (tagName == u"text") {
            errorText = subEl.text();
        } else if (auto conditionEnum = enumFromString<StreamError>(STREAM_ERROR_CONDITIONS, tagName)) {
            condition = conditionEnum;
        } else if (tagName == u"see-other-host") {
            if (auto [host, port] = parseHostAddress(subEl.text()); !host.isEmpty()) {
                condition = SeeOtherHost { host, quint16(port > 0 ? port : XMPP_DEFAULT_PORT) };
            }
        }
    }

    if (!condition) {
        return QXmppError { QStringLiteral("Stream error is missing valid error condition."), {} };
    }

    return StreamErrorElement {
        std::move(*condition),
        std::move(errorText),
    };
}
/// \endcond

XmppSocket::XmppSocket(QObject *parent)
    : QXmppLoggable(parent)
{
}

void XmppSocket::setSocket(QSslSocket *socket)
{
    m_socket = socket;
    if (!m_socket) {
        return;
    }

    QObject::connect(socket, &QAbstractSocket::connected, this, [this]() {
        info(QStringLiteral("Socket connected to %1 %2")
                 .arg(m_socket->peerAddress().toString(),
                      QString::number(m_socket->peerPort())));
        m_dataBuffer.clear();
        m_streamOpenElement.clear();
        Q_EMIT started();
    });
    QObject::connect(socket, &QSslSocket::encrypted, this, [this]() {
        debug(QStringLiteral("Socket encrypted"));
        m_dataBuffer.clear();
        m_streamOpenElement.clear();
        Q_EMIT started();
    });
    QObject::connect(socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        warning(QStringLiteral("Socket error: ") + m_socket->errorString());
    });
    QObject::connect(socket, &QSslSocket::readyRead, this, [this]() {
        processData(QString::fromUtf8(m_socket->readAll()));
    });
}

bool XmppSocket::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void XmppSocket::disconnectFromHost()
{
    if (m_socket) {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            sendData(QByteArrayLiteral("</stream:stream>"));
            m_socket->flush();
        }
        // FIXME: according to RFC 6120 section 4.4, we should wait for
        // the incoming stream to end before closing the socket
        m_socket->disconnectFromHost();
    }
}

bool XmppSocket::sendData(const QByteArray &data)
{
    logSent(QString::fromUtf8(data));
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    return m_socket->write(data) == data.size();
}

void XmppSocket::processData(const QString &data)
{
    // As we may only have partial XML content, we need to cache the received
    // data until it has been successfully parsed. In case it can't be parsed,
    //
    // There are only two small problems with the current strategy:
    //  * When we receive a full stanza + a partial one, we can't parse the
    //    first stanza until another stanza arrives that is complete.
    //  * We don't know when we received invalid XML (would cause a growing
    //    cache and a timeout after some time).
    // However, both issues could only be solved using an XML stream reader
    // which would cause many other problems since we don't actually use it for
    // parsing the content.
    m_dataBuffer.append(data);

    //
    // Check for whitespace pings
    //
    if (m_dataBuffer.isEmpty() || m_dataBuffer.trimmed().isEmpty()) {
        m_dataBuffer.clear();

        logReceived({});
        Q_EMIT stanzaReceived(QDomElement());
        return;
    }

    //
    // Check whether we received a stream open or closing tag
    //
    static const QRegularExpression streamStartRegex(QStringLiteral(R"(^(<\?xml.*\?>)?\s*<stream:stream[^>]*>)"));
    static const QRegularExpression streamEndRegex(QStringLiteral("</stream:stream>$"));

    auto streamOpenMatch = streamStartRegex.match(m_dataBuffer);
    bool hasStreamOpen = streamOpenMatch.hasMatch();

    bool hasStreamClose = streamEndRegex.match(m_dataBuffer).hasMatch();

    //
    // The stream start/end and stanza packets can't be parsed without any
    // modifications with QDomDocument. This is because of multiple reasons:
    //  * The <stream:stream> open element is not considered valid without the
    //    closing tag.
    //  * Only the closing tag is of course not valid too.
    //  * Stanzas/Nonzas need to have the correct stream namespaces set:
    //     * For being able to parse <stream:features/>
    //     * For having the correct namespace (e.g. 'jabber:client') set to
    //       stanzas and their child elements (e.g. <body/> of a message).
    //
    // The wrapping strategy looks like this:
    //  * The stream open tag is cached once it arrives, for later access
    //  * Incoming XML that has no <stream> open tag will be prepended by the
    //    cached <stream> tag.
    //  * Incoming XML that has no <stream> close tag will be appended by a
    //    generic string "</stream:stream>"
    //
    // The result is parsed by QDomDocument and the child elements of the stream
    // are processed. In case the received data contained a stream open tag,
    // the stream is processed (before the stanzas are processed). In case we
    // received a </stream> closing tag, the connection is closed.
    //
    auto wrappedStanzas = m_dataBuffer;
    if (!hasStreamOpen) {
        wrappedStanzas.prepend(m_streamOpenElement);
    }
    if (!hasStreamClose) {
        wrappedStanzas.append(QStringLiteral("</stream:stream>"));
    }

    //
    // Try to parse the wrapped XML
    //
    QDomDocument doc;
    if (!doc.setContent(wrappedStanzas, true)) {
        return;
    }

    //
    // Success: We can clear the buffer and send a 'received' log message
    //
    logReceived(m_dataBuffer);
    m_dataBuffer.clear();

    // process stream start
    if (hasStreamOpen) {
        m_streamOpenElement = streamOpenMatch.captured();
        Q_EMIT streamReceived(doc.documentElement());
    }

    // process stanzas
    auto stanza = doc.documentElement().firstChildElement();
    for (; !stanza.isNull(); stanza = stanza.nextSiblingElement()) {
        Q_EMIT stanzaReceived(stanza);
    }

    // process stream end
    if (hasStreamClose) {
        Q_EMIT streamClosed();
    }
}

#if defined(SFOS)
}  }  // namespace QXmpp  Private
#else
}  // namespace QXmpp::Private
#endif
