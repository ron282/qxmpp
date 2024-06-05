/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Copyright (C) 2018 Intel Corporation.
** Copyright (C) 2019 Mail.ru Group.
** Contact: https://www.qt.io/licensing/
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

#include "qemustringview2.h"

qsizetype QEmuPrivate::findString(QEmuStringView haystack0, qsizetype from, QEmuStringView needle0, Qt::CaseSensitivity cs) noexcept
{
    const qsizetype l = haystack0.size();
    const qsizetype sl = needle0.size();
    if (from < 0)
        from += l;
    if (std::size_t(sl + from) > std::size_t(l))
        return -1;
    if (!sl)
        return from;
    if (!l)
        return -1;
    if (sl == 1)
        return qFindChar(haystack0,
needle0[0], from, cs);
    /*
        We use the Boyer-Moore algorithm in cases where the overhead
        for the skip table should pay off, otherwise we use a simple
        hash function.
    */
    if (l > 500 && sl > 5)
        return qFindStringBoyerMoore(haystack0, from,
needle0, cs);
    auto sv = [sl](const ushort *v) { return QStringView(v, sl); };
    /*
        We use some hashing for efficiency's sake. Instead of
        comparing strings, we compare the hash value of str with that
        of a part of this QString. Only if that matches, we call
        qt_string_compare().
    */
    const ushort *needle = (const ushort *)needle0.data();
    const ushort *haystack = (const ushort *)(haystack0.data()) + from;
    const ushort *end = (const ushort *)(haystack0.data()) + (l - sl);
    const std::size_t sl_minus_1 = sl - 1;
    std::size_t hashNeedle = 0, hashHaystack = 0;
    qsizetype idx;
    if (cs == Qt::CaseSensitive) {
        for (idx = 0; idx < sl; ++idx) {
            hashNeedle = ((hashNeedle<<1) + needle[idx]);
            hashHaystack = ((hashHaystack<<1) + haystack[idx]);
        }
        hashHaystack -= haystack[sl_minus_1];
        while (haystack <= end) {
            hashHaystack += haystack[sl_minus_1];
            if (hashHaystack == hashNeedle
                 && qt_compare_strings(needle0, sv(haystack),
Qt::CaseSensitive) == 0)
                return haystack - (const ushort *)haystack0.data();
            REHASH(*haystack);
            ++haystack;
        }
    } else {
        const ushort *haystack_start = (const ushort *)haystack0.data();
        for (idx = 0; idx < sl; ++idx) {
            hashNeedle = (hashNeedle<<1) + foldCase(needle + idx,
needle);
            hashHaystack = (hashHaystack<<1) + foldCase(haystack + idx,
haystack_start);
        }
        hashHaystack -= foldCase(haystack + sl_minus_1,
haystack_start);
        while (haystack <= end) {
            hashHaystack += foldCase(haystack + sl_minus_1,
haystack_start);
            if (hashHaystack == hashNeedle
                 && qt_compare_strings(needle0, sv(haystack),
Qt::CaseInsensitive) == 0)
                return haystack - (const ushort *)haystack0.data();
            REHASH(foldCase(haystack, haystack_start));
            ++haystack;
        }
    }
    return -1;
}
