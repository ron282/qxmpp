// SPDX-FileCopyrightText: 2010 Jeremy Lainé <jeremy.laine@m4x.org>
// SPDX-FileCopyrightText: 2022 Melvin Keskin <melvo@olomono.de>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "QXmppJingleData.h"

#include "QXmppConstants_p.h"
#include "QXmppUtils_p.h"

#include "StringLiterals.h"

#include <QDate>
#include <QDateTime>
#include <QDomElement>
#include <QRegularExpression>

using namespace QXmpp::Private;

static const int RTP_COMPONENT = 1;

static const char *jingle_actions[] = {
    "content-accept",
    "content-add",
    "content-modify",
    "content-reject",
    "content-remove",
    "description-info",
    "security-info",
    "session-accept",
    "session-info",
    "session-initiate",
    "session-terminate",
    "transport-accept",
    "transport-info",
    "transport-reject",
    "transport-replace",
};

static const char *jingle_reasons[] = {
    "",
    "alternative-session",
    "busy",
    "cancel",
    "connectivity-error",
    "decline",
    "expired",
    "failed-application",
    "failed-transport",
    "general-error",
    "gone",
    "incompatible-parameters",
    "media-error",
    "security-error",
    "success",
    "timeout",
    "unsupported-applications",
    "unsupported-transports",
};

constexpr auto JINGLE_RTP_ERROR_CONDITIONS = to_array<QStringView>({
    {},
    QStringView(u"invalid-crypto"),
    QStringView(u"crypto-required"),
});

constexpr auto JINGLE_RTP_HEADER_EXTENSIONS_SENDERS = to_array<QStringView>({
    QStringView(u"both"),
    QStringView(u"initiator"),
    QStringView(u"responder"),
});

static QString formatFingerprint(const QByteArray &digest)
{
    QString fingerprint;
    const QString hx = QString::fromUtf8(digest.toHex()).toUpper();
    for (int i = 0; i < hx.size(); i += 2) {
        if (!fingerprint.isEmpty()) {
            fingerprint += u':';
        }
        fingerprint += hx.mid(i, 2);
    }
    return fingerprint;
}

static QByteArray parseFingerprint(const QString &fingerprint)
{
    QString z = fingerprint;
    z.replace(u':', QChar());
    return QByteArray::fromHex(z.toUtf8());
}

static QString addressToSdp(const QHostAddress &host)
{
    return u"IN %1 %2"_s.arg(host.protocol() == QAbstractSocket::IPv6Protocol ? u"IP6"_s : u"IP4"_s, host.toString());
}

static bool candidateParseSdp(QXmppJingleCandidate *candidate, const QString &sdp)
{
    if (!sdp.startsWith(u"candidate:"_s)) {
        return false;
    }

    const QStringList bits = sdp.mid(10).split(u' ');
    if (bits.size() < 6) {
        return false;
    }

    candidate->setFoundation(bits[0]);
    candidate->setComponent(bits[1].toInt());
    candidate->setProtocol(bits[2].toLower());
    candidate->setPriority(bits[3].toInt());
    candidate->setHost(QHostAddress(bits[4]));
    candidate->setPort(bits[5].toInt());
    for (int i = 6; i < bits.size() - 1; i += 2) {
        if (bits[i] == u"typ") {
            bool ok;
            candidate->setType(QXmppJingleCandidate::typeFromString(bits[i + 1], &ok));
            if (!ok) {
                return false;
            }
        } else if (bits[i] == u"generation") {
            candidate->setGeneration(bits[i + 1].toInt());
        } else {
            qWarning() << "Candidate SDP contains unknown attribute" << bits[i];
            return false;
        }
    }
    return true;
}

static QString candidateToSdp(const QXmppJingleCandidate &candidate)
{
    return u"candidate:%1 %2 %3 %4 %5 %6 typ %7 generation %8"_s.arg(candidate.foundation(), QString::number(candidate.component()), candidate.protocol(), QString::number(candidate.priority()), candidate.host().toString(), QString::number(candidate.port()), QXmppJingleCandidate::typeToString(candidate.type()), QString::number(candidate.generation()));
}

// Parses all found SDP parameter elements of parent into parameters.
static void parseSdpParameters(const QDomElement &parent, QVector<QXmppSdpParameter> &parameters)
{
    for (const auto &childElement : iterChildElements(parent)) {
        if (QXmppSdpParameter::isSdpParameter(childElement)) {
            QXmppSdpParameter parameter;
            parameter.parse(childElement);
            parameters.append(parameter);
        }
    }
}

// Serializes the SDP parameters.
static void sdpParametersToXml(QXmlStreamWriter *writer, const QVector<QXmppSdpParameter> &parameters)
{
    for (const auto &parameter : parameters) {
        parameter.toXml(writer);
    }
}

// Parses all found RTP Feedback Negotiation elements inside of parent into properties and
// intervals.
static void parseJingleRtpFeedbackNegotiationElements(const QDomElement &parent, QVector<QXmppJingleRtpFeedbackProperty> &properties, QVector<QXmppJingleRtpFeedbackInterval> &intervals)
{
    for (const auto &child : iterChildElements(parent)) {
        if (QXmppJingleRtpFeedbackProperty::isJingleRtpFeedbackProperty(child)) {
            QXmppJingleRtpFeedbackProperty property;
            property.parse(child);
            properties.append(property);
        } else if (QXmppJingleRtpFeedbackInterval::isJingleRtpFeedbackInterval(child)) {
            QXmppJingleRtpFeedbackInterval interval;
            interval.parse(child);
            intervals.append(interval);
        }
    }
}

// Serializes the RTP feedback properties and intervals.
static void jingleRtpFeedbackNegotiationElementsToXml(QXmlStreamWriter *writer, const QVector<QXmppJingleRtpFeedbackProperty> &properties, const QVector<QXmppJingleRtpFeedbackInterval> &intervals)
{
    for (const auto &property : properties) {
        property.toXml(writer);
    }

    for (const auto &interval : intervals) {
        interval.toXml(writer);
    }
}

// Parses all found RTP Header Extensions Negotiation elements inside of parent into properties and
// isRtpHeaderExtensionMixingAllowed.
static void parseJingleRtpHeaderExtensionsNegotiationElements(const QDomElement &parent, QVector<QXmppJingleRtpHeaderExtensionProperty> &properties, bool &isRtpHeaderExtensionMixingAllowed)
{
    for (const auto &child : iterChildElements(parent)) {
        if (QXmppJingleRtpHeaderExtensionProperty::isJingleRtpHeaderExtensionProperty(child)) {
            QXmppJingleRtpHeaderExtensionProperty property;
            property.parse(child);
            properties.append(property);
        } else if (child.tagName() == u"extmap-allow-mixed" && child.namespaceURI() == ns_jingle_rtp_header_extensions_negotiation) {
            isRtpHeaderExtensionMixingAllowed = true;
        }
    }
}

// Serializes the RTP header extension properties and isRtpHeaderExtensionMixingAllowed.
static void jingleRtpHeaderExtensionsNegotiationElementsToXml(QXmlStreamWriter *writer, const QVector<QXmppJingleRtpHeaderExtensionProperty> &properties, bool isRtpHeaderExtensionMixingAllowed)
{
    for (const auto &property : properties) {
        property.toXml(writer);
    }

    if (isRtpHeaderExtensionMixingAllowed) {
        writer->writeStartElement(QSL65("extmap-allow-mixed"));
        writer->writeDefaultNamespace(toString65(ns_jingle_rtp_header_extensions_negotiation));
        writer->writeEndElement();
    }
}

class QXmppJingleIqContentPrivate : public QSharedData
{
public:
    QXmppJingleIqContentPrivate();

    QString creator;
    QString disposition;
    QString name;
    QString senders;

    QXmppJingleDescription description;
    bool isRtpMultiplexingSupported = false;

    QString transportType;
    QString transportUser;
    QString transportPassword;

    QByteArray transportFingerprint;
    QString transportFingerprintHash;
    QString transportFingerprintSetup;

    QList<QXmppJingleCandidate> transportCandidates;

    // XEP-0167: Jingle RTP Sessions
    std::optional<QXmppJingleRtpEncryption> rtpEncryption;

    // XEP-0293: Jingle RTP Feedback Negotiation
    QVector<QXmppJingleRtpFeedbackProperty> rtpFeedbackProperties;
    QVector<QXmppJingleRtpFeedbackInterval> rtpFeedbackIntervals;

    // XEP-0294: Jingle RTP Header Extensions Negotiation
    QVector<QXmppJingleRtpHeaderExtensionProperty> rtpHeaderExtensionProperties;
    bool isRtpHeaderExtensionMixingAllowed = false;
};

QXmppJingleIqContentPrivate::QXmppJingleIqContentPrivate()
{
    description.setSsrc(0);
}

///
/// \enum QXmppJingleIq::Creator
///
/// Party that originially generated the content type
///
/// \since QXmpp 1.5
///

///
/// \struct QXmppJingleIq::RtpSessionStateActive
///
/// Actively participating in the session after having been on mute or having put the other party on
/// hold
///
/// \since QXmpp 1.5
///

///
/// \struct QXmppJingleIq::RtpSessionStateHold
///
/// Temporarily not listening for media from the other party
///
/// \since QXmpp 1.5
///

///
/// \struct QXmppJingleIq::RtpSessionStateUnhold
///
/// Ending hold state
///
/// \since QXmpp 1.5
///

///
/// \struct QXmppJingleIq::RtpSessionStateMuting
///
/// State for muting or unmuting
///
/// \since QXmpp 1.5
///

///
/// \struct QXmppJingleIq::RtpSessionStateRinging
///
/// State after the callee acknowledged the call but did not yet interacted with it
///
/// \since QXmpp 1.5
///

///
/// \typedef QXmppJingleIq::RtpSessionState
///
/// Contains the state of an RTP session as specified by \xep{0167, Jingle RTP Sessions}
/// Informational Messages.
///
/// \since QXmpp 1.5
///

/// Constructs an empty content.
QXmppJingleIq::Content::Content()
    : d(new QXmppJingleIqContentPrivate())
{
}

/// Copy-constructor.
QXmppJingleIq::Content::Content(const QXmppJingleIq::Content &other) = default;
/// Move-constructor.
QXmppJingleIq::Content::Content(QXmppJingleIq::Content &&) = default;
/// Assignment operator.
QXmppJingleIq::Content &QXmppJingleIq::Content::operator=(const QXmppJingleIq::Content &) = default;
/// Move-assignment operator.
QXmppJingleIq::Content &QXmppJingleIq::Content::operator=(QXmppJingleIq::Content &&) = default;

QXmppJingleIq::Content::~Content() = default;

QString QXmppJingleIq::Content::creator() const
{
    return d->creator;
}

void QXmppJingleIq::Content::setCreator(const QString &creator)
{
    d->creator = creator;
}

QString QXmppJingleIq::Content::name() const
{
    return d->name;
}

void QXmppJingleIq::Content::setName(const QString &name)
{
    d->name = name;
}

QString QXmppJingleIq::Content::senders() const
{
    return d->senders;
}

void QXmppJingleIq::Content::setSenders(const QString &senders)
{
    d->senders = senders;
}

///
/// Returns the description as specified by
/// \xep{0167, Jingle RTP Sessions} and RFC 3550.
///
/// \since QXmpp 0.9
///
QXmppJingleDescription QXmppJingleIq::Content::description() const
{
    return d->description;
}

void QXmppJingleIq::Content::setDescription(const QXmppJingleDescription &description)
{
    d->description = description;
}

/// \deprecated This method is deprecated since QXmpp 1.6. Use
/// \c QXmppJingleIq::Conent::description().media() instead.
QString QXmppJingleIq::Content::descriptionMedia() const
{
    return d->description.media();
}

/// \deprecated This method is deprecated since QXmpp 1.6. Use
/// \c QXmppJingleIq::Conent::description().setMedia() instead.
void QXmppJingleIq::Content::setDescriptionMedia(const QString &media)
{
    d->description.setMedia(media);
}

/// Returns the description's 32-bit synchronization source for the media stream as specified by
/// \xep{0167, Jingle RTP Sessions} and RFC 3550.
///
/// \since QXmpp 0.9
/// \deprecated This method is deprecated since QXmpp 1.6. Use
/// \c QXmppJingleIq::Content::description().setSsrc() instead.
///
quint32 QXmppJingleIq::Content::descriptionSsrc() const
{
    return d->description.ssrc();
}

