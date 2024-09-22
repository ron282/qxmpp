// SPDX-FileCopyrightText: 2024 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef STREAM_H
#define STREAM_H

#include <optional>

#include <QString>

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
#include "QEmuStringView.h"
#endif

class QDomElement;
class QXmlStreamWriter;

namespace QXmpp::Private {

struct StreamOpen {
    void toXml(QXmlStreamWriter *) const;

    QString to;
    QString from;
    QStringView xmlns;
};

struct StarttlsRequest {
    static std::optional<StarttlsRequest> fromDom(const QDomElement &);
    void toXml(QXmlStreamWriter *) const;
};

struct StarttlsProceed {
    static std::optional<StarttlsProceed> fromDom(const QDomElement &);
    void toXml(QXmlStreamWriter *) const;
};

struct CsiActive {
    void toXml(QXmlStreamWriter *w) const;
};

struct CsiInactive {
    void toXml(QXmlStreamWriter *w) const;
};

}  // namespace QXmpp::Private

#endif  // STREAM_H
