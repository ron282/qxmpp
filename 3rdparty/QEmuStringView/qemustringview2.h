/****************************************************************************
**
** Copyright (C) 2017 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Marc Mutz <marc.mutz@kdab.com>
** Copyright (C) 2019 Mail.ru Group.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#ifndef QEmuStringView_H
#define QEmuStringView_H
#ifndef QT_STRINGVIEW_LEVEL
#  define QT_STRINGVIEW_LEVEL 1
#endif
#include <QtCore/qchar.h>
#include <QtCore/qbytearray.h>
#include <string>
#include <QVector>
class QString;
class QStringRef;
class QRegularExpression;

#ifdef __SSE2__
#include <immintrin.h>
#endif
#include <qalgorithms.h>
#include <qglobal.h>

#if !defined(Q_ASSERT10)
#  if defined(QT_NO_DEBUG) && !defined(QT_FORCE_ASSERTS)
#    define Q_ASSERT10(cond) static_cast<void>(false && (cond))
#  else
#    define Q_ASSERT10(cond) ((cond) ? static_cast<void>(0) : qt_assert(#cond, __FILE__, __LINE__))
#  endif
#endif

using qsizetype = QIntegerForSizeof<std::size_t>::Signed;

namespace QEmuPrivate {
template <typename Char>
struct IsCompatibleCharTypeHelper
    : std::integral_constant<bool,
                             std::is_same<Char, QChar>::value ||
                             std::is_same<Char, ushort>::value ||
                             std::is_same<Char, char16_t>::value ||
                             (std::is_same<Char, wchar_t>::value && sizeof(wchar_t) == sizeof(QChar))> {};
template <typename Char>
struct IsCompatibleCharType
    : IsCompatibleCharTypeHelper<typename std::remove_cv<typename std::remove_reference<Char>::type>::type> {};
template <typename Array>
struct IsCompatibleArrayHelper : std::false_type {};
template <typename Char, size_t N>
struct IsCompatibleArrayHelper<Char[N]>
    : IsCompatibleCharType<Char> {};
template <typename Array>
struct IsCompatibleArray
    : IsCompatibleArrayHelper<typename std::remove_cv<typename std::remove_reference<Array>::type>::type> {};
template <typename Pointer>
struct IsCompatiblePointerHelper : std::false_type {};
template <typename Char>
struct IsCompatiblePointerHelper<Char*>
    : IsCompatibleCharType<Char> {};
template <typename Pointer>
struct IsCompatiblePointer
    : IsCompatiblePointerHelper<typename std::remove_cv<typename std::remove_reference<Pointer>::type>::type> {};
template <typename T>
struct IsCompatibleStdBasicStringHelper : std::false_type {};
template <typename Char, typename...Args>
struct IsCompatibleStdBasicStringHelper<std::basic_string<Char, Args...> >
    : IsCompatibleCharType<Char> {};
template <typename T>
struct IsCompatibleStdBasicString
    : IsCompatibleStdBasicStringHelper<
        typename std::remove_cv<typename std::remove_reference<T>::type>::type
      > {};

inline qsizetype qustrlen(const ushort *str) Q_DECL_NOTHROW
{
#ifdef __SSE2__
// find the 16-byte alignment immediately prior or equal to str
quintptr misalignment = quintptr(str) & 0xf;
Q_ASSERT10((misalignment & 1) == 0);
const ushort *ptr = str - (misalignment / 2);

// load 16 bytes and see if we have a null
// (aligned loads can never segfault)
const __m128i zeroes = _mm_setzero_si128();
__m128i data = _mm_load_si128(reinterpret_cast<const __m128i *>(ptr));
__m128i comparison = _mm_cmpeq_epi16(data, zeroes);
quint32 mask = _mm_movemask_epi8(comparison);

// ignore the result prior to the beginning of str
mask >>= misalignment;

// Have we found something in the first block? Need to handle it now
// because of the left shift above.
if (mask)
return qCountTrailingZeroBits(quint32(mask)) / 2;

do {
ptr += 8;
data = _mm_load_si128(reinterpret_cast<const __m128i *>(ptr));

comparison = _mm_cmpeq_epi16(data, zeroes);
mask = _mm_movemask_epi8(comparison);
} while (mask == 0);

// found a null
uint idx = qCountTrailingZeroBits(quint32(mask));
return ptr - str + idx / 2;
#else
qsizetype result = 0;

if (sizeof(wchar_t) == sizeof(ushort))
return wcslen(reinterpret_cast<const wchar_t *>(str));

while (*str++)
++result;
return result;
#endif
}

} // namespace QEmuPrivate
class QEmuStringView
{
public:
    typedef char16_t storage_type;
    typedef const QChar value_type;
    typedef std::ptrdiff_t difference_type;
    typedef qsizetype size_type;
    typedef value_type &reference;
    typedef value_type &const_reference;
    typedef value_type *pointer;
    typedef value_type *const_pointer;
    typedef pointer iterator;
    typedef const_pointer const_iterator;
    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
private:
    template <typename Char>
    using if_compatible_char = typename std::enable_if<QEmuPrivate::IsCompatibleCharType<Char>::value, bool>::type;
    template <typename Array>
    using if_compatible_array = typename std::enable_if<QEmuPrivate::IsCompatibleArray<Array>::value, bool>::type;
    template <typename Pointer>
    using if_compatible_pointer = typename std::enable_if<QEmuPrivate::IsCompatiblePointer<Pointer>::value, bool>::type;
    template <typename T>
    using if_compatible_string = typename std::enable_if<QEmuPrivate::IsCompatibleStdBasicString<T>::value, bool>::type;
    template <typename T>
    using if_compatible_qstring_like = typename std::enable_if<std::is_same<T, QString>::value || std::is_same<T, QStringRef>::value, bool>::type;
    template <typename Char, size_t N>
    static Q_DECL_CONSTEXPR qsizetype lengthHelperArray(const Char (&)[N]) noexcept
    {
        return qsizetype(N - 1);
    }
    template <typename Char>
    static Q_DECL_RELAXED_CONSTEXPR qsizetype lengthHelperPointer(const Char *str) noexcept
    {
#if defined(Q_CC_GNU) && !defined(Q_CC_CLANG) && !defined(Q_CC_INTEL)
        if (__builtin_constant_p(*str)) {
            qsizetype result = 0;
            while (*str++)
                ++result;
            return result;
        }
#endif
        return QEmuPrivate::qustrlen(
reinterpret_cast<const ushort *>(str));
    }
    static qsizetype lengthHelperPointer(const QChar *str) noexcept
    {
        return QEmuPrivate::qustrlen(
reinterpret_cast<const ushort *>(str));
    }
    template <typename Char>
    static const storage_type *castHelper(const Char *str) noexcept
    { return reinterpret_cast<const storage_type*>(str); }
    static Q_DECL_CONSTEXPR const storage_type *castHelper(const storage_type *str) noexcept
    { return str; }
public:
    Q_DECL_CONSTEXPR QEmuStringView() noexcept
        : m_size(0), m_data(nullptr) {}
    Q_DECL_CONSTEXPR QEmuStringView(std::nullptr_t) noexcept
        : QEmuStringView() {}
    template <typename Char, if_compatible_char<Char> = true>
    Q_DECL_CONSTEXPR QEmuStringView(const Char *str, qsizetype len)
        : m_size((Q_ASSERT10(len >= 0), Q_ASSERT10(str || !len), len)),
          m_data(castHelper(str)) {}
    template <typename Char, if_compatible_char<Char> = true>
    Q_DECL_CONSTEXPR QEmuStringView(const Char *f, const Char *l)
        : QEmuStringView(f, l - f) {}
#ifdef Q_CLANG_QDOC
    template <typename Char, size_t N>
    Q_DECL_CONSTEXPR QEmuStringView(const Char (&array)[N]) noexcept;
    template <typename Char>
    Q_DECL_CONSTEXPR QEmuStringView(const Char *str) noexcept;
#else
#if QT_DEPRECATED_SINCE(5, 14)
    template <typename Array, if_compatible_array<Array> = true>
    QT_DEPRECATED_VERSION_X_5_14(R"(Use u"~~~" or QEmuStringView(u"~~~") instead of QEmuStringViewLiteral("~~~"))")
    Q_DECL_CONSTEXPR QEmuStringView(const Array &str, QEmuPrivate::Deprecated_t) noexcept
        : QEmuStringView(str, lengthHelperArray(str)) {}
#endif // QT_DEPRECATED_SINCE
    template <typename Array, if_compatible_array<Array> = true>
    Q_DECL_CONSTEXPR QEmuStringView(const Array &str) noexcept
        : QEmuStringView(str, lengthHelperArray(str)) {}
    template <typename Pointer, if_compatible_pointer<Pointer> = true>
    Q_DECL_CONSTEXPR QEmuStringView(const Pointer &str) noexcept
        : QEmuStringView(str, str ? lengthHelperPointer(str) : 0) {}
#endif
#ifdef Q_CLANG_QDOC
    QEmuStringView(const QString &str) noexcept;
    QEmuStringView(const QStringRef &str) noexcept;
#else
    template <typename String, if_compatible_qstring_like<String> = true>
    QEmuStringView(const String &str) noexcept
        : QEmuStringView(str.isNull() ? nullptr : str.data(), qsizetype(str.size())) {}
#endif
    template <typename StdBasicString, if_compatible_string<StdBasicString> = true>
    Q_DECL_CONSTEXPR QEmuStringView(const StdBasicString &str) noexcept
        : QEmuStringView(str.data(), qsizetype(str.size())) {}
    Q_REQUIRED_RESULT inline QString toString() const; // defined below
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR qsizetype size() const noexcept { return m_size; }
    Q_REQUIRED_RESULT const_pointer data() const noexcept { return reinterpret_cast<const_pointer>(m_data); }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR const storage_type *utf16() const noexcept { return m_data; }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QChar operator[](qsizetype n) const
    { return Q_ASSERT10(n >= 0), Q_ASSERT10(n < size()), QChar(m_data[n]); }
    //
    // QString API
    //
//    template <typename...Args>
//    Q_REQUIRED_RESULT inline QString arg(Args &&...args) const; // defined in qstring.h
    Q_REQUIRED_RESULT QByteArray toLatin1() const { return (*this).toString().toLatin1(); }
    Q_REQUIRED_RESULT QByteArray toUtf8() const {  return (*this).toString().toUtf8(); }
	Q_REQUIRED_RESULT QByteArray toLocal8Bit() const { return (*this).toString().toLocal8Bit(); }
    Q_REQUIRED_RESULT inline QVector<uint> toUcs4() const { return (*this).toString().toUcs4(); }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QChar at(qsizetype n) const { return (*this)[n]; }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QEmuStringView mid(qsizetype pos) const
    {
        return QEmuStringView(m_data + qBound(qsizetype(0), pos, m_size), m_size - qBound(qsizetype(0), pos,
m_size));
    }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QEmuStringView mid(qsizetype pos, qsizetype n) const
    {
        return QEmuStringView(m_data + qBound(qsizetype(0), pos,
m_size),
            n == -1 ? m_size - pos : qBound(qsizetype(0), pos + n, m_size) - qBound(qsizetype(0), pos,
m_size));
    }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QEmuStringView left(qsizetype n) const
    {
        return QEmuStringView(m_data, (size_t(n) > size_t(m_size) ? m_size : n));
    }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QEmuStringView right(qsizetype n) const
    {
        return QEmuStringView(m_data + m_size - (size_t(n) > size_t(m_size) ? m_size : n), (size_t(n) > size_t(m_size) ? m_size : n));
    }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QEmuStringView chopped(qsizetype n) const
    { return Q_ASSERT10(n >= 0), Q_ASSERT10(n <= size()), QEmuStringView(m_data, m_size - n); }
    Q_DECL_RELAXED_CONSTEXPR void truncate(qsizetype n)
    { Q_ASSERT10(n >= 0); Q_ASSERT10(n <= size()); m_size = n; }
    Q_DECL_RELAXED_CONSTEXPR void chop(qsizetype n)
    { Q_ASSERT10(n >= 0); Q_ASSERT10(n <= size()); m_size -= n; }
//    Q_REQUIRED_RESULT QEmuStringView trimmed() const noexcept { return QEmuPrivate::trimmed(
//*this); }
//    Q_REQUIRED_RESULT int compare(QEmuStringView other, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return QEmuPrivate::compareStrings(*this,
//other, cs); }
    Q_REQUIRED_RESULT inline int compare(QLatin1String other, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept;
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR int compare(QChar c) const noexcept
    { return size() >= 1 ? compare_single_char_helper(*utf16() - c.unicode()) : -1; }
//    Q_REQUIRED_RESULT int compare(QChar c, Qt::CaseSensitivity cs) const noexcept
//    { return QEmuPrivate::compareStrings(*this,
//QEmuStringView(&c, 1), cs); }
//    Q_REQUIRED_RESULT bool startsWith(QEmuStringView s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return QEmuPrivate::startsWith(*this,
//s, cs); }
//    Q_REQUIRED_RESULT inline bool startsWith(QLatin1String s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept;
//    Q_REQUIRED_RESULT bool startsWith(QChar c) const noexcept
//    { return !empty() && front() == c; }
//    Q_REQUIRED_RESULT bool startsWith(QChar c, Qt::CaseSensitivity cs) const noexcept
//    { return QEmuPrivate::startsWith(*this,
//QEmuStringView(&c, 1), cs); }
//    Q_REQUIRED_RESULT bool endsWith(QEmuStringView s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return QEmuPrivate::endsWith(*this,
//s, cs); }
//    Q_REQUIRED_RESULT inline bool endsWith(QLatin1String s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept;
//    Q_REQUIRED_RESULT bool endsWith(QChar c) const noexcept
//    { return !empty() && back() == c; }
//    Q_REQUIRED_RESULT bool endsWith(QChar c, Qt::CaseSensitivity cs) const noexcept
//    { return QEmuPrivate::endsWith(*this,
//QEmuStringView(&c, 1), cs); }
//    Q_REQUIRED_RESULT qsizetype indexOf(QChar c, qsizetype from = 0, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return QEmuPrivate::findString(*this, from,
//QEmuStringView(&c, 1), cs); }
//    Q_REQUIRED_RESULT qsizetype indexOf(QEmuStringView s, qsizetype from = 0, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return QEmuPrivate::findString(*this, from,
//s, cs); }
//    Q_REQUIRED_RESULT inline qsizetype indexOf(QLatin1String s, qsizetype from = 0, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept;
//    Q_REQUIRED_RESULT bool contains(QChar c, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return indexOf(QEmuStringView(&c, 1),
//0, cs) != qsizetype(-1); }
//    Q_REQUIRED_RESULT bool contains(QEmuStringView s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return indexOf(s,
//0, cs) != qsizetype(-1); }
//    Q_REQUIRED_RESULT inline bool contains(QLatin1String s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept;
//    Q_REQUIRED_RESULT inline qsizetype count(QChar c, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept;
//    Q_REQUIRED_RESULT inline qsizetype count(QEmuStringView s, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept;
//    Q_REQUIRED_RESULT qsizetype lastIndexOf(QChar c, qsizetype from = -1, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return QEmuPrivate::lastIndexOf(*this, from,
//QEmuStringView(&c, 1), cs); }
//    Q_REQUIRED_RESULT qsizetype lastIndexOf(QEmuStringView s, qsizetype from = -1, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept
//    { return QEmuPrivate::lastIndexOf(*this, from,
//s, cs); }
//    Q_REQUIRED_RESULT inline qsizetype lastIndexOf(QLatin1String s, qsizetype from = -1, Qt::CaseSensitivity cs = Qt::CaseSensitive) const noexcept;
//    Q_REQUIRED_RESULT bool isRightToLeft() const noexcept
//    { return QEmuPrivate::isRightToLeft(
//*this); }
//    Q_REQUIRED_RESULT bool isValidUtf16() const noexcept
//    { return QEmuPrivate::isValidUtf16(
//*this); }
    Q_REQUIRED_RESULT inline short toShort(bool *ok = nullptr, int base = 10) const;
    Q_REQUIRED_RESULT inline ushort toUShort(bool *ok = nullptr, int base = 10) const;
    Q_REQUIRED_RESULT inline int toInt(bool *ok = nullptr, int base = 10) const;
    Q_REQUIRED_RESULT inline uint toUInt(bool *ok = nullptr, int base = 10) const;
    Q_REQUIRED_RESULT inline long toLong(bool *ok = nullptr, int base = 10) const;
    Q_REQUIRED_RESULT inline ulong toULong(bool *ok = nullptr, int base = 10) const;
    Q_REQUIRED_RESULT inline qlonglong toLongLong(bool *ok = nullptr, int base = 10) const;
    Q_REQUIRED_RESULT inline qulonglong toULongLong(bool *ok = nullptr, int base = 10) const;
    Q_REQUIRED_RESULT inline float toFloat(bool *ok = nullptr) const;
    Q_REQUIRED_RESULT inline double toDouble(bool *ok = nullptr) const;
    Q_REQUIRED_RESULT inline int toWCharArray(wchar_t *array) const; // defined in qstring.h
    Q_REQUIRED_RESULT inline
    QList<QEmuStringView> split(QEmuStringView sep,
                             QString::SplitBehavior behavior = QString::KeepEmptyParts,
                             Qt::CaseSensitivity cs = Qt::CaseSensitive) const;
    Q_REQUIRED_RESULT inline
    QList<QEmuStringView> split(QChar sep, QString::SplitBehavior behavior = QString::KeepEmptyParts,
                             Qt::CaseSensitivity cs = Qt::CaseSensitive) const;/*
#if QT_CONFIG(regularexpression)
    Q_REQUIRED_RESULT inline
    QList<QEmuStringView> split(const QRegularExpression &sep, Qt::SplitBehavior behavior = Qt::KeepEmptyParts) const;
#endif*/
    //
    // STL compatibility API:
    //
    Q_REQUIRED_RESULT const_iterator begin()   const noexcept { return data(); }
    Q_REQUIRED_RESULT const_iterator end()     const noexcept { return data() + size(); }
    Q_REQUIRED_RESULT const_iterator cbegin()  const noexcept { return begin(); }
    Q_REQUIRED_RESULT const_iterator cend()    const noexcept { return end(); }
    Q_REQUIRED_RESULT const_reverse_iterator rbegin()  const noexcept { return const_reverse_iterator(end()); }
    Q_REQUIRED_RESULT const_reverse_iterator rend()    const noexcept { return const_reverse_iterator(begin()); }
    Q_REQUIRED_RESULT const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    Q_REQUIRED_RESULT const_reverse_iterator crend()   const noexcept { return rend(); }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR bool empty() const noexcept { return size() == 0; }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QChar front() const { return Q_ASSERT10(!empty()), QChar(m_data[0]); }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QChar back()  const { return Q_ASSERT10(!empty()), QChar(m_data[m_size - 1]); }
    //
    // Qt compatibility API:
    //
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR bool isNull() const noexcept { return !m_data; }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR bool isEmpty() const noexcept { return empty(); }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR int length() const /* not nothrow! */
    { return Q_ASSERT10(int(size()) == size()), int(size()); }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QChar first() const { return front(); }
    Q_REQUIRED_RESULT Q_DECL_CONSTEXPR QChar last()  const { return back(); }