/// \deprecated This method is deprecated since QXmpp 1.6. Use
/// \c QXmppJingleIq::Conent::description().setSsrc() instead.
void QXmppJingleIq::Content::setDescriptionSsrc(quint32 ssrc)
{
    d->description.setSsrc(ssrc);
}

/// \deprecated This method is deprecated since QXmpp 1.6. Use
/// \c QXmppJingleIq::Conent::description().addPayloadType() instead.
void QXmppJingleIq::Content::addPayloadType(const QXmppJinglePayloadType &payload)
{
    d->description.addPayloadType(payload);
}

/// \deprecated This method is deprecated since QXmpp 1.6. Use
/// \c QXmppJingleIq::Conent::description().payloadTypes() instead.
QList<QXmppJinglePayloadType> QXmppJingleIq::Content::payloadTypes() const
{
    return d->description.payloadTypes();
}

/// \deprecated This method is deprecated since QXmpp 1.6. Use
/// \c QXmppJingleIq::Conent::description().setPayloadTypes() instead.
void QXmppJingleIq::Content::setPayloadTypes(const QList<QXmppJinglePayloadType> &payloadTypes)
{
    d->description.setPayloadTypes(payloadTypes);
}

///
/// Returns whether multiplexing of RTP data and control packets on a single port is supported as
/// specified by \xep{0167, Jingle RTP Sessions} and  RFC 5761.
///
/// \return whether multiplexing of RTP data and control packets is supported
///
/// \since QXmpp 1.5
///
bool QXmppJingleIq::Content::isRtpMultiplexingSupported() const
{
    return d->isRtpMultiplexingSupported;
}

///
/// Sets whether multiplexing of RTP data and control packets on a single port is supported as
/// specified by \xep{0167, Jingle RTP Sessions} and  RFC 5761.
///
/// \param isRtpMultiplexingSupported whether multiplexing of RTP data and control packets is
///        supported
///
/// \since QXmpp 1.5
///
void QXmppJingleIq::Content::setRtpMultiplexingSupported(bool isRtpMultiplexingSupported)
{
    d->isRtpMultiplexingSupported = isRtpMultiplexingSupported;
}

///
/// Returns the encryption used for SRTP negotiation as specified by
/// \xep{0167, Jingle RTP Sessions}.
///
/// \return the RTP encryption via SRTP
///
/// \since QXmpp 1.5
///
std::optional<QXmppJingleRtpEncryption> QXmppJingleIq::Content::rtpEncryption() const
{
    return d->rtpEncryption;
}

///
/// Sets the encryption used for SRTP negotiation as specified by \xep{0167, Jingle RTP Sessions}.
///
/// \param rtpEncryption RTP encryption via SRTP
///
/// \since QXmpp 1.5
///
void QXmppJingleIq::Content::setRtpEncryption(const std::optional<QXmppJingleRtpEncryption> &rtpEncryption)
{
    d->rtpEncryption = rtpEncryption;
}

void QXmppJingleIq::Content::addTransportCandidate(const QXmppJingleCandidate &candidate)
{
    d->transportType = ns_jingle_ice_udp.toString();
    d->transportCandidates << candidate;
}

QList<QXmppJingleCandidate> QXmppJingleIq::Content::transportCandidates() const
{
    return d->transportCandidates;
}

///
/// Sets a list of transport candidates.
///
/// \since QXmpp 0.9.2
///
void QXmppJingleIq::Content::setTransportCandidates(const QList<QXmppJingleCandidate> &candidates)
{
    d->transportType = candidates.isEmpty() ? QString() : ns_jingle_ice_udp.toString();
    d->transportCandidates = candidates;
}

QString QXmppJingleIq::Content::transportUser() const
{
    return d->transportUser;
}

void QXmppJingleIq::Content::setTransportUser(const QString &user)
{
    d->transportUser = user;
}

QString QXmppJingleIq::Content::transportPassword() const
{
    return d->transportPassword;
}

void QXmppJingleIq::Content::setTransportPassword(const QString &password)
{
    d->transportPassword = password;
}

///
/// Returns the properties of RTP feedback.
///
/// \return the RTP feedback properties
///
/// \since QXmpp 1.5
///
QVector<QXmppJingleRtpFeedbackProperty> QXmppJingleIq::Content::rtpFeedbackProperties() const
{
    return d->rtpFeedbackProperties;
}

///
/// Sets the properties of RTP feedback.
///
/// \param rtpFeedbackProperties RTP feedback properties
///
/// \since QXmpp 1.5
///
void QXmppJingleIq::Content::setRtpFeedbackProperties(const QVector<QXmppJingleRtpFeedbackProperty> &rtpFeedbackProperties)
{
    d->rtpFeedbackProperties = rtpFeedbackProperties;
}

///
/// Returns the intervals of RTP feedback.
///
/// \return the RTP feedback intervals
///
/// \since QXmpp 1.5
///
QVector<QXmppJingleRtpFeedbackInterval> QXmppJingleIq::Content::rtpFeedbackIntervals() const
{
    return d->rtpFeedbackIntervals;
}

///
/// Sets the intervals of RTP feedback.
///
/// \param rtpFeedbackIntervals RTP feedback intervals
///
/// \since QXmpp 1.5
///
void QXmppJingleIq::Content::setRtpFeedbackIntervals(const QVector<QXmppJingleRtpFeedbackInterval> &rtpFeedbackIntervals)
{
    d->rtpFeedbackIntervals = rtpFeedbackIntervals;
}

///
/// Returns the RTP header extension properties.
///
/// \return the RTP header extension properties
///
/// \since QXmpp 1.5
///
QVector<QXmppJingleRtpHeaderExtensionProperty> QXmppJingleIq::Content::rtpHeaderExtensionProperties() const
{
    return d->rtpHeaderExtensionProperties;
}

///
/// Sets the RTP header extension properties.
///
/// \param rtpHeaderExtensionProperties RTP header extension properties
///
/// \since QXmpp 1.5
///
void QXmppJingleIq::Content::setRtpHeaderExtensionProperties(const QVector<QXmppJingleRtpHeaderExtensionProperty> &rtpHeaderExtensionProperties)
{
    d->rtpHeaderExtensionProperties = rtpHeaderExtensionProperties;
}

///
/// Returns whether mixing of RTP header extensions is allowed corresponding to the
/// "extmap-allow-mixed" element as specified by
/// \xep{0293, Jingle RTP Header Extensions Negotiation}.
///
/// \return whether mixing of RTP header extensions is allowed
///
/// \since QXmpp 1.5
///
bool QXmppJingleIq::Content::isRtpHeaderExtensionMixingAllowed() const
{
    return d->isRtpHeaderExtensionMixingAllowed;
}

///
/// Sets whether mixing of RTP header extensions is allowed corresponding to the
/// "extmap-allow-mixed" element as specified by
/// \xep{0293, Jingle RTP Header Extensions Negotiation}.
///
/// \param isAllowed whether mixing of RTP header extensions is allowed
///
/// \since QXmpp 1.5
///
void QXmppJingleIq::Content::setRtpHeaderExtensionMixingAllowed(bool isRtpHeaderExtensionMixingAllowed)
{
    d->isRtpHeaderExtensionMixingAllowed = isRtpHeaderExtensionMixingAllowed;
}

///
/// Returns the fingerprint hash value for the transport key.
///
/// This is used for DTLS-SRTP as defined in \xep{0320}.
///
/// \since QXmpp 0.9
///
QByteArray QXmppJingleIq::Content::transportFingerprint() const
{
    return d->transportFingerprint;
}

///
/// Sets the fingerprint hash value for the transport key.
///
/// This is used for DTLS-SRTP as defined in \xep{0320}.
///
/// \since QXmpp 0.9
///
void QXmppJingleIq::Content::setTransportFingerprint(const QByteArray &fingerprint)
{
    d->transportFingerprint = fingerprint;
}

///
/// Returns the fingerprint hash algorithm for the transport key.
///
/// This is used for DTLS-SRTP as defined in \xep{0320}.
///
/// \since QXmpp 0.9
///
QString QXmppJingleIq::Content::transportFingerprintHash() const
{
    return d->transportFingerprintHash;
}

///
/// Sets the fingerprint hash algorithm for the transport key.
///
/// This is used for DTLS-SRTP as defined in \xep{0320}.
///
/// \since QXmpp 0.9
///
void QXmppJingleIq::Content::setTransportFingerprintHash(const QString &hash)
{
    d->transportFingerprintHash = hash;
}

///
/// Returns the setup role for the encrypted transport.
///
/// This is used for DTLS-SRTP as defined in \xep{0320}.
///
/// \since QXmpp 0.9
///
QString QXmppJingleIq::Content::transportFingerprintSetup() const
{
    return d->transportFingerprintSetup;
}

///
/// Sets the setup role for the encrypted transport.
///
/// This is used for DTLS-SRTP as defined in \xep{0320}.
///
/// \since QXmpp 0.9
///
void QXmppJingleIq::Content::setTransportFingerprintSetup(const QString &setup)
{
    d->transportFingerprintSetup = setup;
}

/// \cond
void QXmppJingleIq::Content::parse(const QDomElement &element)
{
    d->creator = element.attribute(u"creator"_s);
    d->disposition = element.attribute(u"disposition"_s);
    d->name = element.attribute(u"name"_s);
    d->senders = element.attribute(u"senders"_s);

    // description
    QDomElement descriptionElement = element.firstChildElement(u"description"_s);
    d->description.setType(descriptionElement.namespaceURI());
    d->description.setMedia(descriptionElement.attribute(u"media"_s));
    d->description.setSsrc(parseInt<uint32_t>(descriptionElement.attribute(u"ssrc"_s)).value_or(0));
    d->isRtpMultiplexingSupported = !descriptionElement.firstChildElement(u"rtcp-mux"_s).isNull();

    for (const auto &childElement : iterChildElements(descriptionElement)) {
        if (QXmppJingleRtpEncryption::isJingleRtpEncryption(childElement)) {
            QXmppJingleRtpEncryption encryption;
            encryption.parse(childElement);
            d->rtpEncryption = encryption;
            break;
        }
    }

    parseJingleRtpFeedbackNegotiationElements(descriptionElement, d->rtpFeedbackProperties, d->rtpFeedbackIntervals);
    parseJingleRtpHeaderExtensionsNegotiationElements(descriptionElement, d->rtpHeaderExtensionProperties, d->isRtpHeaderExtensionMixingAllowed);

    for (const auto &child : iterChildElements(descriptionElement, u"payload-type")) {
        QXmppJinglePayloadType payload;
        payload.parse(child);
        d->description.addPayloadType(payload);
    }

    // transport
    QDomElement transportElement = element.firstChildElement(u"transport"_s);
    d->transportType = transportElement.namespaceURI();
    d->transportUser = transportElement.attribute(u"ufrag"_s);
    d->transportPassword = transportElement.attribute(u"pwd"_s);
    for (const auto &child : iterChildElements(transportElement, u"candidate")) {
        QXmppJingleCandidate candidate;
        candidate.parse(child);
        d->transportCandidates << candidate;
    }

    /// XEP-0320
    auto child = firstChildElement(transportElement, u"fingerprint");
    if (!child.isNull()) {
        d->transportFingerprint = parseFingerprint(child.text());
        d->transportFingerprintHash = child.attribute(u"hash"_s);
        d->transportFingerprintSetup = child.attribute(u"setup"_s);
    }
}

