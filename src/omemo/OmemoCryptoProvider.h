// SPDX-FileCopyrightText: 2022 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef OMEMOCRYPTOPROVIDER_H
#define OMEMOCRYPTOPROVIDER_H

#include <signal_protocol.h>

class QXmppOmemoManagerPrivate;

#if defined(SFOS)
namespace QXmpp { namespace Omemo { namespace Private {
signal_crypto_provider createOmemoCryptoProvider(QXmppOmemoManagerPrivate *d);
} } }
#else
namespace QXmpp::Omemo::Private {
signal_crypto_provider createOmemoCryptoProvider(QXmppOmemoManagerPrivate *d);
}
#endif


#endif  // OMEMOCRYPTOPROVIDER_H
