// SPDX-FileCopyrightText: 2023 Tibor Csötönyi <work@taibsu.de>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "QXmppExternalServiceDiscoveryIq.h"

#include "QXmppConstants_p.h"
#include "QXmppUtils.h"
#include "QXmppUtils_p.h"

#include <QDateTime>
#include <QDomElement>

using namespace QXmpp::Private;

using Action = QXmppExternalService::Action;
using Transport = QXmppExternalService::Transport;

QString actionToString(Action action)
{
    switch (action) {
    case Action::Add:
        return QStringLiteral("add");
    case Action::Delete:
        return QStringLiteral("delete");
    case Action::Modify:
        return QStringLiteral("modify");
    }
    return {};
}

std::optional<Action> actionFromString(const QString &string)
{
    if (string == QStringLiteral("add")) {
        return Action::Add;
    } else if (string == QStringLiteral("delete")) {
        return Action::Delete;
    } else if (string == QStringLiteral("modify")) {
        return Action::Modify;
    }

    return std::nullopt;
}

QString transportToString(Transport transport)
{
    switch (transport) {
    case Transport::Tcp:
        return QStringLiteral("tcp");
    case Transport::Udp:
        return QStringLiteral("udp");
    }

    return {};
}

std::optional<Transport> transportFromString(const QString &string)
{
    if (string == QStringLiteral("tcp")) {
        return Transport::Tcp;
    } else if (string == QStringLiteral("udp")) {
        return Transport::Udp;
    }

    return std::nullopt;
}

class QXmppExternalServicePrivate : public QSharedData
{
public:
    QString host;
    QString type;
    std::optional<Action> action;
    std::optional<QDateTime> expires;
    std::optional<QString> name;
    std::optional<QString> password;
    std::optional<int> port;  // recommended
    std::optional<bool> restricted;
    std::optional<Transport> transport;  // recommended
    std::optional<QString> username;
};

class QXmppExternalServiceDiscoveryIqPrivate : public QSharedData
{
public:
    QVector<QXmppExternalService> externalServices;
};

///
/// \class QXmppExternalService
///
/// QXmppExternalService represents a related XMPP entity that can be queried using \xep{0215,
/// External Service Discovery}.
///
/// \since QXmpp 1.6
///

QXmppExternalService::QXmppExternalService()
    : d(new QXmppExternalServicePrivate)
{
}

QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppExternalService)

///
/// Returns the host of the external service.
///
QString QXmppExternalService::host() const
{
    return d->host;
}

///
/// Sets the host of the external service.
///
void QXmppExternalService::setHost(const QString &host)
{
    d->host = host;
}

///
/// Returns the type of the external service.
///
QString QXmppExternalService::type() const
{
    return d->type;
}

///
/// Sets the type of the external service.
///
void QXmppExternalService::setType(const QString &type)
{
    d->type = type;
}

///
/// Returns the action of the external service.
///
std::optional<Action> QXmppExternalService::action() const
{
    return d->action;
}

///
/// Sets the action of the external service.
///
void QXmppExternalService::setAction(std::optional<Action> action)
{
    d->action = action;
}

///
/// Returns the expiration date of the external service.
///
std::optional<QDateTime> QXmppExternalService::expires() const
{
    return d->expires;
}

///
/// Sets the expiration date of the external service.
///
void QXmppExternalService::setExpires(std::optional<QDateTime> expires)
{
    d->expires = std::move(expires);
}

///
/// Returns the name of the external service.
///
std::optional<QString> QXmppExternalService::name() const
{
    return d->name;
}

///
/// Sets the name of the external service.
///
void QXmppExternalService::setName(std::optional<QString> name)
{
    d->name = std::move(name);
}

///
/// Returns the password of the external service.
///
std::optional<QString> QXmppExternalService::password() const
{
    return d->password;
}

///
/// Sets the password of the external service.
///
void QXmppExternalService::setPassword(std::optional<QString> password)
{
    d->password = std::move(password);
}

