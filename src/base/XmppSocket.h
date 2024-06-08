// SPDX-FileCopyrightText: 2024 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef XMPPSOCKET_H
#define XMPPSOCKET_H

#include "QXmppLogger.h"

class QDomElement;
class QSslSocket;
class TestStream;

#if defined(SFOS)
namespace QXmpp {  namespace Private {
#else
namespace QXmpp::Private {
#endif

class SendDataInterface
{
public:
    virtual bool sendData(const QByteArray &) = 0;
};

class QXMPP_EXPORT XmppSocket : public QXmppLoggable, public SendDataInterface
{
    Q_OBJECT
public:
    XmppSocket(QObject *parent);
    ~XmppSocket() override = default;

    QSslSocket *socket() const { return m_socket; }
    void setSocket(QSslSocket *socket);

    bool isConnected() const;
    void disconnectFromHost();
    bool sendData(const QByteArray &) override;

    Q_SIGNAL void started();
    Q_SIGNAL void stanzaReceived(const QDomElement &);
    Q_SIGNAL void streamReceived(const QDomElement &);
    Q_SIGNAL void streamClosed();

private:
    void processData(const QString &data);

    friend class ::TestStream;

    QString m_dataBuffer;
    QSslSocket *m_socket = nullptr;

    // incoming stream state
    QString m_streamOpenElement;
};

#if defined(SFOS)
}  } // namespaces QXmpp Private
#else
}  // namespace QXmpp::Private
#endif

#endif  // XMPPSOCKET_H
