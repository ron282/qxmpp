// SPDX-FileCopyrightText: 2022 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef QCAINITIALIZER_P_H
#define QCAINITIALIZER_P_H

#include "QXmppGlobal.h"

#include <memory>

namespace QCA {
class Initializer;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 9, 0)
namespace QXmpp { namespace Private {
#else
namespace QXmpp::Private {
#endif
// export required for tests
class QXMPP_EXPORT QcaInitializer
{
public:
    QcaInitializer();

private:
    static std::shared_ptr<QCA::Initializer> createInitializer();
    std::shared_ptr<QCA::Initializer> d;
};

#if QT_VERSION < QT_VERSION_CHECK(5, 9, 0)
}  /* namespace Private*/ }  /*namespace QXmpp*/
#else
}  // namespace QXmpp::Private
#endif

#endif  // QCAINITIALIZER_P_H
