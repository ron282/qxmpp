// SPDX-FileCopyrightText: 2010 Manjeet Dahiya <manjeetdahiya@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "QXmppEntityTimeIq.h"

#include "QXmppConstants_p.h"
#include "QXmppUtils.h"

#include <QDomElement>

///
/// Returns the timezone offset in seconds.
///
int QXmppEntityTimeIq::tzo() const
{
    return m_tzo;
}

///
/// Sets the timezone offset in seconds.
///
/// \param tzo
///
void QXmppEntityTimeIq::setTzo(int tzo)
{
    m_tzo = tzo;
}

///
/// Returns the date/time in Coordinated Universal Time (UTC).
///
QDateTime QXmppEntityTimeIq::utc() const
{
    return m_utc;
}

///
/// Sets the date/time in Coordinated Universal Time (UTC).
///
/// \param utc
///
void QXmppEntityTimeIq::setUtc(const QDateTime &utc)
{
    m_utc = utc;
}

///
/// Returns true, if the element is a valid entity time IQ.
///
bool QXmppEntityTimeIq::isEntityTimeIq(const QDomElement &element)
{
    QDomElement timeElement = element.firstChildElement("time");
    return timeElement.namespaceURI() == ns_entity_time;
}

/// \cond
bool QXmppEntityTimeIq::checkIqType(const QString &tagName, const QString &xmlns)
{
    return tagName == QStringLiteral("time") && xmlns == ns_entity_time;
}

void QXmppEntityTimeIq::parseElementFromChild(const QDomElement &element)
{
    QDomElement timeElement = element.firstChildElement("time");
    m_tzo = QXmppUtils::timezoneOffsetFromString(timeElement.firstChildElement("tzo").text());
    m_utc = QXmppUtils::datetimeFromString(timeElement.firstChildElement("utc").text());
}

void QXmppEntityTimeIq::toXmlElementFromChild(QXmlStreamWriter *writer) const
{
    writer->writeStartElement("time");
    writer->writeDefaultNamespace(ns_entity_time);

    if (m_utc.isValid()) {
        helperToXmlAddTextElement(writer, "tzo", QXmppUtils::timezoneOffsetToString(m_tzo));
        helperToXmlAddTextElement(writer, "utc", QXmppUtils::datetimeToString(m_utc));
    }
    writer->writeEndElement();
}
/// \endcond