void QXmppJingleIq::Content::toXml(QXmlStreamWriter *writer) const
{
    if (d->creator.isEmpty() || d->name.isEmpty()) {
        return;
    }

    writer->writeStartElement(QSL65("content"));
    writeOptionalXmlAttribute(writer, u"creator", d->creator);
    writeOptionalXmlAttribute(writer, u"disposition", d->disposition);
    writeOptionalXmlAttribute(writer, u"name", d->name);
    writeOptionalXmlAttribute(writer, u"senders", d->senders);

    // description
    if (!d->description.type().isEmpty() || !d->description.payloadTypes().isEmpty()) {
        writer->writeStartElement(QSL65("description"));
        writer->writeDefaultNamespace(d->description.type());
        writeOptionalXmlAttribute(writer, u"media", d->description.media());

        if (d->description.ssrc()) {
            writer->writeAttribute(QSL65("ssrc"), QString::number(d->description.ssrc()));
        }

        if (d->isRtpMultiplexingSupported) {
            writer->writeEmptyElement(u"rtcp-mux"_s);
        }

        if (d->rtpEncryption) {
            d->rtpEncryption->toXml(writer);
        }

        jingleRtpFeedbackNegotiationElementsToXml(writer, d->rtpFeedbackProperties, d->rtpFeedbackIntervals);
        jingleRtpHeaderExtensionsNegotiationElementsToXml(writer, d->rtpHeaderExtensionProperties, d->isRtpHeaderExtensionMixingAllowed);

        for (const auto &payload : d->description.payloadTypes()) {
            payload.toXml(writer);
        }

        writer->writeEndElement();
    }

    // transport
    if (!d->transportType.isEmpty() || !d->transportCandidates.isEmpty()) {
        writer->writeStartElement(QSL65("transport"));
        writer->writeDefaultNamespace(d->transportType);
        writeOptionalXmlAttribute(writer, u"ufrag", d->transportUser);
        writeOptionalXmlAttribute(writer, u"pwd", d->transportPassword);
        for (const auto &candidate : d->transportCandidates) {
            candidate.toXml(writer);
        }

        // XEP-0320: Use of DTLS-SRTP in Jingle Sessions
        if (!d->transportFingerprint.isEmpty() && !d->transportFingerprintHash.isEmpty()) {
            writer->writeStartElement(QSL65("fingerprint"));
            writer->writeDefaultNamespace(toString65(ns_jingle_dtls));
            writer->writeAttribute(QSL65("hash"), d->transportFingerprintHash);
            writer->writeAttribute(QSL65("setup"), d->transportFingerprintSetup);
            writer->writeCharacters(formatFingerprint(d->transportFingerprint));
            writer->writeEndElement();
        }
        writer->writeEndElement();
    }

    writer->writeEndElement();
}

bool QXmppJingleIq::Content::parseSdp(const QString &sdp)
{
    QList<QXmppJinglePayloadType> payloads;
    for (auto &line : sdp.split(QChar(u'\n'))) {
        if (line.endsWith(u'\r')) {
            line.resize(line.size() - 1);
        }
        if (line.startsWith(u"a="_s)) {
            qsizetype idx = line.indexOf(u':');
            const QString attrName = idx != -1 ? line.mid(2, idx - 2) : line.mid(2);
            const QString attrValue = idx != -1 ? line.mid(idx + 1) : QString();

            if (attrName == u"candidate") {
                QXmppJingleCandidate candidate;
                if (!candidateParseSdp(&candidate, line.mid(2))) {
                    qWarning() << "Could not parse candidate" << line;
                    return false;
                }
                addTransportCandidate(candidate);
            } else if (attrName == u"fingerprint") {
                const QStringList bits = attrValue.split(u' ');
                if (bits.size() > 1) {
                    d->transportFingerprintHash = bits[0];
                    d->transportFingerprint = parseFingerprint(bits[1]);
                }
            } else if (attrName == u"fmtp") {
                qsizetype spIdx = attrValue.indexOf(u' ');
                if (spIdx == -1) {
                    qWarning() << "Could not parse payload parameters" << line;
                    return false;
                }
                const int id = attrValue.left(spIdx).toInt();
                const QString paramStr = attrValue.mid(spIdx + 1);
                for (auto &payload : payloads) {
                    if (payload.id() == id) {
                        QMap<QString, QString> params;
                        if (payload.name() == u"telephone-event") {
                            params.insert(u"events"_s, paramStr);
                        } else {
                            thread_local static const auto regex = QRegularExpression(u";\\s*"_s);
                            const auto paramParts = paramStr.split(regex);
                            for (const auto &p : paramParts) {
                                const QStringList bits = p.split(u'=');
                                if (bits.size() == 2) {
                                    params.insert(bits.at(0), bits.at(1));
                                }
                            }
                        }
                        payload.setParameters(params);
                    }
                }
            } else if (attrName == u"rtpmap") {
                // payload type map
                const QStringList bits = attrValue.split(u' ');
                if (bits.size() != 2) {
                    continue;
                }
                bool ok = false;
                const int id = bits[0].toInt(&ok);
                if (!ok) {
                    continue;
                }

                const QStringList args = bits[1].split(u'/');
                for (auto &payload : payloads) {
                    if (payload.id() == id) {
                        payload.setName(args[0]);
                        if (args.size() > 1) {
                            payload.setClockrate(args[1].toUInt());
                        }
                        if (args.size() > 2) {
                            payload.setChannels(parseInt<uint8_t>(args[2]).value_or(0));
                        }
                    }
                }
            } else if (attrName == u"ice-ufrag") {
                d->transportUser = attrValue;
            } else if (attrName == u"ice-pwd") {
                d->transportPassword = attrValue;
            } else if (attrName == u"setup") {
                d->transportFingerprintSetup = attrValue;
            } else if (attrName == u"ssrc") {
                const QStringList bits = attrValue.split(u' ');
                if (bits.isEmpty()) {
                    qWarning() << "Could not parse ssrc" << line;
                    return false;
                }
                d->description.setSsrc(bits[0].toULong());
            }
        } else if (line.startsWith(u"m="_s)) {
            // FIXME: what do we do with the profile (bits[2]) ?
            QStringList bits = line.mid(2).split(u' ');
            if (bits.size() < 3) {
                qWarning() << "Could not parse media" << line;
                return false;
            }
            d->description.setMedia(bits[0]);

            // parse payload types
            for (int i = 3; i < bits.size(); ++i) {
                bool ok = false;
                int id = bits[i].toInt(&ok);
                if (!ok) {
                    continue;
                }
                QXmppJinglePayloadType payload;
                payload.setId(id);
                payloads << payload;
            }
        }
    }

    d->description.setPayloadTypes(payloads);
    return true;
}

static bool candidateLessThan(const QXmppJingleCandidate &c1, const QXmppJingleCandidate &c2)
{
    if (c1.type() == c2.type()) {
        return c1.priority() > c2.priority();
    } else {
        return c1.type() == QXmppJingleCandidate::ServerReflexiveType;
    }
}

QString QXmppJingleIq::Content::toSdp() const
{
    // get default candidate
    QHostAddress localRtpAddress = QHostAddress::Any;
    quint16 localRtpPort = 0;
    QList<QXmppJingleCandidate> sortedCandidates = d->transportCandidates;
    std::sort(sortedCandidates.begin(), sortedCandidates.end(), candidateLessThan);
    for (const auto &candidate : sortedCandidates) {
        if (candidate.component() == RTP_COMPONENT) {
            localRtpAddress = candidate.host();
            localRtpPort = candidate.port();
            break;
        }
    }

    QStringList sdp;

    // media
    QString payloads;
    QStringList attrs;
    for (const QXmppJinglePayloadType &payload : d->description.payloadTypes()) {
        payloads += u" ";
        payloads += QString::number(payload.id());
        QString rtpmap = QString::number(payload.id()) + u' ' + payload.name() + u'/' + QString::number(payload.clockrate());
        if (payload.channels() > 1) {
            rtpmap += u'/' + QString::number(payload.channels());
        }
        attrs << QStringLiteral(u"a=rtpmap:") + rtpmap;

        // payload parameters
        QStringList paramList;
        const QMap<QString, QString> params = payload.parameters();
        if (payload.name() == u"telephone-event") {
            if (params.contains(u"events"_s)) {
                paramList << params.value(u"events"_s);
            }
        } else {
            QMap<QString, QString>::const_iterator i;
            for (i = params.begin(); i != params.end(); ++i) {
                paramList << i.key() + u"="_s + i.value();
            }
        }
        if (!paramList.isEmpty()) {
			attrs << QString(u"a=fmtp:" + QString::number(payload.id()) + u' ' + paramList.join(u"; "_s));
//            attrs << QString(QStringLiteral("a=fmtp:") + QString::number(payload.id()) + u' ' + paramList.join(QStringLiteral(u"; ")));
        }
    }
    sdp << u"m=%1 %2 RTP/AVP%3"_s.arg(d->description.media(), QString::number(localRtpPort), payloads);
    sdp << u"c=%1"_s.arg(addressToSdp(localRtpAddress));
    sdp += attrs;

    // transport
    for (const auto &candidate : d->transportCandidates) {
        sdp << u"a=%1"_s.arg(candidateToSdp(candidate));
    }
    if (!d->transportUser.isEmpty()) {
        sdp << u"a=ice-ufrag:%1"_s.arg(d->transportUser);
    }
    if (!d->transportPassword.isEmpty()) {
        sdp << u"a=ice-pwd:%1"_s.arg(d->transportPassword);
    }
    if (!d->transportFingerprint.isEmpty() && !d->transportFingerprintHash.isEmpty()) {
        sdp << u"a=fingerprint:%1 %2"_s.arg(d->transportFingerprintHash, formatFingerprint(d->transportFingerprint));
    }
    if (!d->transportFingerprintSetup.isEmpty()) {
        sdp << u"a=setup:%1"_s.arg(d->transportFingerprintSetup);
    }

    return sdp.join(QStringLiteral(u"\r\n")) + QStringLiteral(u"\r\n");
}

/// \endcond

class QXmppJingleIqReasonPrivate : public QSharedData
{
public:
    QXmppJingleIqReasonPrivate();

    QString m_text;
    QXmppJingleReason::Type m_type;
    QXmppJingleReason::RtpErrorCondition m_rtpErrorCondition;
};

QXmppJingleIqReasonPrivate::QXmppJingleIqReasonPrivate()
    : m_type(QXmppJingleReason::Type::None),
      m_rtpErrorCondition(QXmppJingleReason::RtpErrorCondition::NoErrorCondition)
{
}

///
/// \class QXmppJingleReason
///
/// The QXmppJingleReason class represents the "reason" element of a
/// QXmppJingle element.
///

QXmppJingleReason::QXmppJingleReason()
    : d(new QXmppJingleIqReasonPrivate())
{
}

/// Returns the reason's textual description.

QString QXmppJingleReason::text() const
{
    return d->m_text;
}

/// Sets the reason's textual description.

void QXmppJingleReason::setText(const QString &text)
{
    d->m_text = text;
}

/// Gets the reason's type.

QXmppJingleReason::Type QXmppJingleReason::type() const
{
    return d->m_type;
}

/// Sets the reason's type.

void QXmppJingleReason::setType(QXmppJingleReason::Type type)
{
    d->m_type = type;
}

///
/// Returns the RTP error condition as specified by \xep{0167, Jingle RTP Sessions}.
///
/// \return the RTP error condition
///
/// \since QXmpp 1.5
///
QXmppJingleReason::RtpErrorCondition QXmppJingleReason::rtpErrorCondition() const
{
    return d->m_rtpErrorCondition;
}

///
/// Sets the RTP error condition as specified by \xep{0167, Jingle RTP Sessions}.
///
/// \param rtpErrorCondition RTP error condition
///
/// \since QXmpp 1.5
///
void QXmppJingleReason::setRtpErrorCondition(RtpErrorCondition rtpErrorCondition)
{
    d->m_rtpErrorCondition = rtpErrorCondition;
}

/// \cond
void QXmppJingleReason::parse(const QDomElement &element)
{
    d->m_text = element.firstChildElement(u"text"_s).text();
    for (int i = AlternativeSession; i <= UnsupportedTransports; i++) {
        if (!element.firstChildElement(QString::fromLocal8Bit(jingle_reasons[i])).isNull()) {
            d->m_type = static_cast<Type>(i);
            break;
        }
    }

    auto child = firstChildElement(element, {}, ns_jingle_rtp_errors);
    d->m_rtpErrorCondition = enumFromString<RtpErrorCondition>(JINGLE_RTP_ERROR_CONDITIONS, child.tagName())
                                 .value_or(NoErrorCondition);
}

