// SPDX-FileCopyrightText: 2021 Linus Jahn <lnj@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef STRINGLITERALS_H
#define STRINGLITERALS_H

#include <QString>

#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
using namespace Qt::Literals::StringLiterals;
#elif QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
inline QString operator"" _s(const char16_t *str, size_t size) noexcept
{
    return QString(QStringPrivate(nullptr, const_cast<char16_t *>(str), qsizetype(size)));
}

constexpr inline QLatin1String operator"" _L1(const char *str, size_t size) noexcept
{
    return QLatin1String { str, int(size) };
}
#else

#if __cplusplus < 202002L
#include "ranges.hpp"
#else
#include <ranges>
#endif

#if defined(SFOS)
namespace QXmpp {  namespace Private {
#else
namespace QXmpp::Private {
#endif

template<std::size_t N>
struct StringLiteralData {
    char16_t data[N];
	std::size_t size = N;

    constexpr StringLiteralData(const char16_t (&str)[N])
    {
#if __cplusplus < 202002L
		std::copy(str, data);
#else
		std::ranges::copy(str, data);
#endif
    }
};

template<std::size_t N>
struct StaticStringData {
    QArrayData str = Q_STATIC_STRING_DATA_HEADER_INITIALIZER(N - 1);
    char16_t data[N];

    StaticStringData(const char16_t (&str)[N])
    {
#if __cplusplus < 202002L
		std::copy(str, data);
#else
        std::ranges::copy(str, data);
#endif
	}

    QStringData *data_ptr() const
    {
        return const_cast<QStringData *>(static_cast<const QStringData *>(&str));
    }
};

#if defined(SFOS)
} }
#else
}  // namespace QXmpp::Private
#endif

//template<QXmpp::Private::StringLiteralData str>
//QString operator""_s()
//{
//    static const auto staticData = QXmpp::Private::StaticStringData<str.size>(str.data);
//    return QString(QStringDataPtr { staticData.data_ptr() });
//}

QString operator""_s(const char16_t *str, size_t size)
{
    return QString::fromUtf16(str, size);
}


constexpr inline QLatin1String operator"" _L1(const char *str, size_t size) noexcept
{
    return QLatin1String { str, int(size) };
}

#endif

#endif  // STRINGLITERALS_H
