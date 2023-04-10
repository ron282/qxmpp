// SPDX-FileCopyrightText: 2010 Jeremy Lainé <jeremy.laine@m4x.org>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "QXmppRpcManager.h"

#include "QXmppClient.h"
#include "QXmppConstants_p.h"
#include "QXmppInvokable.h"
#include "QXmppRemoteMethod.h"
#include "QXmppRpcIq.h"

/// Constructs a QXmppRpcManager.

QXmppRpcManager::QXmppRpcManager()
{
}

/// Adds a local interface which can be queried using RPC.
///
/// \param interface

void QXmppRpcManager::addInvokableInterface(QXmppInvokable *interface)
{
    m_interfaces[interface->metaObject()->className()] = interface;
}

/// Invokes a remote interface using RPC.
///
/// \param iq

void QXmppRpcManager::invokeInterfaceMethod(const QXmppRpcInvokeIq &iq)
{
    QXmppStanza::Error error;

    const QStringList methodBits = iq.method().split('.');
    if (methodBits.size() != 2) {
        return;
    }
    const QString interface = methodBits.first();
    const QString method = methodBits.last();
    QXmppInvokable *iface = m_interfaces.value(interface);
    if (iface) {
        if (iface->isAuthorized(iq.from())) {

            if (iface->interfaces().contains(method)) {
                QVariant result = iface->dispatch(method.toLatin1(),
                                                  iq.arguments());
                QXmppRpcResponseIq resultIq;
                resultIq.setId(iq.id());
                resultIq.setTo(iq.from());
                resultIq.setValues(QVariantList() << result);
                client()->sendPacket(resultIq);
                return;
            } else {
                error.setType(QXmppStanza::Error::Cancel);
                error.setCondition(QXmppStanza::Error::ItemNotFound);
            }
        } else {
            error.setType(QXmppStanza::Error::Auth);
            error.setCondition(QXmppStanza::Error::Forbidden);
        }
    } else {
        error.setType(QXmppStanza::Error::Cancel);
        error.setCondition(QXmppStanza::Error::ItemNotFound);
    }
    QXmppRpcErrorIq errorIq;
    errorIq.setId(iq.id());
    errorIq.setTo(iq.from());
    errorIq.setQuery(iq);
    errorIq.setError(error);
    client()->sendPacket(errorIq);
}

/// Calls a remote method using RPC with the specified arguments.
///
/// \note This method blocks until the response is received, and it may
/// cause XMPP stanzas to be lost!

QXmppRemoteMethodResult QXmppRpcManager::callRemoteMethod(const QString &jid,
                                                          const QString &interface,
                                                          const QVariant &arg1,
                                                          const QVariant &arg2,
                                                          const QVariant &arg3,
                                                          const QVariant &arg4,
                                                          const QVariant &arg5,
                                                          const QVariant &arg6,
                                                          const QVariant &arg7,
                                                          const QVariant &arg8,
                                                          const QVariant &arg9,
                                                          const QVariant &arg10)
{
    QVariantList args;
    if (arg1.isValid()) {
        args << arg1;
    }
    if (arg2.isValid()) {
        args << arg2;
    }
    if (arg3.isValid()) {
        args << arg3;
    }
    if (arg4.isValid()) {
        args << arg4;
    }
    if (arg5.isValid()) {
        args << arg5;
    }
    if (arg6.isValid()) {
        args << arg6;
    }
    if (arg7.isValid()) {
        args << arg7;
    }
    if (arg8.isValid()) {
        args << arg8;
    }
    if (arg9.isValid()) {
        args << arg9;
    }
    if (arg10.isValid()) {
        args << arg10;
    }

    bool check;
    Q_UNUSED(check)

    QXmppRemoteMethod method(jid, interface, args, client());
    check = connect(this, SIGNAL(rpcCallResponse(QXmppRpcResponseIq)),
                    &method, SLOT(gotResult(QXmppRpcResponseIq)));
    Q_ASSERT(check);
    check = connect(this, SIGNAL(rpcCallError(QXmppRpcErrorIq)),
                    &method, SLOT(gotError(QXmppRpcErrorIq)));
    Q_ASSERT(check);

    return method.call();
}

/// \cond
QStringList QXmppRpcManager::discoveryFeatures() const
{
    // XEP-0009: Jabber-RPC
    return QStringList() << ns_rpc;
}

QList<QXmppDiscoveryIq::Identity> QXmppRpcManager::discoveryIdentities() const
{
    QXmppDiscoveryIq::Identity identity;
    identity.setCategory("automation");
    identity.setType("rpc");
    return QList<QXmppDiscoveryIq::Identity>() << identity;
}

bool QXmppRpcManager::handleStanza(const QDomElement &element)
{
    // XEP-0009: Jabber-RPC
    if (QXmppRpcInvokeIq::isRpcInvokeIq(element)) {
        QXmppRpcInvokeIq rpcIqPacket;
        rpcIqPacket.parse(element);
        invokeInterfaceMethod(rpcIqPacket);
        return true;
    } else if (QXmppRpcResponseIq::isRpcResponseIq(element)) {
        QXmppRpcResponseIq rpcResponseIq;
        rpcResponseIq.parse(element);
        Q_EMIT rpcCallResponse(rpcResponseIq);
        return true;
    } else if (QXmppRpcErrorIq::isRpcErrorIq(element)) {
        QXmppRpcErrorIq rpcErrorIq;
        rpcErrorIq.parse(element);
        Q_EMIT rpcCallError(rpcErrorIq);
        return true;
    }
    return false;
}
/// \endcond