///
/// Returns the port of the external service.
///
std::optional<int> QXmppExternalService::port() const
{
    return d->port;
}

///
/// Sets the port of the external service.
///
void QXmppExternalService::setPort(std::optional<int> port)
{
    d->port = port;
}

///
/// Returns the restricted mode of the external service.
///
std::optional<bool> QXmppExternalService::restricted() const
{
    return d->restricted;
}
///
/// Sets the restricted mode of the external service.
///
void QXmppExternalService::setRestricted(std::optional<bool> restricted)
{
    d->restricted = restricted;
}

///
/// Returns the transport type of the external service.
///
std::optional<QXmppExternalService::Transport> QXmppExternalService::transport() const
{
    return d->transport;
}

///
/// Sets the transport type of the external service.
///
void QXmppExternalService::setTransport(std::optional<Transport> transport)
{
    d->transport = transport;
}

///
/// Returns the username of the external service.
///
std::optional<QString> QXmppExternalService::username() const
{
    return d->username;
}

///
/// Sets the username of the external service.
///
void QXmppExternalService::setUsername(std::optional<QString> username)
{
    d->username = std::move(username);
}

///
/// Returns true if the element is a valid external service and can be parsed.
///
bool QXmppExternalService::isExternalService(const QDomElement &element)
{
    if (element.tagName() != u"service") {
        return false;
    }

    return !element.attribute(QStringLiteral("host")).isEmpty() &&
        !element.attribute(QStringLiteral("type")).isEmpty();
}

///
/// Parses given DOM element as an external service.
///
void QXmppExternalService::parse(const QDomElement &el)
{
    QDomNamedNodeMap attributes = el.attributes();

    setHost(el.attribute(QStringLiteral("host")));
    setType(el.attribute(QStringLiteral("type")));

    d->action = actionFromString(el.attribute(QStringLiteral("action")));

    if (attributes.contains(QStringLiteral("expires"))) {
        setExpires(QXmppUtils::datetimeFromString(el.attribute(QStringLiteral("expires"))));
    }

    if (attributes.contains(QStringLiteral("name"))) {
        setName(el.attribute(QStringLiteral("name")));
    }

    if (attributes.contains(QStringLiteral("password"))) {
        setPassword(el.attribute(QStringLiteral("password")));
    }

    if (attributes.contains(QStringLiteral("port"))) {
        setPort(el.attribute(QStringLiteral("port")).toInt());
    }

    if (attributes.contains(QStringLiteral("restricted"))) {
        auto restrictedStr = el.attribute(QStringLiteral("restricted"));
        setRestricted(restrictedStr == u"true" || restrictedStr == u"1");
    }

    d->transport = transportFromString(el.attribute(QStringLiteral("transport")));

    if (attributes.contains(QStringLiteral("username"))) {
        setUsername(el.attribute(QStringLiteral("username")));
    }
}

///
/// \brief QXmppExternalService::toXml
///
/// Translates the external service to XML using the provided XML stream writer.
///
/// \param writer
///

void QXmppExternalService::toXml(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("service"));
    writeOptionalXmlAttribute(writer, u"host", d->host);
    writeOptionalXmlAttribute(writer, u"type", d->type);

    if (d->action) {
        writeOptionalXmlAttribute(writer, u"action", actionToString(d->action.value()));
    }

    if (d->expires) {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
//        auto e = *d->expires;
//		auto f = e.toUTC().toString(Qt::ISODate).insert(19, e.toUTC().toString(".zzz"));
//		writeOptionalXmlAttribute(writer, u"expires", QStringView(f));
#else
		writeOptionalXmlAttribute(writer, u"expires", d->expires->toString(Qt::ISODateWithMs));
#endif
    }

    if (d->name) {
        writeOptionalXmlAttribute(writer, u"name", d->name.value());
    }

    if (d->password) {
        writeOptionalXmlAttribute(writer, u"password", d->password.value());
    }

    if (d->port) {
        writeOptionalXmlAttribute(writer, u"port", QString::number(d->port.value()));
    }

    if (d->restricted) {
        writeOptionalXmlAttribute(writer, u"restricted", d->restricted.value() ? u"true" : u"false");
    }

    if (d->transport) {
        writeOptionalXmlAttribute(writer, u"transport", transportToString(d->transport.value()));
    }

    if (d->username) {
        writeOptionalXmlAttribute(writer, u"username", d->username.value());
    }

    writer->writeEndElement();
}