void QXmppJingleReason::toXml(QXmlStreamWriter *writer) const
{
    if (d->m_type < AlternativeSession || d->m_type > UnsupportedTransports) {
        return;
    }

    writer->writeStartElement(QSL65("reason"));
    writer->writeDefaultNamespace(toString65(ns_jingle));

    if (!d->m_text.isEmpty()) {
        writeXmlTextElement(writer, u"text", d->m_text);
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    writer->writeEmptyElement(jingle_reasons[d->m_type]);
#else
    writer->writeEmptyElement(QString::fromUtf8(jingle_reasons[d->m_type]));
#endif

    if (d->m_rtpErrorCondition != NoErrorCondition) {
        writer->writeStartElement(toString65(JINGLE_RTP_ERROR_CONDITIONS.at(d->m_rtpErrorCondition)));
        writer->writeDefaultNamespace(toString65(ns_jingle_rtp_errors));
        writer->writeEndElement();
    }

    writer->writeEndElement();
}
/// \endcond

class QXmppJingleIqPrivate : public QSharedData
{
public:
    QXmppJingleIqPrivate();

    QXmppJingleIq::Action action;
    QString initiator;
    QString responder;
    QString sid;

    QString mujiGroupChatJid;

    QList<QXmppJingleIq::Content> contents;
    QXmppJingleReason reason;

    std::optional<QXmppJingleIq::RtpSessionState> rtpSessionState;
};

QXmppJingleIqPrivate::QXmppJingleIqPrivate()
    : action(QXmppJingleIq::ContentAccept)
{
}

/// Constructs a QXmppJingleIq.
QXmppJingleIq::QXmppJingleIq()
    : d(new QXmppJingleIqPrivate())
{
}

/// Copy-constructor.
QXmppJingleIq::QXmppJingleIq(const QXmppJingleIq &) = default;
/// Move-constructor.
QXmppJingleIq::QXmppJingleIq(QXmppJingleIq &&) = default;

QXmppJingleIq::~QXmppJingleIq() = default;

/// Assignment operator.
QXmppJingleIq &QXmppJingleIq::operator=(const QXmppJingleIq &) = default;
/// Move-assignment operator.
QXmppJingleIq &QXmppJingleIq::operator=(QXmppJingleIq &&) = default;

///
/// Returns the Jingle IQ's action.
///
QXmppJingleIq::Action QXmppJingleIq::action() const
{
    return d->action;
}

/// Sets the Jingle IQ's action.
///
/// \param action

void QXmppJingleIq::setAction(QXmppJingleIq::Action action)
{
    d->action = action;
}

///
/// Adds an element to the IQ's content elements.
///
/// \since QXmpp 0.9.2
///
void QXmppJingleIq::addContent(const QXmppJingleIq::Content &content)
{
    d->contents << content;
}

///
/// Returns the IQ's content elements.
///
/// \since QXmpp 0.9.2
///
QList<QXmppJingleIq::Content> QXmppJingleIq::contents() const
{
    return d->contents;
}

///
/// Sets the IQ's content elements.
///
/// \since QXmpp 0.9.2
///
void QXmppJingleIq::setContents(const QList<QXmppJingleIq::Content> &contents)
{
    d->contents = contents;
}

/// Returns the session initiator.
QString QXmppJingleIq::initiator() const
{
    return d->initiator;
}

/// Sets the session initiator.
void QXmppJingleIq::setInitiator(const QString &initiator)
{
    d->initiator = initiator;
}

/// Returns a reference to the IQ's reason element.
QXmppJingleReason &QXmppJingleIq::reason()
{
    return d->reason;
}

/// Returns a const reference to the IQ's reason element.
const QXmppJingleReason &QXmppJingleIq::reason() const
{
    return d->reason;
}

/// Returns the session responder.
QString QXmppJingleIq::responder() const
{
    return d->responder;
}

/// Sets the session responder.
void QXmppJingleIq::setResponder(const QString &responder)
{
    d->responder = responder;
}

///
/// Returns true if the call is ringing.
///
/// \deprecated This method is deprecated since QXmpp 1.5. Use \c QXmppJingleIq::rtpSessionState()
/// instead.
///
bool QXmppJingleIq::ringing() const
{
    if (d->rtpSessionState) {
        return std::holds_alternative<RtpSessionStateRinging>(*d->rtpSessionState);
    }

    return false;
}

///
/// Set to true if the call is ringing.
///
/// \param ringing
///
/// \deprecated This method is deprecated since QXmpp 1.5. Use
/// \c QXmppJingleIq::setRtpSessionState() instead.
///
void QXmppJingleIq::setRinging(bool ringing)
{
    if (ringing) {
        d->rtpSessionState = RtpSessionStateRinging();
    } else {
        d->rtpSessionState = std::nullopt;
    }
}

/// Returns the session ID.
QString QXmppJingleIq::sid() const
{
    return d->sid;
}

/// Sets the session ID.
void QXmppJingleIq::setSid(const QString &sid)
{
    d->sid = sid;
}

///
/// Returns the JID of the \xep{0272, Multiparty Jingle (Muji)} group chat.
///
/// \return the Muji group chat JID
///
/// \since QXmpp 1.5
///
QString QXmppJingleIq::mujiGroupChatJid() const
{
    return d->mujiGroupChatJid;
}

///
/// Sets the JID of the \xep{0272, Multiparty Jingle (Muji)} group chat.
///
/// \param mujiGroupChatJid Muji group chat JID
///
/// \since QXmpp 1.5
///
void QXmppJingleIq::setMujiGroupChatJid(const QString &mujiGroupChatJid)
{
    d->mujiGroupChatJid = mujiGroupChatJid;
}

///
/// Returns the state of an RTP session as specified by \xep{0167, Jingle RTP Sessions}
/// Informational Messages.
///
/// \return the session's state
///
/// \since QXmpp 1.5
///
std::optional<QXmppJingleIq::RtpSessionState> QXmppJingleIq::rtpSessionState() const
{
    return d->rtpSessionState;
}

///
/// Sets the state of an RTP session as specified by \xep{0167, Jingle RTP Sessions} Informational
/// Messages.
///
/// The appropriate action is set as well.
/// Thus, it is not needed to set it manually.
///
/// \param rtpSessionState session's state
///
/// \since QXmpp 1.5
///
void QXmppJingleIq::setRtpSessionState(const std::optional<RtpSessionState> &rtpSessionState)
{
    d->rtpSessionState = rtpSessionState;
    d->action = Action::SessionInfo;
}

/// \cond
bool QXmppJingleIq::isJingleIq(const QDomElement &element)
{
    QDomElement jingleElement = element.firstChildElement(u"jingle"_s);
    return (jingleElement.namespaceURI() == ns_jingle);
}

void QXmppJingleIq::parseElementFromChild(const QDomElement &element)
{
    QDomElement jingleElement = element.firstChildElement(u"jingle"_s);
    const auto action = jingleElement.attribute(u"action"_s).toStdString();
    for (int i = ContentAccept; i <= TransportReplace; i++) {
        if (action == jingle_actions[i]) {
            d->action = static_cast<Action>(i);
            break;
        }
    }
    d->initiator = jingleElement.attribute(u"initiator"_s);
    d->responder = jingleElement.attribute(u"responder"_s);
    d->sid = jingleElement.attribute(u"sid"_s);

    // XEP-0272: Multiparty Jingle (Muji)
    if (const auto mujiGroupChatElement = jingleElement.firstChildElement(u"muji"_s);
        mujiGroupChatElement.namespaceURI() == ns_muji) {
        d->mujiGroupChatJid = mujiGroupChatElement.attribute(u"room"_s);
    }

    // content
    d->contents.clear();
    for (const auto &contentElement : iterChildElements(jingleElement, u"content")) {
        Content content;
        content.parse(contentElement);
        addContent(content);
    }
    d->reason.parse(firstChildElement(jingleElement, u"reason"));

    for (const auto &childElement : iterChildElements(jingleElement, {}, ns_jingle_rtp_info)) {
        const auto elementTag = childElement.tagName();

        if (elementTag == u"active") {
            d->rtpSessionState = RtpSessionStateActive();
        } else if (elementTag == u"hold") {
            d->rtpSessionState = RtpSessionStateHold();
        } else if (elementTag == u"unhold") {
            d->rtpSessionState = RtpSessionStateUnhold();
        } else if (const auto isMute = elementTag == u"mute"; isMute || elementTag == u"unmute") {
            RtpSessionStateMuting muting;
            muting.isMute = isMute;

            if (const auto creator = childElement.attribute(u"creator"_s); creator == u"initiator") {
                muting.creator = Initiator;
            } else if (creator == u"responder") {
                muting.creator = Responder;
            }

            muting.name = childElement.attribute(u"name"_s);

            d->rtpSessionState = muting;
        } else if (elementTag == u"ringing") {
            d->rtpSessionState = RtpSessionStateRinging();
        }
    }
}

void QXmppJingleIq::toXmlElementFromChild(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("jingle"));
    writer->writeDefaultNamespace(toString65(ns_jingle));
    writeOptionalXmlAttribute(writer, u"action", QString::fromLocal8Bit(jingle_actions[d->action]));
    writeOptionalXmlAttribute(writer, u"initiator", d->initiator);
    writeOptionalXmlAttribute(writer, u"responder", d->responder);
    writeOptionalXmlAttribute(writer, u"sid", d->sid);

    // XEP-0272: Multiparty Jingle (Muji)
    if (!d->mujiGroupChatJid.isEmpty()) {
        writer->writeStartElement(QSL65("muji"));
        writer->writeDefaultNamespace(toString65(ns_muji));
        writeOptionalXmlAttribute(writer, u"room", d->mujiGroupChatJid);
        writer->writeEndElement();
    }

    for (const auto &content : d->contents) {
        content.toXml(writer);
    }

    d->reason.toXml(writer);

    const auto writeStartElementWithNamespace = [=](const QString &tagName) {
        writer->writeStartElement(tagName);
        writer->writeDefaultNamespace(toString65(ns_jingle_rtp_info));
    };

    if (d->rtpSessionState) {
        if (std::holds_alternative<RtpSessionStateActive>(*d->rtpSessionState)) {
            writeStartElementWithNamespace(u"active"_s);
        } else if (std::holds_alternative<RtpSessionStateHold>(*d->rtpSessionState)) {
            writeStartElementWithNamespace(u"hold"_s);
        } else if (std::holds_alternative<RtpSessionStateUnhold>(*d->rtpSessionState)) {
            writeStartElementWithNamespace(u"unhold"_s);
        } else if (auto rtpSessionStateMuting = std::get_if<RtpSessionStateMuting>(&(*d->rtpSessionState))) {
            if (rtpSessionStateMuting->isMute) {
                writeStartElementWithNamespace(u"mute"_s);
            } else {
                writeStartElementWithNamespace(u"unmute"_s);
            }

            if (rtpSessionStateMuting->creator == Initiator) {
                writeOptionalXmlAttribute(writer, u"creator", u"initiator"_s);
            } else if (rtpSessionStateMuting->creator == Responder) {
                writeOptionalXmlAttribute(writer, u"creator", u"responder"_s);
            }

            writeOptionalXmlAttribute(writer, u"name", rtpSessionStateMuting->name);
        } else {
            writeStartElementWithNamespace(u"ringing"_s);
        }

        writer->writeEndElement();
    }

    writer->writeEndElement();
}
/// \endcond

class QXmppJingleCandidatePrivate : public QSharedData
{
public:
    QXmppJingleCandidatePrivate();

    int component;
    QString foundation;
    int generation;
    QHostAddress host;
    QString id;
    int network;
    quint16 port;
    QString protocol;
    int priority;
    QXmppJingleCandidate::Type type;
};

QXmppJingleCandidatePrivate::QXmppJingleCandidatePrivate()
    : component(0), generation(0), network(0), port(0), priority(0), type(QXmppJingleCandidate::HostType)
{
}

///
/// Constructs an empty candidate.
///
QXmppJingleCandidate::QXmppJingleCandidate()
    : d(new QXmppJingleCandidatePrivate())
{
}

/// Copy-constructor.
QXmppJingleCandidate::QXmppJingleCandidate(const QXmppJingleCandidate &other) = default;
/// Move-constructor.
QXmppJingleCandidate::QXmppJingleCandidate(QXmppJingleCandidate &&) = default;
QXmppJingleCandidate::~QXmppJingleCandidate() = default;
/// Assignment operator.
QXmppJingleCandidate &QXmppJingleCandidate::operator=(const QXmppJingleCandidate &other) = default;
/// Move-assignment operator.
QXmppJingleCandidate &QXmppJingleCandidate::operator=(QXmppJingleCandidate &&) = default;

/// Returns the candidate's component ID.
int QXmppJingleCandidate::component() const
{
    return d->component;
}

/// Sets the candidates's component ID.
void QXmppJingleCandidate::setComponent(int component)
{
    d->component = component;
}

///
/// Returns the candidate's foundation.
///
/// \since QXmpp 0.9
///
QString QXmppJingleCandidate::foundation() const
{
    return d->foundation;
}

