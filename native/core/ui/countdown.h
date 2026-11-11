// The countdown widget's date arithmetic. One entry is the text a person types, "春节 2027-02-06":
// a name, then a date. A date without a year ("生日 03-15") repeats every year and counts to its
// next occurrence. Plain C++ with no Windows types, so it can be tested on its own.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace atrium {

struct CountdownEntry {
    std::wstring name;      // what is counted to; may be empty
    bool hasDate = false;   // false: the text holds no date (yet)
    bool yearly = false;    // written without a year: counts to the next occurrence
    int year = 0, month = 0, day = 0; // the day counted to (for a yearly entry, its next occurrence)
    int days = 0;           // that day minus today: 0 today, negative once it has passed
};

namespace countdown_detail {

inline bool IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

inline int DaysInMonth(int y, int m)
{
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && IsLeap(y) ? 29 : kDays[m - 1];
}

// Days since 1970-01-01 in the proleptic Gregorian calendar.
inline int64_t DaysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline bool IsDateChar(wchar_t c)
{
    return (c >= L'0' && c <= L'9') || c == L'-' || c == L'/' || c == L'.' || c == L'\u5E74' /* 年 */ || c == L'\u6708' /* 月 */ ||
           c == L'\u65E5' /* 日 */;
}

// "2027-02-06", "2027/2/6", "2027.02.06", "2027年2月6日", or without the year: "02-06", "2月6日".
inline bool ParseDateToken(const std::wstring& token, int& a, int& b, int& c, int& groups)
{
    std::vector<int> g;
    std::wstring digits;
    auto flush = [&]() -> bool {
        if (digits.empty())
            return true;
        if (digits.size() > 4)
            return false;
        g.push_back(std::stoi(digits));
        digits.clear();
        return true;
    };
    std::vector<size_t> lengths; // digits of each group: a year has four
    for (size_t i = 0; i < token.size(); ++i) {
        wchar_t ch = token[i];
        if (ch >= L'0' && ch <= L'9') {
            digits += ch;
        } else if (ch == L'-' || ch == L'/' || ch == L'.' || ch == L'年' /* 年 */ || ch == L'月' /* 月 */) {
            if (digits.empty())
                return false;
            lengths.push_back(digits.size());
            if (!flush())
                return false;
        } else if (ch == L'日' /* 日 */ && i + 1 == token.size()) {
            break;
        } else {
            return false;
        }
    }
    if (!digits.empty()) {
        lengths.push_back(digits.size());
        if (!flush())
            return false;
    }
    groups = int(g.size());
    if (groups == 3) {
        if (lengths[0] != 4)
            return false;
        a = g[0], b = g[1], c = g[2];
        return true;
    }
    if (groups == 2) {
        if (lengths[0] > 2 || lengths[1] > 2)
            return false;
        a = 0, b = g[0], c = g[1];
        return true;
    }
    return false;
}

} // namespace countdown_detail

inline int CountdownDaysInMonth(int y, int m) { return countdown_detail::DaysInMonth(y, m); }

// What an entry is saved as: "名称 2027-02-06" (the name may be empty).
inline std::wstring CountdownText(const std::wstring& name, int y, int m, int d)
{
    auto pad = [](int v, size_t width) {
        std::wstring s = std::to_wstring(v);
        while (s.size() < width)
            s.insert(s.begin(), L'0');
        return s;
    };
    const wchar_t* const kSpaces = L" \t\u3000\r\n";
    const size_t first = name.find_first_not_of(kSpaces), last = name.find_last_not_of(kSpaces);
    const std::wstring trimmed = first == std::wstring::npos ? std::wstring() : name.substr(first, last - first + 1);
    return (trimmed.empty() ? std::wstring() : trimmed + L" ") + pad(y, 4) + L"-" + pad(m, 2) + L"-" + pad(d, 2);
}

// `text` as typed; today is (ty, tm, td). The date is the end of the text, with or without a space
// before it: the tail of digits and date marks ("考研2026-12-19").
inline CountdownEntry ParseCountdown(const std::wstring& text, int ty, int tm, int td)
{
    using namespace countdown_detail;
    CountdownEntry e;
    const wchar_t* const kSpaces = L" \t\u3000\r\n";
    const size_t first = text.find_first_not_of(kSpaces), last = text.find_last_not_of(kSpaces);
    if (first == std::wstring::npos)
        return e;
    const std::wstring body = text.substr(first, last - first + 1);
    size_t run = body.size(); // where the tail of date characters starts
    while (run > 0 && IsDateChar(body[run - 1]))
        --run;
    while (run < body.size() && !(body[run] >= L'0' && body[run] <= L'9')) // a date starts with a digit: "生日03-15", "纪念日2027-02-06"
        ++run;
    // The whole tail has to read as a date: a slip such as "13-01" is not trimmed into a different date.
    int y = 0, m = 0, d = 0, groups = 0;
    size_t at = body.size();
    if (run < body.size()) {
        int a, b, c, g;
        bool ok = ParseDateToken(body.substr(run), a, b, c, g) && b >= 1 && b <= 12 && c >= 1;
        if (ok && g == 3)
            ok = a >= 1900 && a <= 2999 && c <= DaysInMonth(a, b);
        else if (ok)
            ok = c <= DaysInMonth(2000, b); // 02-29 is allowed: it falls on 02-28 in a common year
        if (ok) {
            y = a, m = b, d = c, groups = g;
            at = run;
        }
    }
    if (at == body.size()) { // no date: all of it is the name
        e.name = body;
        return e;
    }
    const std::wstring name = body.substr(0, at);
    const size_t end = name.find_last_not_of(kSpaces);
    e.name = end == std::wstring::npos ? std::wstring() : name.substr(0, end + 1);
    e.hasDate = true;
    const int64_t today = DaysFromCivil(ty, tm, td);
    if (groups == 3) {
        e.year = y, e.month = m, e.day = d;
    } else {
        e.yearly = true;
        e.month = m;
        e.year = ty;
        e.day = std::min(d, DaysInMonth(e.year, m));
        if (DaysFromCivil(e.year, e.month, e.day) < today) {
            e.year = ty + 1;
            e.day = std::min(d, DaysInMonth(e.year, m));
        }
    }
    e.days = int(DaysFromCivil(e.year, e.month, e.day) - today);
    return e;
}

