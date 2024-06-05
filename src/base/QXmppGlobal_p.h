// SPDX-FileCopyrightText: 2022 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef QXMPPGLOBAL_P_H
#define QXMPPGLOBAL_P_H

#include "QXmppGlobal.h"

#include <optional>

#if defined(SFOS)
namespace QXmpp { namespace Private {
#else
namespace QXmpp::Private {
#endif

// Encryption enum
std::optional<EncryptionMethod> encryptionFromString(QStringView str);
QStringView encryptionToString(EncryptionMethod);
QStringView encryptionToName(EncryptionMethod);

#if defined(SFOS)
}	}  // namespace QXmpp Private
#else
}  // namespace QXmpp::Private
#endif

#endif  // QXMPPGLOBAL_P_H
