// Countdown entries on the Chinese lunar calendar: a lunar month and day that come back every year (a
// birthday, the Spring Festival), resolved to their next Gregorian day through ChineseCalendar (Windows' ICU).
// Without ICU an entry keeps the Gregorian day it was saved with.
#pragma once

#include "shell/lunar.h"
#include "ui/countdown.h"
#include "ui/widget_faces.h"

#include <windows.h>

#include <algorithm>
#include <map>
#include <string>
#include <tuple>

namespace atrium {

inline const wchar_t* LunarMonthName(int m)
{
    static const wchar_t* const names[] = {L"正月", L"二月", L"三月", L"四月", L"五月", L"六月",
                                           L"七月", L"八月", L"九月", L"十月", L"冬月", L"腊月"};
    return names[std::clamp(m, 1, 12) - 1];
}

inline std::wstring LunarDayName(int d)
{
    static const wchar_t* const digits[] = {L"", L"一", L"二", L"三", L"四", L"五", L"六", L"七", L"八", L"九", L"十"};
    d = std::clamp(d, 1, 30);
    if (d == 10)
        return L"初十";
    if (d == 20)
        return L"二十";
    if (d == 30)
        return L"三十";
    const wchar_t* tens = d < 10 ? L"初" : d < 20 ? L"十" : L"廿";
    return std::wstring(tens) + digits[d % 10];
}

// "农历八月十五"
inline std::wstring LunarDateName(int m, int d) { return std::wstring(L"农历") + LunarMonthName(m) + LunarDayName(d); }

// The first day on or after y-m-d whose lunar date is month lm, day ld (not in a leap month); a month that
// has no 30th takes its 29th. Results are kept per day asked (it walks up to 400 days).
inline bool NextLunarDate(const ChineseCalendar& cal, int lm, int ld, int fy, int fm, int fd, int& y, int& m, int& d)
{
    static std::map<std::tuple<int, int, int, int, int>, std::tuple<int, int, int>> cache;
    const auto key = std::make_tuple(lm, ld, fy, fm, fd);
    if (auto it = cache.find(key); it != cache.end()) {
        std::tie(y, m, d) = it->second;
        return y != 0;
    }
    if (cache.size() > 256)
        cache.clear();
    int ry = 0, rm = 0, rd = 0;
    if (cal.Available()) {
        int cy = fy, cm = fm, cd = fd;
        auto next = [](int& yy, int& mm, int& dd) {
            if (++dd > countdown_detail::DaysInMonth(yy, mm)) {
                dd = 1;
                if (++mm > 12)
                    mm = 1, ++yy;
            }
        };
        for (int i = 0; i < 400 && !ry; ++i, next(cy, cm, cd)) {
            int clm = 0, cld = 0, cycle = 0;
            bool leap = false;
            if (!cal.Lunar(cy, cm, cd, clm, cld, leap, cycle) || leap || clm != lm)
                continue;
            if (cld == ld) {
                ry = cy, rm = cm, rd = cd;
            } else if (ld == 30 && cld == 29) { // the 29th is the month's last day: it stands for the 30th
                int ny = cy, nm = cm, nd = cd, nlm = 0, nld = 0, ncycle = 0;
                bool nleap = false;
                next(ny, nm, nd);
                if (cal.Lunar(ny, nm, nd, nlm, nld, nleap, ncycle) && nld == 1)
                    ry = cy, rm = cm, rd = cd;
            }
        }
    }
    cache[key] = std::make_tuple(ry, rm, rd);
    y = ry, m = rm, d = rd;
    return ry != 0;
}

// A lunar entry's next day, written into `e` (as a one-off Gregorian date, so nothing rolls it on by the
// Gregorian year). `rollPastMoment`: on the day itself, once its time of day (hms) has passed, the next year's.
inline void ResolveLunar(CountdownEntry& e, const Reminder& it, const ChineseCalendar* cal, const SYSTEMTIME& now, bool rollPastMoment)
{
    if (!it.lunar || !cal)
        return;
    int y = 0, m = 0, d = 0;
    if (!NextLunarDate(*cal, it.lunarMonth, it.lunarDay, now.wYear, now.wMonth, now.wDay, y, m, d))
        return;
    const int nowSecond = now.wHour * 3600 + now.wMinute * 60 + now.wSecond;
    if (rollPastMoment && it.hms && y == now.wYear && m == now.wMonth && d == now.wDay && it.tod <= nowSecond) {
        int ty = now.wYear, tm = now.wMonth, td = now.wDay + 1;
        if (td > countdown_detail::DaysInMonth(ty, tm))
            td = 1, tm = tm == 12 ? 1 : tm + 1, ty = tm == 1 ? ty + 1 : ty;
        if (!NextLunarDate(*cal, it.lunarMonth, it.lunarDay, ty, tm, td, y, m, d))
            return;
    }
    e.hasDate = true;
    e.yearly = false;
    e.year = y, e.month = m, e.day = d;
    e.days = int(countdown_detail::DaysFromCivil(y, m, d) - countdown_detail::DaysFromCivil(now.wYear, now.wMonth, now.wDay));
}

} // namespace atrium