// "2027年2月6日"; "每年2月6日" for a yearly entry.
inline std::wstring CountdownDate(const CountdownEntry& e)
{
    std::wstring s = e.yearly ? std::wstring(L"每年") : std::to_wstring(e.year) + L"年";
    return s + std::to_wstring(e.month) + L"月" + std::to_wstring(e.day) + L"日";
}

// ---- hours, minutes and seconds; a message; a colour ----------------------------------------------

constexpr size_t kCountdownNoteMax = 20; // characters of an entry's message

// The part of `add` that fits after `existing` when at most `max` characters (not UTF-16 units) may be
// held: an emoji or a rare ideograph is one character.
inline size_t CountdownLength(const std::wstring& s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if (!(s[i] >= 0xDC00 && s[i] <= 0xDFFF && i > 0 && s[i - 1] >= 0xD800 && s[i - 1] <= 0xDBFF))
            ++n;
    return n;
}

inline std::wstring CountdownClip(const std::wstring& existing, const std::wstring& add, size_t max)
{
    const size_t have = CountdownLength(existing), room = max > have ? max - have : 0;
    size_t n = 0, i = 0;
    while (i < add.size() && n < room) {
        i += (add[i] >= 0xD800 && add[i] <= 0xDBFF && i + 1 < add.size() && add[i + 1] >= 0xDC00 && add[i + 1] <= 0xDFFF) ? 2 : 1;
        ++n;
    }
    return add.substr(0, i);
}

// "05:03:09"
inline std::wstring CountdownClockText(int h, int m, int s)
{
    wchar_t b[16];
    swprintf(b, 16, L"%02d:%02d:%02d", h, m, s);
    return b;
}

// What is left until `tod` seconds past midnight of the entry's day, at the time given. A yearly entry
// whose moment has passed this year counts to the next one (`year` then is that year).
struct CountdownClock {
    bool past = true;   // the moment has come
    int64_t total = 0;  // seconds left; <= 0 once it has come
    int days = 0, h = 0, m = 0, s = 0; // total, split
    int year = 0;       // the year counted to
};

inline CountdownClock CountdownRemaining(const CountdownEntry& e, int tod, int ty, int tm, int td, int th, int tmin, int ts)
{
    using namespace countdown_detail;
    CountdownClock k;
    const int64_t now = DaysFromCivil(ty, tm, td) * 86400 + th * 3600 + tmin * 60 + ts;
    int year = e.year, day = e.day;
    int64_t target = DaysFromCivil(year, e.month, day) * 86400 + tod;
    if (e.yearly && target <= now) {
        year = e.year + 1;
        day = std::min(e.day, DaysInMonth(year, e.month));
        target = DaysFromCivil(year, e.month, day) * 86400 + tod;
    }
    k.year = year;
    k.total = target - now;
    k.past = k.total <= 0;
    if (!k.past) {
        k.days = int(k.total / 86400);
        const int r = int(k.total % 86400);
        k.h = r / 3600, k.m = r % 3600 / 60, k.s = r % 60;
    }
    return k;
}

// Colours of the countdown number a person can pick: none (the card's own: blue, red in the last
// week), six accents, and a strip of 36 hues. 0xRRGGBB.
constexpr uint32_t kCountdownColors[] = {0xFF453A, 0xFF9F0A, 0xFFD60A, 0x30D158, 0x0A84FF, 0xBF5AF2};
constexpr int kCountdownHues = 36;

inline uint32_t CountdownHue(int step)
{
    const float h = float(((step % kCountdownHues) + kCountdownHues) % kCountdownHues) * 360.f / float(kCountdownHues) / 60.f;
    const float s = 0.85f, v = 1.f;
    const int i = int(h);
    const float f = h - float(i), pp = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r = v, g = t, b = pp;
    switch (i % 6) {
    case 1: r = q, g = v, b = pp; break;
    case 2: r = pp, g = v, b = t; break;
    case 3: r = pp, g = q, b = v; break;
    case 4: r = t, g = pp, b = v; break;
    case 5: r = v, g = pp, b = q; break;
    default: break;
    }
    auto ch = [](float x) { return uint32_t(x * 255.f + 0.5f); };
    return (ch(r) << 16) | (ch(g) << 8) | ch(b);
}

} // namespace atrium