///
/// Sets the candidate's foundation.
///
/// \param foundation
///
/// \since QXmpp 0.9
///
void QXmppJingleCandidate::setFoundation(const QString &foundation)
{
    d->foundation = foundation;
}

///
/// Returns the candidate's generation.
///
/// \since QXmpp 0.9
///
int QXmppJingleCandidate::generation() const
{
    return d->generation;
}

///
/// Sets the candidate's generation.
///
/// \param generation
///
/// \since QXmpp 0.9
///
void QXmppJingleCandidate::setGeneration(int generation)
{
    d->generation = generation;
}

/// Returns the candidate's host address.
///

QHostAddress QXmppJingleCandidate::host() const
{
    return d->host;
}

/// Sets the candidate's host address.
void QXmppJingleCandidate::setHost(const QHostAddress &host)
{
    d->host = host;
}

/// Returns the candidate's unique identifier.
QString QXmppJingleCandidate::id() const
{
    return d->id;
}

/// Sets the candidate's unique identifier.
void QXmppJingleCandidate::setId(const QString &id)
{
    d->id = id;
}

/// Returns the network index (starting at 0) the candidate is on.
int QXmppJingleCandidate::network() const
{
    return d->network;
}

/// Sets the network index (starting at 0) the candidate is on.
void QXmppJingleCandidate::setNetwork(int network)
{
    d->network = network;
}

/// Returns the candidate's port number.
quint16 QXmppJingleCandidate::port() const
{
    return d->port;
}

/// Sets the candidate's port number.
void QXmppJingleCandidate::setPort(quint16 port)
{
    d->port = port;
}

/// Returns the candidate's priority.
int QXmppJingleCandidate::priority() const
{
    return d->priority;
}

/// Sets the candidate's priority.
void QXmppJingleCandidate::setPriority(int priority)
{
    d->priority = priority;
}

/// Returns the candidate's protocol (e.g. "udp").
QString QXmppJingleCandidate::protocol() const
{
    return d->protocol;
}

/// Sets the candidate's protocol (e.g. "udp").
void QXmppJingleCandidate::setProtocol(const QString &protocol)
{
    d->protocol = protocol;
}

/// Returns the candidate type (e.g. "host").
QXmppJingleCandidate::Type QXmppJingleCandidate::type() const
{
    return d->type;
}

/// Sets the candidate type (e.g. "host").
void QXmppJingleCandidate::setType(QXmppJingleCandidate::Type type)
{
    d->type = type;
}

/// Returns true if the host address or port are empty.
bool QXmppJingleCandidate::isNull() const
{
    return d->host.isNull() || !d->port;
}

/// \cond
void QXmppJingleCandidate::parse(const QDomElement &element)
{
    d->component = element.attribute(u"component"_s).toInt();
    d->foundation = element.attribute(u"foundation"_s);
    d->generation = element.attribute(u"generation"_s).toInt();
    d->host = QHostAddress(element.attribute(u"ip"_s));
    d->id = element.attribute(u"id"_s);
    d->network = element.attribute(u"network"_s).toInt();
    d->port = element.attribute(u"port"_s).toInt();
    d->priority = element.attribute(u"priority"_s).toInt();
    d->protocol = element.attribute(u"protocol"_s);
    d->type = typeFromString(element.attribute(u"type"_s));
}

void QXmppJingleCandidate::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("candidate"));
    writeOptionalXmlAttribute(writer, u"component", QString::number(d->component));
    writeOptionalXmlAttribute(writer, u"foundation", d->foundation);
    writeOptionalXmlAttribute(writer, u"generation", QString::number(d->generation));
    writeOptionalXmlAttribute(writer, u"id", d->id);
    writeOptionalXmlAttribute(writer, u"ip", d->host.toString());
    writeOptionalXmlAttribute(writer, u"network", QString::number(d->network));
    writeOptionalXmlAttribute(writer, u"port", QString::number(d->port));
    writeOptionalXmlAttribute(writer, u"priority", QString::number(d->priority));
    writeOptionalXmlAttribute(writer, u"protocol", d->protocol);
    writeOptionalXmlAttribute(writer, u"type", typeToString(d->type));
    writer->writeEndElement();
}

QXmppJingleCandidate::Type QXmppJingleCandidate::typeFromString(const QString &typeStr, bool *ok)
{
    QXmppJingleCandidate::Type type;
    if (typeStr == u"host") {
        type = HostType;
    } else if (typeStr == u"prflx") {
        type = PeerReflexiveType;
    } else if (typeStr == u"srflx") {
        type = ServerReflexiveType;
    } else if (typeStr == u"relay") {
        type = RelayedType;
    } else {
        qWarning() << "Unknown candidate type" << typeStr;
        if (ok) {
            *ok = false;
        }
        return HostType;
    }
    if (ok) {
        *ok = true;
    }
    return type;
}

QString QXmppJingleCandidate::typeToString(QXmppJingleCandidate::Type type)
{
    QString typeStr;
    switch (type) {
    case HostType:
        typeStr = u"host"_s;
        break;
    case PeerReflexiveType:
        typeStr = u"prflx"_s;
        break;
    case ServerReflexiveType:
        typeStr = u"srflx"_s;
        break;
    case RelayedType:
        typeStr = u"relay"_s;
        break;
    }
    return typeStr;
}
/// \endcond

class QXmppJinglePayloadTypePrivate : public QSharedData
{
public:
    QXmppJinglePayloadTypePrivate();

    unsigned char channels;
    unsigned int clockrate;
    unsigned char id;
    unsigned int maxptime;
    QString name;
    QMap<QString, QString> parameters;
    unsigned int ptime;

    // XEP-0293: Jingle RTP Feedback Negotiation
    QVector<QXmppJingleRtpFeedbackProperty> rtpFeedbackProperties;
    QVector<QXmppJingleRtpFeedbackInterval> rtpFeedbackIntervals;
};

QXmppJinglePayloadTypePrivate::QXmppJinglePayloadTypePrivate()
    : channels(1), clockrate(0), id(0), maxptime(0), ptime(0)
{
}

QXmppJinglePayloadType::QXmppJinglePayloadType()
    : d(new QXmppJinglePayloadTypePrivate())
{
}

/// Constructs a copy of other.
QXmppJinglePayloadType::QXmppJinglePayloadType(const QXmppJinglePayloadType &other)
    : d(other.d)
{
}

QXmppJinglePayloadType::~QXmppJinglePayloadType()
{
}

/// Returns the number of channels (e.g. 1 for mono, 2 for stereo).
unsigned char QXmppJinglePayloadType::channels() const
{
    return d->channels;
}

/// Sets the number of channels (e.g. 1 for mono, 2 for stereo).
void QXmppJinglePayloadType::setChannels(unsigned char channels)
{
    d->channels = channels;
}

/// Returns the clockrate in Hz, i.e. the number of samples per second.
unsigned int QXmppJinglePayloadType::clockrate() const
{
    return d->clockrate;
}

/// Sets the clockrate in Hz, i.e. the number of samples per second.
void QXmppJinglePayloadType::setClockrate(unsigned int clockrate)
{
    d->clockrate = clockrate;
}

/// Returns the payload type identifier.
unsigned char QXmppJinglePayloadType::id() const
{
    return d->id;
}

/// Sets the payload type identifier.
void QXmppJinglePayloadType::setId(unsigned char id)
{
    Q_ASSERT(id <= 127);
    d->id = id;
}

/// Returns the maximum packet time in milliseconds.
unsigned int QXmppJinglePayloadType::maxptime() const
{
    return d->maxptime;
}

/// Sets the maximum packet type in milliseconds.
void QXmppJinglePayloadType::setMaxptime(unsigned int maxptime)
{
    d->maxptime = maxptime;
}

/// Returns the payload type name.
QString QXmppJinglePayloadType::name() const
{
    return d->name;
}

/// Sets the payload type name.
void QXmppJinglePayloadType::setName(const QString &name)
{
    d->name = name;
}

/// Returns the payload parameters.
QMap<QString, QString> QXmppJinglePayloadType::parameters() const
{
    return d->parameters;
}

/// Sets the payload parameters.
void QXmppJinglePayloadType::setParameters(const QMap<QString, QString> &parameters)
{
    d->parameters = parameters;
}

/// Returns the packet time in milliseconds (20 by default).
unsigned int QXmppJinglePayloadType::ptime() const
{
    return d->ptime ? d->ptime : 20;
}

/// Sets the packet time in milliseconds (20 by default).
void QXmppJinglePayloadType::setPtime(unsigned int ptime)
{
    d->ptime = ptime;
}

///
/// Returns the properties of RTP feedback.
///
/// \return the RTP feedback properties
///
/// \since QXmpp 1.5
///
QVector<QXmppJingleRtpFeedbackProperty> QXmppJinglePayloadType::rtpFeedbackProperties() const
{
    return d->rtpFeedbackProperties;
}

///
/// Sets the properties of RTP feedback.
///
/// \param rtpFeedbackProperties RTP feedback properties
///
/// \since QXmpp 1.5
///
void QXmppJinglePayloadType::setRtpFeedbackProperties(const QVector<QXmppJingleRtpFeedbackProperty> &rtpFeedbackProperties)
{
    d->rtpFeedbackProperties = rtpFeedbackProperties;
}

///
/// Returns the intervals of RTP feedback.
///
/// \return the RTP feedback intervals
///
QVector<QXmppJingleRtpFeedbackInterval> QXmppJinglePayloadType::rtpFeedbackIntervals() const
{
    return d->rtpFeedbackIntervals;
}

///
/// Sets the intervals of RTP feedback.
///
/// \param rtpFeedbackIntervals RTP feedback intervals
///
void QXmppJinglePayloadType::setRtpFeedbackIntervals(const QVector<QXmppJingleRtpFeedbackInterval> &rtpFeedbackIntervals)
{
    d->rtpFeedbackIntervals = rtpFeedbackIntervals;
}

/// \cond
void QXmppJinglePayloadType::parse(const QDomElement &element)
{
    d->id = parseInt<uint8_t>(element.attribute(u"id"_s)).value_or(0);
    d->name = element.attribute(u"name"_s);
    d->channels = parseInt<uint8_t>(element.attribute(u"channels"_s)).value_or(1);
    d->clockrate = element.attribute(u"clockrate"_s).toUInt();
    d->maxptime = element.attribute(u"maxptime"_s).toUInt();
    d->ptime = element.attribute(u"ptime"_s).toUInt();

    for (const auto &child : iterChildElements(element, u"parameter")) {
        d->parameters.insert(child.attribute(u"name"_s), child.attribute(u"value"_s));
    }

    parseJingleRtpFeedbackNegotiationElements(element, d->rtpFeedbackProperties, d->rtpFeedbackIntervals);
}

void QXmppJinglePayloadType::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("payload-type"));
    writeOptionalXmlAttribute(writer, u"id", QString::number(d->id));
    writeOptionalXmlAttribute(writer, u"name", d->name);
    if (d->channels > 1) {
        writeOptionalXmlAttribute(writer, u"channels", QString::number(d->channels));
    }
    if (d->clockrate > 0) {
        writeOptionalXmlAttribute(writer, u"clockrate", QString::number(d->clockrate));
    }
    if (d->maxptime > 0) {
        writeOptionalXmlAttribute(writer, u"maxptime", QString::number(d->maxptime));
    }
    if (d->ptime > 0) {
        writeOptionalXmlAttribute(writer, u"ptime", QString::number(d->ptime));
    }

    for (auto itr = d->parameters.begin(); itr != d->parameters.end(); itr++) {
        writer->writeStartElement(QSL65("parameter"));
        writer->writeAttribute(QSL65("name"), itr.key());
        writer->writeAttribute(QSL65("value"), itr.value());
        writer->writeEndElement();
    }

    jingleRtpFeedbackNegotiationElementsToXml(writer, d->rtpFeedbackProperties, d->rtpFeedbackIntervals);

    writer->writeEndElement();
}
/// \endcond

/// Assigns the other payload type to this one.
///
/// \param other

QXmppJinglePayloadType &QXmppJinglePayloadType::operator=(const QXmppJinglePayloadType &other)
{
    d = other.d;
    return *this;
}

/// Returns true if this QXmppJinglePayloadType and \a other refer to the same payload type.
///
/// \param other

