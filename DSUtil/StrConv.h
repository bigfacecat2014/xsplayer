/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2017 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <atlcoll.h>
#include <string>

template<class T, typename SEP>
T Explode(const T& str, CAtlList<T>& sl, SEP sep, size_t limit = 0)
{
    sl.RemoveAll();

    for (int i = 0, j = 0; ; i = j + 1) {
        j = str.Find(sep, i);

        if (j < 0 || sl.GetCount() == limit - 1) {
            sl.AddTail(str.Mid(i).Trim());
            break;
        } else {
            sl.AddTail(str.Mid(i, j - i).Trim());
        }
    }

    return sl.GetHead();
}

template<class T, typename SEP>
T ExplodeMin(const T& str, CAtlList<T>& sl, SEP sep, size_t limit = 0)
{
    Explode(str, sl, sep, limit);
    POSITION pos = sl.GetHeadPosition();
    while (pos) {
        POSITION tmp = pos;
        if (sl.GetNext(pos).IsEmpty()) {
            sl.RemoveAt(tmp);
        }
    }
    if (sl.IsEmpty()) {
        sl.AddTail(T());    // eh
    }

    return sl.GetHead();
}

template<class T, typename SEP>
T ExplodeEsc(T str, CAtlList<T>& sl, SEP sep, size_t limit = 0, SEP esc = _T('\\'))
{
    sl.RemoveAll();

    int split = 0;
    for (int i = 0, j = 0; ; i = j + 1) {
        j = str.Find(sep, i);
        if (j < 0) {
            break;
        }

        // Skip this separator if it is escaped
        if (j > 0 && str.GetAt(j - 1) == esc) {
            // Delete the escape character
            str.Delete(j - 1);
            continue;
        }

        if (sl.GetCount() < limit - 1) {
            sl.AddTail(str.Mid(split, j - split).Trim());

            // Save new splitting position
            split = j + 1;
        }
    }
    sl.AddTail(str.Mid(split).Trim());

    return sl.GetHead();
}

template<class T, typename SEP>
T Implode(const CAtlList<T>& sl, SEP sep)
{
    T ret;
    POSITION pos = sl.GetHeadPosition();
    while (pos) {
        ret += sl.GetNext(pos);
        if (pos) {
            ret += sep;
        }
    }
    return ret;
}

template<class T, typename SEP>
T ImplodeEsc(const CAtlList<T>& sl, SEP sep, SEP esc = _T('\\'))
{
    T ret;
    T escsep = T(esc) + T(sep);
    POSITION pos = sl.GetHeadPosition();
    while (pos) {
        T str = sl.GetNext(pos);
        str.Replace(T(sep), escsep);
        ret += str;
        if (pos) {
            ret += sep;
        }
    }
    return ret;
}

extern CStringA ConvertMBCS(CStringA str, DWORD SrcCharSet, DWORD DstCharSet);
extern CStringA UrlEncode(const CStringA& strIn);
/**
 * @brief Escape the characters that JSON reserves as special.
 * @param str The string that needs escaping.
 * @return The input string with the special characters escaped.
 */
extern CStringA EscapeJSONString(const CStringA& str);
extern CStringA UrlDecode(const CStringA& strIn);
extern DWORD CharSetToCodePage(DWORD dwCharSet);
extern CAtlList<CString>& MakeLower(CAtlList<CString>& sl);
extern CAtlList<CString>& MakeUpper(CAtlList<CString>& sl);

CString FormatNumber(CString szNumber, bool bNoFractionalDigits = true);

template<class T>
T& FastTrimRight(T& str)
{
    if (!str.IsEmpty()) {
        typename T::PCXSTR szStart = str;
        typename T::PCXSTR szEnd   = szStart + str.GetLength() - 1;
        typename T::PCXSTR szCur   = szEnd;
        for (; szCur >= szStart; szCur--) {
            if (!T::StrTraits::IsSpace(*szCur)) {
                break;
            }
        }

        if (szCur != szEnd) {
            str.Truncate(int(szCur - szStart + 1));
        }
    }

    return str;
}

template<class T>
T& FastTrim(T& str)
{
    return FastTrimRight(str).TrimLeft();
}

template<class T>
int FindOneOf(const T& str, typename T::PCXSTR pszCharSet, int iStart) throw()
{
    ATLASSERT(AtlIsValidString(pszCharSet));
    ATLASSERT(iStart >= 0);

    if (iStart < 0 || iStart >= str.GetLength()) {
        return -1;
    }

    typename T::PCXSTR psz = T::StrTraits::StringScanSet(str.GetString() + iStart, pszCharSet);
    return ((psz == NULL) ? -1 : int(psz - str.GetString()));
}

template<typename T>
CString NumToCString(T num)
{
    static_assert(std::numeric_limits<T>::is_specialized, "NumToCString can be used only for numeric types.");
    return std::to_string(num).c_str();
}