///
/// \brief The QXmppExternalServiceDiscoveryIq class represents an IQ used to discover external
/// services as defined by \xep{0215}: External Service Discovery.
///
/// \ingroup Stanzas
///
/// \since QXmpp 1.6
///

///
/// Constructs an external service discovery IQ.
///
QXmppExternalServiceDiscoveryIq::QXmppExternalServiceDiscoveryIq()
    : d(new QXmppExternalServiceDiscoveryIqPrivate)
{
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
QXMPP_PRIVATE_DEFINE_RULE_OF_SIX(QXmppExternalServiceDiscoveryIq)
#else
QXmppExternalServiceDiscoveryIq::QXmppExternalServiceDiscoveryIq(const QXmppExternalServiceDiscoveryIq &) = default;
QXmppExternalServiceDiscoveryIq::QXmppExternalServiceDiscoveryIq(QXmppExternalServiceDiscoveryIq &&) = default;
QXmppExternalServiceDiscoveryIq::~QXmppExternalServiceDiscoveryIq() = default;
QXmppExternalServiceDiscoveryIq &QXmppExternalServiceDiscoveryIq::operator=(const QXmppExternalServiceDiscoveryIq &) = default;
QXmppExternalServiceDiscoveryIq &QXmppExternalServiceDiscoveryIq::operator=(QXmppExternalServiceDiscoveryIq &&) = default;
#endif

///
/// Returns the external services of the IQ.
///
QVector<QXmppExternalService> QXmppExternalServiceDiscoveryIq::externalServices()
{
    return d->externalServices;
}

///
/// Sets the external services of the IQ.
///
void QXmppExternalServiceDiscoveryIq::setExternalServices(const QVector<QXmppExternalService> &externalServices)
{
    d->externalServices = externalServices;
}

///
/// Adds an external service to the list of external services in the IQ.
///
void QXmppExternalServiceDiscoveryIq::addExternalService(const QXmppExternalService &externalService)
{
    d->externalServices.append(externalService);
}

///
/// Returns true if the provided DOM element is an external service discovery IQ.
///
bool QXmppExternalServiceDiscoveryIq::isExternalServiceDiscoveryIq(const QDomElement &element)
{
    return isIqType(element, u"services", ns_external_service_discovery);
}

///
/// Returns true if the IQ is a valid external service discovery IQ.
///
bool QXmppExternalServiceDiscoveryIq::checkIqType(const QString &tagName, const QString &xmlNamespace)
{
    return tagName == QStringLiteral("services") && (xmlNamespace == ns_external_service_discovery);
}

/// \cond
void QXmppExternalServiceDiscoveryIq::parseElementFromChild(const QDomElement &element)
{
    for (const auto &el : iterChildElements(firstChildElement(element, u"services"))) {
        if (QXmppExternalService::isExternalService(el)) {
            QXmppExternalService service;
            service.parse(el);
            d->externalServices.append(std::move(service));
        }
    }
}

void QXmppExternalServiceDiscoveryIq::toXmlElementFromChild(QXmlStreamWriter *writer) const
{
    writer->writeStartElement(QSL65("services"));
    writer->writeDefaultNamespace(toString65(ns_external_service_discovery));

    for (const QXmppExternalService &item : d->externalServices) {
        item.toXml(writer);
    }

    writer->writeEndElement();
}
/// \endcond
