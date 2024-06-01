// SPDX-FileCopyrightText: 2023 Tibor Csötönyi <work@taibsu.de>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef QXMPPEXTERNALSERVICEDISCOVERYIQ_H
#define QXMPPEXTERNALSERVICEDISCOVERYIQ_H

#include "QXmppExternalService.h"
#include "QXmppIq.h"

class QXmppExternalServiceDiscoveryIqPrivate;

class QXMPP_EXPORT QXmppExternalServiceDiscoveryIq : public QXmppIq
{
public:
    QXmppExternalServiceDiscoveryIq();

#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    QXMPP_PRIVATE_DECLARE_RULE_OF_SIX(QXmppExternalServiceDiscoveryIq)
#else
    QXmppExternalServiceDiscoveryIq(const QXmppExternalServiceDiscoveryIq &);
    QXmppExternalServiceDiscoveryIq(QXmppExternalServiceDiscoveryIq &&);
    ~QXmppExternalServiceDiscoveryIq();
    QXmppExternalServiceDiscoveryIq &operator=(const QXmppExternalServiceDiscoveryIq &);
    QXmppExternalServiceDiscoveryIq &operator=(QXmppExternalServiceDiscoveryIq &&);
#endif

    QVector<QXmppExternalService> externalServices();
    void setExternalServices(const QVector<QXmppExternalService> &);
    void addExternalService(const QXmppExternalService &);

    static bool isExternalServiceDiscoveryIq(const QDomElement &);
    static bool checkIqType(const QString &tagName, const QString &xmlNamespace);

protected:
    /// \cond
    void parseElementFromChild(const QDomElement &) override;
    void toXmlElementFromChild(QXmlStreamWriter *) const override;
    /// \endcond

private:
    QSharedDataPointer<QXmppExternalServiceDiscoveryIqPrivate> d;
};

#endif  // QXMPPEXTERNALSERVICEDISCOVERYIQ_H