bool QXmppJinglePayloadType::operator==(const QXmppJinglePayloadType &other) const
{
    // FIXME : what to do with m_ptime and m_maxptime?
    if (d->id <= 95) {
        return other.d->id == d->id && other.d->clockrate == d->clockrate;
    } else {
        return other.d->channels == d->channels &&
            other.d->clockrate == d->clockrate &&
            other.d->name.toLower() == d->name.toLower();
    }
}

class QXmppJingleDescriptionPrivate : public QSharedData
{
public:
    QXmppJingleDescriptionPrivate() = default;

    QString media;
    quint32 ssrc;
    QString type;
    QList<QXmppJinglePayloadType> payloadTypes;
};

///
/// \class QXmppJingleDescription
///
/// \brief The QXmppJingleDescription class represents descriptions for Jingle elements including
/// media type, streaming source, namespace and payload types.
///
/// \since QXmpp 1.6
///

QXmppJingleDescription::QXmppJingleDescription()
    : d(new QXmppJingleDescriptionPrivate())
{
}

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppJingleDescription)

///
/// Returns the media type.
///
QString QXmppJingleDescription::media() const
{
    return d->media;
}

///
/// Sets the media type.
///
void QXmppJingleDescription::setMedia(const QString &media)
{
    d->media = media;
}

///
/// Returns the streaming source.
///
quint32 QXmppJingleDescription::ssrc() const
{
    return d->ssrc;
}

///
/// Sets the streaming source.
///
void QXmppJingleDescription::setSsrc(quint32 ssrc)
{
    d->ssrc = ssrc;
}

///
/// Returns the description namespace.
///
QString QXmppJingleDescription::type() const
{
    return d->type;
}

///
/// Sets the description namespace.
///
void QXmppJingleDescription::setType(const QString &type)
{
    d->type = type;
}

///
/// Adds a payload type to the list of payload types.
///
void QXmppJingleDescription::addPayloadType(const QXmppJinglePayloadType &payload)
{
    d->type = ns_jingle_rtp.toString();
    d->payloadTypes.append(payload);
}

///
/// Returns a list of payload types.
///
const QList<QXmppJinglePayloadType> &QXmppJingleDescription::payloadTypes() const
{
    return d->payloadTypes;
}

///
/// Sets the list of payload types.
///
void QXmppJingleDescription::setPayloadTypes(const QList<QXmppJinglePayloadType> &payloadTypes)
{
    d->type = payloadTypes.isEmpty() ? QString() : ns_jingle_rtp.toString();
    d->payloadTypes = payloadTypes;
}

/// \cond
void QXmppJingleDescription::parse(const QDomElement &element)
{
    d->type = element.namespaceURI();
    d->media = element.attribute(u"media"_s);
    d->ssrc = element.attribute(u"ssrc"_s).toULong();

    for (const auto &child : iterChildElements(element, u"payload-type")) {
        QXmppJinglePayloadType payload;
        payload.parse(child);
        d->payloadTypes.append(payload);
    }
}

void QXmppJingleDescription::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("description"));
    writer->writeDefaultNamespace(d->type);

    writeOptionalXmlAttribute(writer, u"media", d->media);

    if (d->ssrc) {
        writer->writeAttribute(QSL65("ssrc"), QString::number(d->ssrc));
    }

    for (const auto &payloadType : d->payloadTypes) {
        payloadType.toXml(writer);
    }

    writer->writeEndElement();
}
/// \endcond

class QXmppSdpParameterPrivate : public QSharedData
{
public:
    QString name;
    QString value;
};

///
/// \class QXmppSdpParameter
///
/// \brief The QXmppSdpParameter class represents a Session Description Protocol (SDP) parameter
/// specified by RFC 4566 and used by several XEPs based on \xep{0166, Jingle}.
///
/// \since QXmpp 1.5
///

///
/// Constructs a Session Description Protocol parameter.
///
QXmppSdpParameter::QXmppSdpParameter()
    : d(new QXmppSdpParameterPrivate())
{
}

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppSdpParameter)

///
/// Returns the name of the parameter.
///
/// \return the parameter's name
///
QString QXmppSdpParameter::name() const
{
    return d->name;
}

///
/// Sets the name of the parameter.
///
/// \param name parameter's name
///
void QXmppSdpParameter::setName(const QString &name)
{
    d->name = name;
}

///
/// Returns the value of the parameter.
///
/// \return the parameter's value
///
QString QXmppSdpParameter::value() const
{
    return d->value;
}

///
/// Sets the value of the parameter.
///
/// A parameter in the form "a=b" can be created by this method.
/// Any other form of parameters can be created by not using this method.
/// The value stays a default-constructed QString then.
///
/// \param value parameter's value
///
void QXmppSdpParameter::setValue(const QString &value)
{
    d->value = value;
}

/// \cond
void QXmppSdpParameter::parse(const QDomElement &element)
{
    d->name = element.attribute(u"name"_s);
    d->value = element.attribute(u"value"_s);
}

void QXmppSdpParameter::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("parameter"));
    writeOptionalXmlAttribute(writer, u"name", d->name);

    if (!d->value.isEmpty()) {
        writeOptionalXmlAttribute(writer, u"value", d->value);
    }

    writer->writeEndElement();
}
/// \endcond

///
/// Determines whether the given DOM element is a Session Description Protocol parameter element.
///
/// \param element DOM element being checked
///
/// \return whether element is a Session Description Protocol parameter element
///
bool QXmppSdpParameter::isSdpParameter(const QDomElement &element)
{
    return element.tagName() == u"parameter";
}

class QXmppJingleRtpCryptoElementPrivate : public QSharedData
{
public:
    uint32_t tag = 0;
    QString cryptoSuite;
    QString keyParams;
    QString sessionParams;
};

///
/// \class QXmppJingleRtpCryptoElement
///
/// \brief The QXmppJingleRtpCryptoElement class represents the \xep{0167: Jingle RTP Sessions}
/// "crypto" element used for SRTP negotiation.
///
/// \since QXmpp 1.5
///

///
/// Constructs a Jingle RTP crypto element.
///
QXmppJingleRtpCryptoElement::QXmppJingleRtpCryptoElement()
    : d(new QXmppJingleRtpCryptoElementPrivate())
{
}

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppJingleRtpCryptoElement)

///
/// Returns the tag used as an identifier for the crypto element.
///
/// \return the identifying tag
///
uint32_t QXmppJingleRtpCryptoElement::tag() const
{
    return d->tag;
}

///
/// Sets the tag used as an identifier for the crypto element.
///
/// \param tag identifying tag
///
void QXmppJingleRtpCryptoElement::setTag(uint32_t tag)
{
    d->tag = tag;
}

///
/// Returns the crypto suite used as an identifier for describing the encryption and authentication
/// algorithms.
///
/// \return the identifying crypto suite
///
QString QXmppJingleRtpCryptoElement::cryptoSuite() const
{
    return d->cryptoSuite;
}

///
/// Sets the crypto suite used as an identifier for describing the encryption and authentication
/// algorithms.
///
/// \param cryptoSuite identifying crypto suite
///
void QXmppJingleRtpCryptoElement::setCryptoSuite(const QString &cryptoSuite)
{
    d->cryptoSuite = cryptoSuite;
}

///
/// Returns the key parameters providing one or more sets of keying material for the crypto suite.
///
/// \return the key parameters providing one or more sets of keying material
///
QString QXmppJingleRtpCryptoElement::keyParams() const
{
    return d->keyParams;
}

///
/// Sets the key parameters providing one or more sets of keying material for the crypto suite.
///
/// \param keyParams key parameters providing one or more sets of keying material
///
void QXmppJingleRtpCryptoElement::setKeyParams(const QString &keyParams)
{
    d->keyParams = keyParams;
}

///
/// Returns the session parameters providing transport-specific data.
///
/// \return the session parameters providing transport-specific data
///
QString QXmppJingleRtpCryptoElement::sessionParams() const
{
    return d->sessionParams;
}

///
/// Sets the session parameters providing transport-specific data.
///
/// \param sessionParams session parameters providing transport-specific data
///
void QXmppJingleRtpCryptoElement::setSessionParams(const QString &sessionParams)
{
    d->sessionParams = sessionParams;
}

/// \cond
void QXmppJingleRtpCryptoElement::parse(const QDomElement &element)
{
    d->tag = element.attribute(u"tag"_s).toUInt();
    d->cryptoSuite = element.attribute(u"crypto-suite"_s);
    d->keyParams = element.attribute(u"key-params"_s);
    d->sessionParams = element.attribute(u"session-params"_s);
}

void QXmppJingleRtpCryptoElement::toXml(QXmlStreamWriter *writer) const
{
    if (!d->cryptoSuite.isEmpty() && !d->keyParams.isEmpty()) {
        writer->writeStartElement(QSL65("crypto"));
        writer->writeAttribute(QSL65("tag"), QString::number(d->tag));
        writer->writeAttribute(QSL65("crypto-suite"), d->cryptoSuite);
        writer->writeAttribute(QSL65("key-params"), d->keyParams);
        writeOptionalXmlAttribute(writer, u"session-params", d->sessionParams);
        writer->writeEndElement();
    }
}
/// \endcond

///
/// Determines whether the given DOM element is an RTP crypto element.
///
/// \param element DOM element being checked
///
/// \return whether element is an RTP crypto element
///
bool QXmppJingleRtpCryptoElement::isJingleRtpCryptoElement(const QDomElement &element)
{
    return element.tagName() == u"crypto";
}

class QXmppJingleRtpEncryptionPrivate : public QSharedData
{
public:
    bool isRequired = false;
    QVector<QXmppJingleRtpCryptoElement> cryptoElements;
};

///
/// \class QXmppJingleRtpEncryption
///
/// \brief The QXmppJingleRtpEncryption class represents the \xep{0167: Jingle RTP Sessions}
/// "encryption" element used for SRTP negotiation.
///
/// \since QXmpp 1.5
///

///
/// Constructs a Jingle RTP encryption.
///
QXmppJingleRtpEncryption::QXmppJingleRtpEncryption()
    : d(new QXmppJingleRtpEncryptionPrivate())
{
}

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppJingleRtpEncryption)

///
/// Returns whether encryption via SRTP is required.
///
/// \return whether encryption is required
///
bool QXmppJingleRtpEncryption::isRequired() const
{
    return d->isRequired;
}

///
/// Sets whether encryption via SRTP is required.
///
/// \param isRequired whether encryption is required
///
void QXmppJingleRtpEncryption::setRequired(bool isRequired)
{
    d->isRequired = isRequired;
}

///
/// Returns the crypto elements used for encryption via SRTP.
///
/// \return the crypto elements
///
QVector<QXmppJingleRtpCryptoElement> QXmppJingleRtpEncryption::cryptoElements() const
{
    return d->cryptoElements;
}

///
/// Sets the crypto elements used for encryption via SRTP.
///
/// \param cryptoElements the crypto elements
///
void QXmppJingleRtpEncryption::setCryptoElements(const QVector<QXmppJingleRtpCryptoElement> &cryptoElements)
{
    d->cryptoElements = cryptoElements;
}

/// \cond
void QXmppJingleRtpEncryption::parse(const QDomElement &element)
{
    d->isRequired = element.attribute(u"required"_s) == u"true" ||
        element.attribute(u"required"_s) == u"1";

    for (const auto &childElement : iterChildElements(element)) {
        if (QXmppJingleRtpCryptoElement::isJingleRtpCryptoElement(childElement)) {
            QXmppJingleRtpCryptoElement cryptoElement;
            cryptoElement.parse(childElement);
            d->cryptoElements.append(std::move(cryptoElement));
        }
    }
}

void QXmppJingleRtpEncryption::toXml(QXmlStreamWriter *writer) const
{
    if (!d->cryptoElements.isEmpty()) {
        writer->writeStartElement(QSL65("encryption"));
        writer->writeDefaultNamespace(toString65(ns_jingle_rtp));

        if (d->isRequired) {
            writer->writeAttribute(QSL65("required"), u"1"_s);
        }

        for (const auto &cryptoElement : std::as_const(d->cryptoElements)) {
            cryptoElement.toXml(writer);
        }

        writer->writeEndElement();
    }
}
/// \endcond

///
/// Determines whether the given DOM element is an RTP encryption element.
///
/// \param element DOM element being checked
///
/// \return whether element is an RTP encryption element
///
bool QXmppJingleRtpEncryption::isJingleRtpEncryption(const QDomElement &element)
{
    return element.tagName() == u"encryption" &&
        element.namespaceURI() == ns_jingle_rtp;
}

