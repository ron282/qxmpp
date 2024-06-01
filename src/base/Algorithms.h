// SPDX-FileCopyrightText: 2024 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <algorithm>

#if defined(SFOS)
namespace QXmpp { namespace Private {
#else
namespace QXmpp::Private {
#endif

template<typename OutputVector, typename InputVector, typename Converter>
auto transform(InputVector &input, Converter convert)
{
    OutputVector output;
    output.reserve(input.size());
    std::transform(input.begin(), input.end(), std::back_inserter(output), std::forward<Converter>(convert));
    return output;
}

#if defined(SFOS)
} } // namespace QXmpp Private
#else
}  // namespace QXmpp::Private
#endif
#endif  // ALGORITHMS_H