private:
    qsizetype m_size;
    const storage_type *m_data;
    Q_DECL_CONSTEXPR int compare_single_char_helper(int diff) const noexcept
    { return diff ? diff : size() > 1 ? 1 : 0; }
};
Q_DECLARE_TYPEINFO(QEmuStringView, Q_PRIMITIVE_TYPE);
template <typename QStringLike, typename std::enable_if<
    std::is_same<QStringLike, QString>::value || std::is_same<QStringLike, QStringRef>::value,
    bool>::type = true>
inline QEmuStringView qToStringViewIgnoringNull(const QStringLike &s) noexcept
{ return QEmuStringView(s.data(), s.size()); }

QString QEmuStringView::toString() const
{ return Q_ASSERT10(size() == length()), QString(data(), length()); }

Q_ALWAYS_INLINE QString to_string(QEmuStringView s) noexcept { return s.toString(); }

inline QT_ASCII_CAST_WARN QString operator+(const QString s, const char16_t c)
{ return s + QChar(c); }

inline QT_ASCII_CAST_WARN bool operator!=(const QString s1, const char16_t *s2)
{ return s1 != QString::fromUtf16(s2); }

inline QT_ASCII_CAST_WARN bool operator==(const QString s1, const char16_t *s2)
{ return s1 == QString::fromUtf16(s2); }