class QXmppJingleRtpFeedbackPropertyPrivate : public QSharedData
{
public:
    QString type;
    QString subtype;
    QVector<QXmppSdpParameter> parameters;
};

///
/// \class QXmppJingleRtpFeedbackProperty
///
/// \brief The QXmppJingleRtpFeedbackProperty class represents the
/// \xep{0293, Jingle RTP Feedback Negotiation} "rtcp-fb" element.
///
/// \since QXmpp 1.5
///

///
/// Constructs a Jingle RTP feedback property.
///
QXmppJingleRtpFeedbackProperty::QXmppJingleRtpFeedbackProperty()
    : d(new QXmppJingleRtpFeedbackPropertyPrivate())
{
}

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppJingleRtpFeedbackProperty)

///
/// Returns the type of RTP feedback.
///
/// \return the RTP feedback type
///
QString QXmppJingleRtpFeedbackProperty::type() const
{
    return d->type;
}

///
/// Sets the type of RTP feedback.
///
/// \param type RTP feedback type
///
void QXmppJingleRtpFeedbackProperty::setType(const QString &type)
{
    d->type = type;
}

///
/// Returns the subtype for RTP feedback.
///
/// \return the RTP feedback subtype
///
QString QXmppJingleRtpFeedbackProperty::subtype() const
{
    return d->subtype;
}

///
/// Sets the subtype of RTP feedback.
///
/// If there is more than one parameter, use QXmppJingleRtpFeedbackProperty::setParameters()
/// instead of this method.
///
/// \param subtype RTP feedback subtype
///
void QXmppJingleRtpFeedbackProperty::setSubtype(const QString &subtype)
{
    d->subtype = subtype;
}

///
/// Returns the parameters of RTP feedback.
///
/// \return the RTP feedback parameters
///
QVector<QXmppSdpParameter> QXmppJingleRtpFeedbackProperty::parameters() const
{
    return d->parameters;
}

///
/// Sets the parameters of RTP feedback.
///
/// Additional parameters can be set by this method.
/// If there is only one parameter, use QXmppJingleRtpFeedbackProperty::setSubtype()
/// instead of this method.
///
/// \param parameters RTP feedback parameters
///
void QXmppJingleRtpFeedbackProperty::setParameters(const QVector<QXmppSdpParameter> &parameters)
{
    d->parameters = parameters;
}

/// \cond
void QXmppJingleRtpFeedbackProperty::parse(const QDomElement &element)
{
    d->type = element.attribute(u"type"_s);
    d->subtype = element.attribute(u"subtype"_s);
    parseSdpParameters(element, d->parameters);
}

void QXmppJingleRtpFeedbackProperty::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("rtcp-fb"));
    writer->writeDefaultNamespace(toString65(ns_jingle_rtp_feedback_negotiation));
    writeOptionalXmlAttribute(writer, u"type", d->type);

    // If there are parameters, they must be used instead of the subtype.
    if (d->subtype.isEmpty()) {
        sdpParametersToXml(writer, d->parameters);
    } else {
        writeOptionalXmlAttribute(writer, u"subtype", d->subtype);
    }

    writer->writeEndElement();
}
/// \endcond

///
/// Determines whether the given DOM element is an RTP feedback property element.
///
/// \param element DOM element being checked
///
/// \return whether element is an RTP feedback property element
///
bool QXmppJingleRtpFeedbackProperty::isJingleRtpFeedbackProperty(const QDomElement &element)
{
    return element.tagName() == u"rtcp-fb" &&
        element.namespaceURI() == ns_jingle_rtp_feedback_negotiation;
}

///
/// \class QXmppJingleRtpFeedbackInterval
///
/// \brief The QXmppJingleRtpFeedbackInterval class represents the
/// \xep{0293, Jingle RTP Feedback Negotiation} "rtcp-fb-trr-int" element.
///
/// \since QXmpp 1.5
///

///
/// Constructs a Jingle RTP feedback interval.
///
QXmppJingleRtpFeedbackInterval::QXmppJingleRtpFeedbackInterval()
{
}

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppJingleRtpFeedbackInterval)

///
/// Returns the value of the RTP feedback interval.
///
/// \return the RTP feedback interval value
///
uint64_t QXmppJingleRtpFeedbackInterval::value() const
{
    return m_value;
}

///
/// Sets the value of the RTP feedback interval.
///
/// \param value RTP feedback interval value
///
void QXmppJingleRtpFeedbackInterval::setValue(uint64_t value)
{
    m_value = value;
}

/// \cond
void QXmppJingleRtpFeedbackInterval::parse(const QDomElement &element)
{
    m_value = element.attribute(u"value"_s).toUInt();
}

void QXmppJingleRtpFeedbackInterval::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("rtcp-fb-trr-int"));
    writer->writeDefaultNamespace(toString65(ns_jingle_rtp_feedback_negotiation));
    writeOptionalXmlAttribute(writer, u"value", QString::number(m_value));
    writer->writeEndElement();
}
/// \endcond

///
/// Determines whether the given DOM element is an RTP feedback interval element.
///
/// \param element DOM element being checked
///
/// \return whether element is an RTP feedback interval element
///
bool QXmppJingleRtpFeedbackInterval::isJingleRtpFeedbackInterval(const QDomElement &element)
{
    return element.tagName() == u"rtcp-fb-trr-int" &&
        element.namespaceURI() == ns_jingle_rtp_feedback_negotiation;
}

class QXmppJingleRtpHeaderExtensionPropertyPrivate : public QSharedData
{
public:
    uint32_t id = 0;
    QString uri;
    QXmppJingleRtpHeaderExtensionProperty::Senders senders = QXmppJingleRtpHeaderExtensionProperty::Both;
    QVector<QXmppSdpParameter> parameters;
};

///
/// \enum QXmppJingleRtpHeaderExtensionProperty::Senders
///
/// Parties that are allowed to send the negotiated RTP header extension
///

///
/// \class QXmppJingleRtpHeaderExtensionProperty
///
/// \brief The QXmppJingleRtpHeaderExtensionProperty class represents the
/// \xep{0294, Jingle RTP Header Extensions Negotiation} "rtp-hdrext" element.
///
/// \since QXmpp 1.5
///

///
/// Constructs a Jingle RTP header extension property.
///
QXmppJingleRtpHeaderExtensionProperty::QXmppJingleRtpHeaderExtensionProperty()
    : d(new QXmppJingleRtpHeaderExtensionPropertyPrivate())
{
}

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppJingleRtpHeaderExtensionProperty)

///
/// Returns the ID of the RTP header extension.
///
/// The ID is 0 if it is unset.
///
/// \return the RTP header extension's ID
///
uint32_t QXmppJingleRtpHeaderExtensionProperty::id() const
{
    return d->id;
}

///
/// Sets the ID of the RTP header extension.
///
/// The ID must either be at least 1 and at most 256 or at least 4096 and at most 4351.
///
/// \param id RTP header extension's ID
///
void QXmppJingleRtpHeaderExtensionProperty::setId(uint32_t id)
{
    d->id = id;
}

///
/// Returns the URI defning the RTP header extension.
///
/// \return the RTP header extension's URI
///
QString QXmppJingleRtpHeaderExtensionProperty::uri() const
{
    return d->uri;
}

///
/// Sets the URI defning the RTP header extension.
///
/// \param uri RTP header extension's URI
///
void QXmppJingleRtpHeaderExtensionProperty::setUri(const QString &uri)
{
    d->uri = uri;
}

///
/// Returns the parties that are allowed to send the negotiated RTP header extensions.
///
/// \return the parties that are allowed to send the RTP header extensions
///
QXmppJingleRtpHeaderExtensionProperty::Senders QXmppJingleRtpHeaderExtensionProperty::senders() const
{
    return d->senders;
}

///
/// Sets the parties that are allowed to send the negotiated RTP header extensions.
///
/// \param senders parties that are allowed to send the RTP header extensions
///
void QXmppJingleRtpHeaderExtensionProperty::setSenders(Senders senders)
{
    d->senders = senders;
}

///
/// Returns the parameters of the RTP header extension.
///
/// \return the RTP header extension's parameters
///
QVector<QXmppSdpParameter> QXmppJingleRtpHeaderExtensionProperty::parameters() const
{
    return d->parameters;
}

///
/// Sets the parameters of the RTP header extension.
///
/// Additional parameters can be set by this method.
///
/// \param parameters RTP header extension's parameters
///
void QXmppJingleRtpHeaderExtensionProperty::setParameters(const QVector<QXmppSdpParameter> &parameters)
{
    d->parameters = parameters;
}

/// \cond
void QXmppJingleRtpHeaderExtensionProperty::parse(const QDomElement &element)
{
    if (element.tagName() == u"rtp-hdrext" && element.namespaceURI() == ns_jingle_rtp_header_extensions_negotiation) {
        d->id = element.attribute(u"id"_s).toUInt();
        d->uri = element.attribute(u"uri"_s);
        d->senders = enumFromString<Senders>(JINGLE_RTP_HEADER_EXTENSIONS_SENDERS, element.attribute(u"senders"_s))
                         .value_or(Both);

        parseSdpParameters(element, d->parameters);
    }
}

void QXmppJingleRtpHeaderExtensionProperty::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("rtp-hdrext"));
    writer->writeDefaultNamespace(toString65(ns_jingle_rtp_header_extensions_negotiation));
    writeOptionalXmlAttribute(writer, u"id", QString::number(d->id));
    writeOptionalXmlAttribute(writer, u"uri", d->uri);

    if (d->senders != QXmppJingleRtpHeaderExtensionProperty::Both) {
        writeOptionalXmlAttribute(writer, u"senders", JINGLE_RTP_HEADER_EXTENSIONS_SENDERS.at(d->senders));
    }

    sdpParametersToXml(writer, d->parameters);

    writer->writeEndElement();
}
/// \endcond

///
/// Determines whether the given DOM element is an RTP header extensions property element.
///
/// \param element DOM element being checked
///
/// \return whether element is an RTP header extension property element
///
bool QXmppJingleRtpHeaderExtensionProperty::isJingleRtpHeaderExtensionProperty(const QDomElement &element)
{
    return element.tagName() == u"rtp-hdrext" &&
        element.namespaceURI() == ns_jingle_rtp_header_extensions_negotiation;
}

class QXmppJingleMessageInitiationElementPrivate : public QSharedData
{
public:
    QXmppJingleMessageInitiationElementPrivate() = default;

    QXmppJingleMessageInitiationElement::Type type { QXmppJingleMessageInitiationElement::Type::None };
    QString id;

    std::optional<QXmppJingleDescription> description;
    std::optional<QXmppJingleReason> reason;
    QString migratedTo;

    bool containsTieBreak;
};

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppJingleReason)

///
/// \enum QXmppJingleMessageInitiationElement::Type
///
/// Possible types of Jingle Message Initiation elements
///

///
/// \class QXmppJingleMessageInitiationElement
///
/// \brief The QXmppJingleMessageInitiationElement class represents a Jingle Message Initiation
/// element as specified by \xep{0353}: Jingle Message Initiation.
///
/// \ingroup Stanzas
///
/// \since QXmpp 1.6
///

///
/// \brief Constructs a Jingle Message Initiation element.
///
QXmppJingleMessageInitiationElement::QXmppJingleMessageInitiationElement()
    : d(new QXmppJingleMessageInitiationElementPrivate())
{
}

///
/// Returns the Jingle Message Initiation element type
///
QXmppJingleMessageInitiationElement::Type QXmppJingleMessageInitiationElement::type() const
{
    return d->type;
}

///
/// Sets the Jingle Message Initiation element type.
///
void QXmppJingleMessageInitiationElement::setType(Type type)
{
    d->type = type;
}

///
/// Returns the Jingle Message Initiation element id.
///
QString QXmppJingleMessageInitiationElement::id() const
{
    return d->id;
}

///
/// Sets the Jingle Message Initiation element id.
///
void QXmppJingleMessageInitiationElement::setId(const QString &id)
{
    d->id = id;
}

///
/// Returns the Jingle Message Initiation element description.
///
std::optional<QXmppJingleDescription> QXmppJingleMessageInitiationElement::description() const
{
    return d->description;
}

