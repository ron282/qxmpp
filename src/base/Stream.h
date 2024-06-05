// SPDX-FileCopyrightText: 2024 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef STREAM_H
#define STREAM_H

#include <QString>

class QXmlStreamWriter;

#if defined(SFOS)
namespace QXmpp { namespace Private {
#else
namespace QXmpp::Private {
#endif

struct StreamOpen {
    void toXml(QXmlStreamWriter *) const;

    QString to;
    QString from;
    QStringView xmlns;
};

#if defined(SFOS)
}  }  // namespace QXmpp  Private
#else
}  // namespace QXmpp::Private
#endif

#endif  // STREAM_H