inline QT_ASCII_CAST_WARN bool operator==(const QString s1, const QEmuStringView s2)
{ return s1 == s2.toString(); }

inline QT_ASCII_CAST_WARN bool operator!=(const QString s1, const QEmuStringView s2)
{ return s1 != s2.toString(); }

// QStringView <> QStringView
inline bool operator==(QEmuStringView lhs, QEmuStringView rhs) noexcept { return lhs.size() == rhs.size() &&lhs.toString() == rhs.toString(); }
inline bool operator!=(QEmuStringView lhs, QEmuStringView rhs) noexcept { return !(lhs == rhs); }
inline bool operator< (QEmuStringView lhs, QEmuStringView rhs) noexcept { return lhs.toString() < rhs.toString(); }
inline bool operator<=(QEmuStringView lhs, QEmuStringView rhs) noexcept { return lhs.toString() <= rhs.toString(); }
inline bool operator> (QEmuStringView lhs, QEmuStringView rhs) noexcept { return lhs.toString() > rhs.toString(); }
inline bool operator>=(QEmuStringView lhs, QEmuStringView rhs) noexcept { return lhs.toString() >= rhs.toString(); }

// QEmuStringView <> QChar
inline bool operator==(QEmuStringView lhs, QChar rhs) noexcept { return lhs == QEmuStringView(&rhs, 1); }
inline bool operator!=(QEmuStringView lhs, QChar rhs) noexcept { return lhs != QEmuStringView(&rhs, 1); }
inline bool operator< (QEmuStringView lhs, QChar rhs) noexcept { return lhs <  QEmuStringView(&rhs, 1); }
inline bool operator<=(QEmuStringView lhs, QChar rhs) noexcept { return lhs <= QEmuStringView(&rhs, 1); }
inline bool operator> (QEmuStringView lhs, QChar rhs) noexcept { return lhs >  QEmuStringView(&rhs, 1); }
inline bool operator>=(QEmuStringView lhs, QChar rhs) noexcept { return lhs >= QEmuStringView(&rhs, 1); }
inline bool operator==(QChar lhs, QEmuStringView rhs) noexcept { return QEmuStringView(&lhs, 1) == rhs; }
inline bool operator!=(QChar lhs, QEmuStringView rhs) noexcept { return QEmuStringView(&lhs, 1) != rhs; }
inline bool operator< (QChar lhs, QEmuStringView rhs) noexcept { return QEmuStringView(&lhs, 1) <  rhs; }
inline bool operator<=(QChar lhs, QEmuStringView rhs) noexcept { return QEmuStringView(&lhs, 1) <= rhs; }
inline bool operator> (QChar lhs, QEmuStringView rhs) noexcept { return QEmuStringView(&lhs, 1) >  rhs; }
inline bool operator>=(QChar lhs, QEmuStringView rhs) noexcept { return QEmuStringView(&lhs, 1) >= rhs; }

#define QStringView QEmuStringView
#undef QStringViewLiteral
#define QStringViewLiteral(str) QEmuStringView(QStringLiteral(str))

#endif /* QEmuStringView_H */