///
/// Sets the Jingle Message Initiation element description.
///
void QXmppJingleMessageInitiationElement::setDescription(std::optional<QXmppJingleDescription> description)
{
    d->description = description;
}

///
/// Returns the Jingle Message Initiation element reason.
///
std::optional<QXmppJingleReason> QXmppJingleMessageInitiationElement::reason() const
{
    return d->reason;
}

///
/// Sets the Jingle Message Initiation element reason.
///
void QXmppJingleMessageInitiationElement::setReason(std::optional<QXmppJingleReason> reason)
{
    d->reason = reason;
}

///
/// Returns true if the Jingle Message Initiation element contains a <tie-break/> tag.
///
bool QXmppJingleMessageInitiationElement::containsTieBreak() const
{
    return d->containsTieBreak;
}

///
/// Sets if the Jingle Message Initiation element contains a <tie-break/> tag.
///
void QXmppJingleMessageInitiationElement::setContainsTieBreak(bool containsTieBreak)
{
    d->containsTieBreak = containsTieBreak;
}

///
/// Returns the Jingle Message Initiation element ID migrated to if the Jingle is being migrated
/// to a different device.
///
QString QXmppJingleMessageInitiationElement::migratedTo() const
{
    return d->migratedTo;
}

///
/// Sets the Jingle Message Initiation element ID migrated to if the Jingle is being migrated
/// to a different device.
///
void QXmppJingleMessageInitiationElement::setMigratedTo(const QString &migratedTo)
{
    d->migratedTo = migratedTo;
}

/// \cond
void QXmppJingleMessageInitiationElement::parse(const QDomElement &element)
{
    std::optional<Type> type { stringToJmiElementType(element.nodeName()) };

    if (!type.has_value()) {
        return;
    }

    d->type = type.value();
    d->id = element.attribute(u"id"_s);

    // Type::Proceed and Type::Ringing don't need any parsing aside of the id.
    switch (d->type) {
    case Type::Propose: {
        if (const auto &descriptionElement = firstChildElement(element, u"description"); !descriptionElement.isNull()) {
            d->description = QXmppJingleDescription();
            d->description->parse(descriptionElement);
        }

        break;
    }
    case Type::Reject:
    case Type::Retract:
        d->containsTieBreak = !firstChildElement(element, u"tie-break").isNull();

        if (const auto &reasonElement = firstChildElement(element, u"reason"); !reasonElement.isNull()) {
            d->reason = QXmppJingleReason();
            d->reason->parse(reasonElement);
        }

        break;
    case Type::Finish:
        if (auto reasonElement = firstChildElement(element, u"reason"); !reasonElement.isNull()) {
            d->reason = QXmppJingleReason();
            d->reason->parse(reasonElement);
        }

        if (auto migratedToElement = firstChildElement(element, u"migrated"); !migratedToElement.isNull()) {
            d->migratedTo = migratedToElement.attribute(u"to"_s);
        }

        break;
    default:
        break;
    }
}

void QXmppJingleMessageInitiationElement::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(jmiElementTypeToString(d->type));
    writer->writeDefaultNamespace(toString65(ns_jingle_message_initiation));

    writer->writeDefaultNamespace(toString65(ns_jingle_message_initiation));

    if (d->description) {
        d->description->toXml(writer);
    }

    if (d->reason) {
        d->reason->toXml(writer);
    }

    if (d->containsTieBreak) {
        writer->writeEmptyElement(u"tie-break"_s);
    }

    if (!d->migratedTo.isEmpty()) {
        writer->writeEmptyElement(u"migrated"_s);
        writeOptionalXmlAttribute(writer, u"to", d->migratedTo);
    }

    writer->writeEndElement();
}
/// \endcond

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppJingleMessageInitiationElement)

///
/// Returns true if passed QDomElement is a Jingle Message Initiation element
///
bool QXmppJingleMessageInitiationElement::isJingleMessageInitiationElement(const QDomElement &element)
{
    return stringToJmiElementType(element.tagName()).has_value() && element.hasAttribute(u"id"_s) && element.namespaceURI() == ns_jingle_message_initiation;
}

///
/// Takes a Jingle Message Initiation element type and parses it to a string.
///
QString QXmppJingleMessageInitiationElement::jmiElementTypeToString(Type type)
{
    switch (type) {
    case Type::Propose:
        return u"propose"_s;
    case Type::Ringing:
        return u"ringing"_s;
    case Type::Proceed:
        return u"proceed"_s;
    case Type::Reject:
        return u"reject"_s;
    case Type::Retract:
        return u"retract"_s;
    case Type::Finish:
        return u"finish"_s;
    default:
        return {};
    }
}

///
/// Takes a string and parses it to a Jingle Message Initiation element type.
///
std::optional<QXmppJingleMessageInitiationElement::Type> QXmppJingleMessageInitiationElement::stringToJmiElementType(const QString &typeStr)
{
    if (typeStr == u"propose") {
        return Type::Propose;
    } else if (typeStr == u"ringing") {
        return Type::Ringing;
    } else if (typeStr == u"proceed") {
        return Type::Proceed;
    } else if (typeStr == u"reject") {
        return Type::Reject;
    } else if (typeStr == u"retract") {
        return Type::Retract;
    } else if (typeStr == u"finish") {
        return Type::Finish;
    }

    return std::nullopt;
}

class QXmppCallInviteElementPrivate : public QSharedData
{
public:
    QXmppCallInviteElement::Type type { QXmppCallInviteElement::Type::None };
    QString id;

    std::optional<QXmppCallInviteElement::Jingle> jingle;
    std::optional<QVector<QXmppCallInviteElement::External>> external;

    bool audio = true;
    bool video = false;
};

///
/// \enum QXmppCallInviteElement::Type
///
/// Possible types of Call Invite elements
///

///
/// \class QXmppCallInviteElement
///
/// \brief The QXmppCallInviteElement class represents a Call Invite
/// element as specified by \xep{0482, Call Invites}.
///
/// \ingroup Stanzas
///
/// \since QXmpp 1.6
///

///
/// \brief Constructs a Call Invite element.
///
QXmppCallInviteElement::QXmppCallInviteElement()
    : d(new QXmppCallInviteElementPrivate())
{
}

///
/// Returns the Call Invite element type.
///
QXmppCallInviteElement::Type QXmppCallInviteElement::type() const
{
    return d->type;
}

///
/// Sets the Call Invite element type.
///
void QXmppCallInviteElement::setType(Type type)
{
    d->type = type;
}

///
/// Returns the Call Invite element id.
///
QString QXmppCallInviteElement::id() const
{
    return d->id;
}

///
/// Sets the Call Invite element id.
///
void QXmppCallInviteElement::setId(const QString &id)
{
    d->id = id;
}

///
/// Returns the Call Invite element audio flag.
///
bool QXmppCallInviteElement::audio() const
{
    return d->audio;
}

///
/// Sets the Call Invite element audio flag.
///
void QXmppCallInviteElement::setAudio(bool audio)
{
    d->audio = audio;
}

///
/// Returns the Call Invite element video flag.
///
bool QXmppCallInviteElement::video() const
{
    return d->video;
}

///
/// Sets the Call Invite element video flag.
///
void QXmppCallInviteElement::setVideo(bool video)
{
    d->video = video;
}

///
/// Returns a possible Call Invite element "jingle" sub element.
///
std::optional<QXmppCallInviteElement::Jingle> QXmppCallInviteElement::jingle() const
{
    return d->jingle;
}

///
/// Sets a possible Call Invite "jingle" sub element.
///
void QXmppCallInviteElement::setJingle(std::optional<Jingle> jingle)
{
    d->jingle = jingle;
}

///
/// Returns possible Call Invite "external" sub elements.
///
std::optional<QVector<QXmppCallInviteElement::External>> QXmppCallInviteElement::external() const
{
    return d->external;
}

///
/// Sets possible Call Invite "external" sub elements.
///
void QXmppCallInviteElement::setExternal(std::optional<QVector<External>> external)
{
    d->external = external;
}

/// \cond
void QXmppCallInviteElement::parse(const QDomElement &element)
{
    std::optional<Type> type { stringToCallInviteElementType(element.nodeName()) };

    if (!type) {
        return;
    }

    d->type = type.value();
    d->id = element.attribute(u"id"_s);

    switch (d->type) {
    case Type::Invite:
        d->audio = element.attribute(u"audio"_s, u"true"_s) == u"true" ? true : false;
        d->video = element.attribute(u"video"_s, u"false"_s) == u"true" ? true : false;
        // fall through
    case Type::Accept: {
        if (auto jingleElement = firstChildElement(element, u"jingle"); !jingleElement.isNull()) {
            d->jingle = Jingle();
            d->jingle->parse(jingleElement);
        }

        QVector<External> external;
        for (const auto &externalEl : iterChildElements(element, u"external")) {
            external.append(External { externalEl.attribute(u"uri"_s) });
        }
        if (!external.isEmpty()) {
            d->external = external;
        }
        break;
    }
    case Type::Retract:
    case Type::Reject:
    case Type::Left:
    default:
        break;
    }
}

void QXmppCallInviteElement::toXml(QXmlStreamWriter *writer) const
{
    // write starting tag.
    writer->writeStartElement(callInviteElementTypeToString(d->type));

    // write namespace and ID.
    writer->writeDefaultNamespace(toString65(ns_call_invites));
    writeOptionalXmlAttribute(writer, u"id", d->id);

    switch (d->type) {
    case Type::Reject:
    case Type::Retract:
    case Type::Left:
        // no need to go on for reject, retract and left tags.
        break;
    default:
        if (d->type == Type::Invite) {
            // only overwrite defaults.
            if (!d->audio) {
                writeOptionalXmlAttribute(writer, u"audio", u"false");
            }
            if (d->video) {
                writeOptionalXmlAttribute(writer, u"video", u"true");
            }
        }

        if (d->jingle) {
            d->jingle->toXml(writer);
        }

        if (d->external) {
            for (const External &ext : d->external.value()) {
                ext.toXml(writer);
            }
        }

        break;
    }

    writer->writeEndElement();
}
/// \endcond

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppCallInviteElement)

///
/// Returns true if passed QDomElement is a Call Invite element
///
bool QXmppCallInviteElement::isCallInviteElement(const QDomElement &element)
{
    return stringToCallInviteElementType(element.tagName()).has_value() &&
        // "invite" tags don't have an ID yet.
        (element.hasAttribute(u"id"_s) || element.tagName() == callInviteElementTypeToString(Type::Invite)) &&
        element.namespaceURI() == ns_call_invites;
}

///
/// Takes a Call Invite element type and parses it to a string.
///
QString QXmppCallInviteElement::callInviteElementTypeToString(Type type)
{
    switch (type) {
    case Type::Invite:
        return u"invite"_s;
    case Type::Accept:
        return u"accept"_s;
    case Type::Reject:
        return u"reject"_s;
    case Type::Retract:
        return u"retract"_s;
    case Type::Left:
        return u"left"_s;
    default:
        return {};
    }
}

///
/// Takes a string and parses it to a Call Invite element type.
///
std::optional<QXmppCallInviteElement::Type> QXmppCallInviteElement::stringToCallInviteElementType(const QString &typeStr)
{
    if (typeStr == u"invite") {
        return Type::Invite;
    } else if (typeStr == u"accept") {
        return Type::Accept;
    } else if (typeStr == u"reject") {
        return Type::Reject;
    } else if (typeStr == u"retract") {
        return Type::Retract;
    } else if (typeStr == u"left") {
        return Type::Left;
    }

    return std::nullopt;
}

/// \cond
void QXmppCallInviteElement::Jingle::parse(const QDomElement &element)
{
    if (element.hasAttribute(u"sid"_s)) {
        sid = element.attribute(u"sid"_s);
    }

    if (element.hasAttribute(u"jid"_s)) {
        jid = element.attribute(u"jid"_s);
    }
}

void QXmppCallInviteElement::Jingle::toXml(QXmlStreamWriter *writer) const
{
    writer->writeEmptyElement(u"jingle"_s);
    writeOptionalXmlAttribute(writer, u"sid", sid);

    if (jid) {
        writeOptionalXmlAttribute(writer, u"jid", *jid);
    }
}

void QXmppCallInviteElement::External::toXml(QXmlStreamWriter *writer) const
{
    writer->writeEmptyElement(u"external"_s);
    writeOptionalXmlAttribute(writer, u"uri", uri);
}
/// \endcond
