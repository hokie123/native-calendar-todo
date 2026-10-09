// ==WindhawkMod==
// @id              win11-native-todo-calendar
// @name            Native Calendar Todo
// @description     Native Windows calendar todos plus China statutory holidays and makeup workdays
// @version         0.4.9
// @author          lonfro
// @github          https://github.com/hokie123/native-calendar-todo
// @include         ShellExperienceHost.exe
// @include         ShellHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject
// @license         GPL-3.0-only
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Native Calendar Todo (lean edition)

A local fork of Agenda in Calendar View by lonfro.

- Keep the Windows 11 calendar and display persistent editable tasks.
- Support task details, categories, completion and colored task density bars.
- Show only statutory holidays and makeup workdays from CN-Holidays.
- Holiday dates are red (#B42336); makeup workdays are blue (#0070C0).
- Load the holiday ICS directly from the working remote URL with in-process caching.
- Does not load lunar dates, solar terms, other festivals or local ICS files.
- No external Python script or local calendar synchronization is required.
- Task data remains in the original Windhawk mod storage (same mod ID).

Original inspiration: https://windhawk.net/mods/agenda-in-calendar-view
*/
// ==/WindhawkModReadme==



// ==WindhawkModSettings==
/*
- calendarPosition: below
  $name: Calendar position
  $description: Show calendar above or below the agenda
  $options:
  - above: Show calendar above agenda
  - below: Show calendar below agenda
- hideCompletedEvents: false
  $name: Hide completed events
  $description: |
    Hide events upon completion time. Events without ending times are hidden on the next day.
- groupingMode: inline
  $name: Calendar pane mode
  $description: |
    When in inline mode, the calendar pane is shown in its own row, rather than as a popup.
    Note that this may cause the notifications pane to not be visible on shorter monitors. You can mitigate this by setting a constraint on the height of the calendar pane when it is expanded.
  $options:
  - inline: "Inline: show in separate row"
  - popup: "Popup: show as dismissable overlay"
- maxHeightCollapsed: 260
  $name: Max height of agenda (calendar collapsed/popup)
  $description: |
    Maximum visible height of the events list in pixels before the list starts to scroll. Set to 0 for no limit.
    This constraint applies for when the calendar is collapsed in inline mode or when the calendar is in popup mode.
- maxHeightExpanded: 260
  $name: Max height of agenda (calendar expanded)
  $description: |
    Maximum visible height of the events list in pixels before the list starts to scroll. Set to 0 for no limit.
    This constraint applies for when the calendar is expanded in inline mode. It has no effect when the calendar is in popup mode.
- timeColumnWidth: 65
  $name: Time column width (in pixels)
  $description: |
    Width of the time column in pixels to keep event titles aligned across cards.
    Set to 0 for automatic width.
- hideFocusSession: true
  $name: Hide Focus Session
  $description: Hide the Focus Session control in the calendar/notification center flyout.
- minFetchInterval: 5
  $name: Minimum time between fetches
  $description: |
    Minimum minutes between downloads from the statutory holiday feed.
    Keep a positive value to reuse the in-process cache.
- keyboardShortcuts: true
  $name: Keyboard shortcuts
  $description: Use left and right arrow keys to move backward and forward by a single day respectively
*/
// ==/WindhawkModSettings==

// Parts of the mod were ported from the "Start Menu Styler" mod by m417z
// This includes:
// - The injection hook into the Notification Centre
// For this reason, the original author's license has been attached:

// Copyright (C) 2026 m417z
// Copyright (C) 2026 lonfro
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

// Modified by lonfro in 2026.
// Original work by m417z.

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#undef GetCurrentTime



#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

#include <windhawk_utils.h>

#include <cstdio>
#include <cwchar>
#include <cwctype>

namespace wf = winrt::Windows::Foundation;
namespace ws = winrt::Windows::System;
namespace wadt = winrt::Windows::ApplicationModel::DataTransfer;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxi = winrt::Windows::UI::Xaml::Input;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuc = winrt::Windows::UI::Core;

std::atomic<ULONGLONG> g_lastOpenTick = 0;
std::atomic<ULONGLONG> g_lastFetchTick = 0;

inline void NormalizeSystemTime(SYSTEMTIME& st) {
    FILETIME ft{};
    if (SystemTimeToFileTime(&st, &ft)) {
        FileTimeToSystemTime(&ft, &st);
    }
}

inline void CopyTextToClipboard(const std::wstring& text) {
    if (text.empty()) return;
    try {
        wadt::DataPackage package;
        package.SetText(text);
        wadt::Clipboard::SetContent(package);
    } catch (...) {}
}

inline SYSTEMTIME ShiftLocalDate(const SYSTEMTIME& stLocal, int deltaDays) {
    if (deltaDays == 0)
        return stLocal;
    FILETIME ft{};
    SystemTimeToFileTime(&stLocal, &ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    ULONGLONG dayTicks = 864000000000ULL;
    if (deltaDays > 0) {
        uli.QuadPart += (ULONGLONG)deltaDays * dayTicks;
    } else {
        uli.QuadPart -= (ULONGLONG)(-deltaDays) * dayTicks;
    }
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SYSTEMTIME result{};
    FileTimeToSystemTime(&ft, &result);
    return result;
}

inline int64_t ToFileTimeDays(const SYSTEMTIME& st) {
    SYSTEMTIME dOnly = st;
    dOnly.wHour = dOnly.wMinute = dOnly.wSecond = dOnly.wMilliseconds = 0;
    FILETIME ft{};
    SystemTimeToFileTime(&dOnly, &ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(uli.QuadPart / 864000000000ULL);
}

inline int DaysBetween(const SYSTEMTIME& from, const SYSTEMTIME& to) {
    return static_cast<int>(ToFileTimeDays(to) - ToFileTimeDays(from));
}

inline int CompareDateOnly(const SYSTEMTIME& a, const SYSTEMTIME& b) {
    if (a.wYear != b.wYear)
        return (a.wYear < b.wYear) ? -1 : 1;
    if (a.wMonth != b.wMonth)
        return (a.wMonth < b.wMonth) ? -1 : 1;
    if (a.wDay != b.wDay)
        return (a.wDay < b.wDay) ? -1 : 1;
    return 0;
}

inline int GetDaysInMonth(int year, int month) {
    static const int days[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return isLeap ? 29 : 28;
    }
    if (month >= 1 && month <= 12) return days[month];
    return 30;
}

inline std::wstring FormatDateYmd(const SYSTEMTIME& st) {
    WCHAR buf[16];
    swprintf_s(buf, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
    return std::wstring(buf);
}

inline SYSTEMTIME ApplyOffsetToUtc(const SYSTEMTIME& stRaw, int offsetMinutes) {
    FILETIME ft{};
    SystemTimeToFileTime(&stRaw, &ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    int64_t offsetTicks = static_cast<int64_t>(offsetMinutes) * 600000000LL;
    int64_t utcTicks = static_cast<int64_t>(uli.QuadPart) - offsetTicks;
    if (utcTicks < 0)
        utcTicks = 0;
    uli.QuadPart = static_cast<ULONGLONG>(utcTicks);

    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SYSTEMTIME stUtc{};
    FileTimeToSystemTime(&ft, &stUtc);
    return stUtc;
}

inline SYSTEMTIME TzToSystemLocal(const SYSTEMTIME& stRaw, int offsetMinutes) {
    SYSTEMTIME stUtc = ApplyOffsetToUtc(stRaw, offsetMinutes);
    SYSTEMTIME stLocal{};
    if (SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal)) {
        return stLocal;
    }
    return stUtc;
}

inline SYSTEMTIME ToLocal(const SYSTEMTIME& stUtc, bool isUtc) {
    if (!isUtc)
        return stUtc;
    SYSTEMTIME stLocal{};
    if (SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal)) {
        return stLocal;
    }
    return stUtc;
}

enum class RecurrenceFreq {
    None,
    Daily,
    Weekly,
    Monthly,
    Yearly
};

struct ByDayItem {
    int ord = 0;        // 0 = every; +1 = 1st, +2 = 2nd, -1 = last, -2 = 2nd-to-last, etc.
    int dayOfWeek = 0;  // 0 = Sun, 1 = Mon, ..., 6 = Sat (matching SYSTEMTIME wDayOfWeek)
};

struct RecurrenceRule {
    RecurrenceFreq freq = RecurrenceFreq::None;
    int interval = 1;
    int count = 0;  // 0 = unlimited
    SYSTEMTIME untilUtc{};
    bool hasUntil = false;
    uint8_t byDayMask = 0;  // bit 0 = Sun, 1 = Mon, ..., 6 = Sat
    std::vector<ByDayItem> byDays;
    std::vector<int> byMonthDays;
    int wkst = 1; // 0 = Sun, 1 = Mon, ..., 6 = Sat (RFC 5545 default: 1 = Monday)
};

struct CalendarEvent {
    std::wstring uid;
    std::wstring recurrenceId;
    std::wstring status;
    SYSTEMTIME startLocal{};
    SYSTEMTIME endLocal{};
    bool hasEnd = false;
    bool isAllDay = false;
    ULONGLONG sortKey = 0;
    std::wstring name;
    std::wstring location;
    std::wstring notes;
    std::wstring url;
    bool hasRRule = false;
    RecurrenceRule rrule;
    std::vector<std::wstring> exDates;
};

std::vector<CalendarEvent> g_allParsedEvents;
std::mutex g_eventsMutex;

struct CalendarCacheEntry {
    std::wstring cachedContent;
    std::vector<CalendarEvent> cachedEvents;
    ULONGLONG lastSuccessfulFetchTick = 0;
};

std::mutex g_cacheMutex;
std::mutex g_watcherMutex;
std::unordered_map<std::wstring, CalendarCacheEntry> g_calendarCaches;

inline int SafeParseIntW(const std::wstring& s, size_t pos, size_t len) {
    if (pos + len > s.size())
        return 0;
    int val = 0;
    for (size_t i = 0; i < len; ++i) {
        wchar_t c = s[pos + i];
        if (c < L'0' || c > L'9')
            return 0;
        val = val * 10 + (c - L'0');
    }
    return val;
}

inline bool ParseIcsDateTimeW(const std::wstring& val, SYSTEMTIME& st, bool& isUtc) {
    ZeroMemory(&st, sizeof(st));
    isUtc = false;
    bool hasTime = false;
    if (val.length() >= 8) {
        st.wYear = (WORD)SafeParseIntW(val, 0, 4);
        st.wMonth = (WORD)SafeParseIntW(val, 4, 2);
        st.wDay = (WORD)SafeParseIntW(val, 6, 2);
    }
    if (val.length() >= 15 && (val[8] == L'T' || val[8] == L't')) {
        hasTime = true;
        st.wHour = (WORD)SafeParseIntW(val, 9, 2);
        st.wMinute = (WORD)SafeParseIntW(val, 11, 2);
        st.wSecond = (WORD)SafeParseIntW(val, 13, 2);
    }
    if (!val.empty() && (val.back() == L'Z' || val.back() == L'z')) {
        isUtc = true;
    }
    NormalizeSystemTime(st);
    return hasTime;
}

inline bool MatchesExDate(const std::wstring& exDateStr, const SYSTEMTIME& tDate, const std::wstring& targetYmd) {
    if (exDateStr.find(targetYmd) != std::wstring::npos) {
        return true;
    }
    SYSTEMTIME stEx{};
    bool isUtc = false;
    if (ParseIcsDateTimeW(exDateStr, stEx, isUtc)) {
        SYSTEMTIME localEx = isUtc ? ToLocal(stEx, true) : stEx;
        if (localEx.wYear == tDate.wYear && localEx.wMonth == tDate.wMonth && localEx.wDay == tDate.wDay) {
            return true;
        }
    }
    return false;
}

inline int ParseUtcOffsetMinutes(const std::wstring& s) {
    size_t first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
        return 0;
    size_t last = s.find_last_not_of(L" \t\r\n");
    std::wstring offsetStr = s.substr(first, last - first + 1);

    size_t start = 0;
    int sign = 1;
    if (offsetStr[0] == L'+') {
        sign = 1;
        start = 1;
    } else if (offsetStr[0] == L'-') {
        sign = -1;
        start = 1;
    }
    if (start + 4 <= offsetStr.size()) {
        if (iswdigit(offsetStr[start]) && iswdigit(offsetStr[start + 1]) &&
            iswdigit(offsetStr[start + 2]) && iswdigit(offsetStr[start + 3])) {
            int hours = (offsetStr[start] - L'0') * 10 + (offsetStr[start + 1] - L'0');
            int mins = (offsetStr[start + 2] - L'0') * 10 + (offsetStr[start + 3] - L'0');
            return sign * (hours * 60 + mins);
        }
    }
    return 0;
}

inline std::wstring TrimW(const std::wstring& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == L' ' || s[start] == L'\t' ||
                                s[start] == L'\r' || s[start] == L'\n')) {
        start++;
    }
    size_t end = s.size();
    while (end > start &&
           (s[end - 1] == L' ' || s[end - 1] == L'\t' ||
            s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        end--;
    }
    return s.substr(start, end - start);
}

inline bool ParseByDayToken(const std::wstring& rawToken, ByDayItem& item) {
    std::wstring token = TrimW(rawToken);
    if (token.size() < 2)
        return false;
    std::wstring dayCode = token.substr(token.size() - 2);
    int dow = -1;
    if (dayCode == L"SU") dow = 0;
    else if (dayCode == L"MO") dow = 1;
    else if (dayCode == L"TU") dow = 2;
    else if (dayCode == L"WE") dow = 3;
    else if (dayCode == L"TH") dow = 4;
    else if (dayCode == L"FR") dow = 5;
    else if (dayCode == L"SA") dow = 6;
    else return false;

    item.dayOfWeek = dow;
    item.ord = 0;
    if (token.size() > 2) {
        std::wstring prefix = token.substr(0, token.size() - 2);
        try {
            item.ord = std::stoi(prefix);
        } catch (...) {
            item.ord = 0;
        }
    }
    return true;
}

inline RecurrenceRule ParseRRule(const std::wstring& rruleStr) {
    RecurrenceRule rule;
    std::wstringstream ss(rruleStr);
    std::wstring part;
    while (std::getline(ss, part, L';')) {
        size_t eq = part.find(L'=');
        if (eq == std::wstring::npos)
            continue;
        std::wstring key = part.substr(0, eq);
        std::wstring val = part.substr(eq + 1);

        if (key == L"FREQ") {
            if (val == L"DAILY") rule.freq = RecurrenceFreq::Daily;
            else if (val == L"WEEKLY") rule.freq = RecurrenceFreq::Weekly;
            else if (val == L"MONTHLY") rule.freq = RecurrenceFreq::Monthly;
            else if (val == L"YEARLY") rule.freq = RecurrenceFreq::Yearly;
        } else if (key == L"INTERVAL") {
            try { rule.interval = std::stoi(val); } catch (...) { rule.interval = 1; }
            if (rule.interval <= 0) rule.interval = 1;
        } else if (key == L"COUNT") {
            try { rule.count = std::stoi(val); } catch (...) { rule.count = 0; }
        } else if (key == L"UNTIL") {
            bool isUtc = false;
            SYSTEMTIME stUntil{};
            ParseIcsDateTimeW(val, stUntil, isUtc);
            if (stUntil.wYear > 0 && stUntil.wMonth > 0 && stUntil.wDay > 0) {
                rule.untilUtc = stUntil;
                rule.hasUntil = true;
            }
        } else if (key == L"BYDAY") {
            std::wstringstream dayss(val);
            std::wstring dayToken;
            while (std::getline(dayss, dayToken, L',')) {
                ByDayItem item;
                if (ParseByDayToken(dayToken, item)) {
                    rule.byDays.push_back(item);
                    rule.byDayMask |= (1 << item.dayOfWeek);
                }
            }
        } else if (key == L"BYMONTHDAY") {
            std::wstringstream mds(val);
            std::wstring mdToken;
            while (std::getline(mds, mdToken, L',')) {
                std::wstring token = TrimW(mdToken);
                try {
                    int dayVal = std::stoi(token);
                    if (dayVal >= -31 && dayVal <= 31 && dayVal != 0) {
                        rule.byMonthDays.push_back(dayVal);
                    }
                } catch (...) {
                }
            }
        } else if (key == L"WKST") {
            std::wstring w = TrimW(val);
            if (w == L"SU") rule.wkst = 0;
            else if (w == L"MO") rule.wkst = 1;
            else if (w == L"TU") rule.wkst = 2;
            else if (w == L"WE") rule.wkst = 3;
            else if (w == L"TH") rule.wkst = 4;
            else if (w == L"FR") rule.wkst = 5;
            else if (w == L"SA") rule.wkst = 6;
        }
    }
    return rule;
}

inline bool IsEventCompleted(const CalendarEvent& ev, const SYSTEMTIME& nowLocal) {
    if (ev.hasEnd && !ev.isAllDay) {
        FILETIME ftEnd{}, ftNow{};
        if (SystemTimeToFileTime(&ev.endLocal, &ftEnd) && SystemTimeToFileTime(&nowLocal, &ftNow)) {
            ULARGE_INTEGER uEnd, uNow;
            uEnd.LowPart = ftEnd.dwLowDateTime; uEnd.HighPart = ftEnd.dwHighDateTime;
            uNow.LowPart = ftNow.dwLowDateTime; uNow.HighPart = ftNow.dwHighDateTime;
            return uNow.QuadPart >= uEnd.QuadPart;
        }
        return false;
    } else {
        if (ev.isAllDay && ev.hasEnd) {
            if (CompareDateOnly(ev.startLocal, ev.endLocal) < 0) {
                return CompareDateOnly(nowLocal, ev.endLocal) >= 0;
            } else {
                return CompareDateOnly(nowLocal, ev.endLocal) > 0;
            }
        }
        return CompareDateOnly(nowLocal, ev.startLocal) > 0;
    }
}

inline bool GetHideCompletedEventsSetting() {
    return Wh_GetIntSetting(L"hideCompletedEvents") != 0;
}

inline bool EventOccursOnDate(const CalendarEvent& ev, const SYSTEMTIME& tDate) {
    if (ev.isAllDay && ev.hasEnd) {
        FILETIME ftStart{}, ftEnd{}, ftTarget{};
        SYSTEMTIME sOnly = ev.startLocal;
        sOnly.wHour = sOnly.wMinute = sOnly.wSecond = sOnly.wMilliseconds = 0;
        SYSTEMTIME eOnly = ev.endLocal;
        eOnly.wHour = eOnly.wMinute = eOnly.wSecond = eOnly.wMilliseconds = 0;
        SYSTEMTIME tOnly = tDate;
        tOnly.wHour = tOnly.wMinute = tOnly.wSecond = tOnly.wMilliseconds = 0;
        SystemTimeToFileTime(&sOnly, &ftStart);
        SystemTimeToFileTime(&eOnly, &ftEnd);
        SystemTimeToFileTime(&tOnly, &ftTarget);
        ULARGE_INTEGER uStart{}, uEnd{}, uTarget{};
        uStart.LowPart = ftStart.dwLowDateTime;
        uStart.HighPart = ftStart.dwHighDateTime;
        uEnd.LowPart = ftEnd.dwLowDateTime;
        uEnd.HighPart = ftEnd.dwHighDateTime;
        uTarget.LowPart = ftTarget.dwLowDateTime;
        uTarget.HighPart = ftTarget.dwHighDateTime;

        if (uTarget.QuadPart >= uStart.QuadPart &&
            uTarget.QuadPart < uEnd.QuadPart) {
            return true;
        } else if (uStart.QuadPart == uEnd.QuadPart &&
                   uTarget.QuadPart == uStart.QuadPart) {
            return true;
        }
    } else {
        if (ev.startLocal.wYear == tDate.wYear &&
            ev.startLocal.wMonth == tDate.wMonth &&
            ev.startLocal.wDay == tDate.wDay) {
            return true;
        } else if (ev.hasEnd) {
            if (CompareDateOnly(ev.startLocal, tDate) < 0 &&
                CompareDateOnly(tDate, ev.endLocal) <= 0) {
                if (CompareDateOnly(tDate, ev.endLocal) < 0 ||
                    (ev.endLocal.wHour > 0 || ev.endLocal.wMinute > 0 ||
                     ev.endLocal.wSecond > 0)) {
                    return true;
                }
            }
        }
    }
    return false;
}

inline bool MatchesByDayItem(const ByDayItem& item, const SYSTEMTIME& candDate) {
    if (candDate.wDayOfWeek != item.dayOfWeek) {
        return false;
    }
    if (item.ord == 0) {
        return true;
    }
    if (item.ord > 0) {
        int nth = (candDate.wDay - 1) / 7 + 1;
        return nth == item.ord;
    } else {
        int daysInMonth = GetDaysInMonth(candDate.wYear, candDate.wMonth);
        int daysFromEnd = daysInMonth - candDate.wDay;
        int negNth = -(daysFromEnd / 7 + 1);
        return negNth == item.ord;
    }
}

inline bool MatchesByMonthDay(int d, const SYSTEMTIME& candDate) {
    if (d > 0) {
        return candDate.wDay == d;
    } else if (d < 0) {
        int daysInMonth = GetDaysInMonth(candDate.wYear, candDate.wMonth);
        return candDate.wDay == (daysInMonth + 1 + d);
    }
    return false;
}

inline bool RecurrenceMatchesDate(const CalendarEvent& ev, const SYSTEMTIME& candDate) {
    if (CompareDateOnly(candDate, ev.startLocal) < 0) {
        return false;
    }

    if (ev.rrule.hasUntil) {
        SYSTEMTIME untilLocal = TzToSystemLocal(ev.rrule.untilUtc, 0);
        if (CompareDateOnly(candDate, untilLocal) > 0) {
            return false;
        }
    }

    int interval = (ev.rrule.interval > 0) ? ev.rrule.interval : 1;

    if (ev.rrule.freq == RecurrenceFreq::Daily) {
        if (ev.rrule.byDayMask != 0) {
            if ((ev.rrule.byDayMask & (1 << candDate.wDayOfWeek)) == 0) {
                return false;
            }
        }
        int days = DaysBetween(ev.startLocal, candDate);
        if (days < 0 || (days % interval != 0)) {
            return false;
        }
        int occIndex = days / interval;
        if (ev.rrule.count > 0 && occIndex >= ev.rrule.count) {
            return false;
        }
        return true;
    } else if (ev.rrule.freq == RecurrenceFreq::Weekly) {
        uint8_t mask = ev.rrule.byDayMask;
        if (mask == 0) {
            mask = (1 << ev.startLocal.wDayOfWeek);
        }
        if ((mask & (1 << candDate.wDayOfWeek)) == 0) {
            return false;
        }

        int wkst = (ev.rrule.wkst >= 0 && ev.rrule.wkst <= 6) ? ev.rrule.wkst : 1;
        int startDayInWeek = (ev.startLocal.wDayOfWeek - wkst + 7) % 7;
        int candDayInWeek = (candDate.wDayOfWeek - wkst + 7) % 7;
        int64_t startWeekStartDays = ToFileTimeDays(ev.startLocal) - startDayInWeek;
        int64_t candWeekStartDays = ToFileTimeDays(candDate) - candDayInWeek;
        int64_t diffDays = candWeekStartDays - startWeekStartDays;
        if (diffDays < 0) {
            return false;
        }
        int weekDiff = static_cast<int>(diffDays / 7);
        if (weekDiff % interval != 0) {
            return false;
        }

        if (ev.rrule.count > 0) {
            int countSoFar = 0;
            for (int w = 0; w <= weekDiff; w += interval) {
                for (int i = 0; i < 7; ++i) {
                    int d = (wkst + i) % 7;
                    if ((mask & (1 << d)) != 0) {
                        if (w == 0 && ((d - wkst + 7) % 7) < startDayInWeek)
                            continue;
                        if (w == weekDiff && ((d - wkst + 7) % 7) > candDayInWeek)
                            break;
                        countSoFar++;
                    }
                }
            }
            if (countSoFar > ev.rrule.count) {
                return false;
            }
        }
        return true;
    } else if (ev.rrule.freq == RecurrenceFreq::Monthly) {
        int monthDiff = (candDate.wYear - ev.startLocal.wYear) * 12 +
                        (candDate.wMonth - ev.startLocal.wMonth);
        if (monthDiff < 0 || (monthDiff % interval != 0)) {
            return false;
        }

        bool dayMatches = false;
        if (!ev.rrule.byDays.empty()) {
            for (const auto& item : ev.rrule.byDays) {
                if (MatchesByDayItem(item, candDate)) {
                    dayMatches = true;
                    break;
                }
            }
        } else if (!ev.rrule.byMonthDays.empty()) {
            for (int d : ev.rrule.byMonthDays) {
                if (MatchesByMonthDay(d, candDate)) {
                    dayMatches = true;
                    break;
                }
            }
        } else {
            if (candDate.wDay == ev.startLocal.wDay) {
                dayMatches = true;
            }
        }

        if (!dayMatches) {
            return false;
        }

        if (ev.rrule.count > 0) {
            int occIndex = monthDiff / interval;
            if (occIndex >= ev.rrule.count) {
                return false;
            }
        }
        return true;
    } else if (ev.rrule.freq == RecurrenceFreq::Yearly) {
        int yearDiff = candDate.wYear - ev.startLocal.wYear;
        if (yearDiff < 0 || (yearDiff % interval != 0)) {
            return false;
        }
        if (candDate.wMonth != ev.startLocal.wMonth) {
            return false;
        }

        bool dayMatches = false;
        if (!ev.rrule.byDays.empty()) {
            for (const auto& item : ev.rrule.byDays) {
                if (MatchesByDayItem(item, candDate)) {
                    dayMatches = true;
                    break;
                }
            }
        } else if (!ev.rrule.byMonthDays.empty()) {
            for (int d : ev.rrule.byMonthDays) {
                if (MatchesByMonthDay(d, candDate)) {
                    dayMatches = true;
                    break;
                }
            }
        } else {
            if (candDate.wDay == ev.startLocal.wDay) {
                dayMatches = true;
            }
        }

        if (!dayMatches) {
            return false;
        }

        if (ev.rrule.count > 0) {
            int occIndex = yearDiff / interval;
            if (occIndex >= ev.rrule.count) {
                return false;
            }
        }
        return true;
    }

    return false;
}

std::vector<CalendarEvent> FilterEventsForDate(
    std::vector<CalendarEvent> const& allEvents,
    SYSTEMTIME const& targetDate) {
    bool hideCompleted = GetHideCompletedEventsSetting();
    SYSTEMTIME nowLocal{};
    if (hideCompleted) {
        GetLocalTime(&nowLocal);
    }

    SYSTEMTIME tDate = targetDate;
    NormalizeSystemTime(tDate);
    std::wstring targetYmd = FormatDateYmd(tDate);

    std::unordered_set<std::wstring> cancelledMasterUids;
    // Map/set of overridden occurrence start dates: key = uid + L"#" + YYYYMMDD
    std::unordered_set<std::wstring> overriddenOccurrences;

    // Pass 1: find cancellations and overridden instances
    for (const auto& ev : allEvents) {
        if (ev.uid.empty())
            continue;

        if (ev.status == L"CANCELLED") {
            if (ev.recurrenceId.empty()) {
                cancelledMasterUids.insert(ev.uid);
            } else {
                SYSTEMTIME stRec{};
                bool isUtc = false;
                if (ParseIcsDateTimeW(ev.recurrenceId, stRec, isUtc)) {
                    SYSTEMTIME localRec = isUtc ? ToLocal(stRec, true) : stRec;
                    overriddenOccurrences.insert(ev.uid + L"#" +
                                                 FormatDateYmd(localRec));
                }
                if (ev.recurrenceId.size() >= 8) {
                    overriddenOccurrences.insert(
                        ev.uid + L"#" + ev.recurrenceId.substr(0, 8));
                }
            }
        } else {
            if (!ev.recurrenceId.empty()) {
                SYSTEMTIME stRec{};
                bool isUtc = false;
                if (ParseIcsDateTimeW(ev.recurrenceId, stRec, isUtc)) {
                    SYSTEMTIME localRec = isUtc ? ToLocal(stRec, true) : stRec;
                    overriddenOccurrences.insert(ev.uid + L"#" +
                                                 FormatDateYmd(localRec));
                }
                if (ev.recurrenceId.size() >= 8) {
                    overriddenOccurrences.insert(
                        ev.uid + L"#" + ev.recurrenceId.substr(0, 8));
                }
            }
        }
    }

    // Pass 2: filter single events and expand recurrences
    std::vector<CalendarEvent> result;

    for (const auto& ev : allEvents) {
        if (!ev.uid.empty() && cancelledMasterUids.count(ev.uid)) {
            continue;
        }
        if (ev.status == L"CANCELLED") {
            continue;
        }

        if (!ev.hasRRule) {
            if (EventOccursOnDate(ev, tDate)) {
                if (hideCompleted && IsEventCompleted(ev, nowLocal)) {
                    continue;
                }
                result.push_back(ev);
            }
            continue;
        }

        // Recurring master event:
        // Duration of an occurrence in days:
        int spanDays = 0;
        if (ev.hasEnd) {
            spanDays = DaysBetween(ev.startLocal, ev.endLocal);
            if (spanDays < 0) spanDays = 0;
            if (spanDays > 366) spanDays = 366;
        }

        for (int offset = 0; offset <= spanDays; ++offset) {
            SYSTEMTIME candDate = ShiftLocalDate(tDate, -offset);

            if (CompareDateOnly(candDate, ev.startLocal) < 0) {
                break; // Earlier offsets will also be before startLocal
            }

            std::wstring candYmd = FormatDateYmd(candDate);
            if (!ev.uid.empty() &&
                overriddenOccurrences.count(ev.uid + L"#" + candYmd)) {
                continue;
            }

            bool isExcluded = false;
            for (const auto& ex : ev.exDates) {
                if (MatchesExDate(ex, candDate, candYmd)) {
                    isExcluded = true;
                    break;
                }
            }
            if (isExcluded) {
                continue;
            }

            if (RecurrenceMatchesDate(ev, candDate)) {
                int days = DaysBetween(ev.startLocal, candDate);
                CalendarEvent occ = ev;
                occ.startLocal = ShiftLocalDate(ev.startLocal, days);
                if (ev.hasEnd) {
                    occ.endLocal = ShiftLocalDate(ev.endLocal, days);
                }
                if (EventOccursOnDate(occ, tDate)) {
                    if (hideCompleted && IsEventCompleted(occ, nowLocal)) {
                        continue;
                    }
                    FILETIME ft{};
                    SystemTimeToFileTime(&occ.startLocal, &ft);
                    occ.sortKey =
                        ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                    result.push_back(occ);
                }
            }
        }
    }

    std::sort(result.begin(), result.end(),
              [](const CalendarEvent& a, const CalendarEvent& b) {
                  if (a.isAllDay != b.isAllDay) {
                      return a.isAllDay > b.isAllDay;
                  }
                  return a.sortKey < b.sortKey;
              });
    return result;
}

void OnCalendarOpened();
void RegisterCoreWindowEvents();
void TriggerBackgroundFetch(bool force = false);
void StartWorkerThread();
void StopWorkerThread();

// A single proven working source.  No local paths or extra holiday feeds.
std::vector<std::wstring> GetIcsPathsSetting() {
    return {L"https://cfap.github.io/CN-Holidays/cn-holidays.ics"};
}

bool IsCalendarAboveAgenda() {
    PCWSTR pos = Wh_GetStringSetting(L"calendarPosition");
    bool isAbove = true;
    if (pos) {
        if (wcscmp(pos, L"below") == 0) {
            isAbove = false;
        }
        Wh_FreeStringSetting(pos);
    }
    return isAbove;
}

bool IsInlineCalendarMode() {
    PCWSTR mode = Wh_GetStringSetting(L"groupingMode");
    bool isInline = true;
    if (mode) {
        if (wcscmp(mode, L"popup") == 0) {
            isInline = false;
        }
        Wh_FreeStringSetting(mode);
    }
    return isInline;
}

int GetMaxHeightCollapsedSetting() {
    int val = Wh_GetIntSetting(L"maxHeightCollapsed");
    return (val < 0) ? 0 : val;
}

int GetMaxHeightExpandedSetting() {
    int val = Wh_GetIntSetting(L"maxHeightExpanded");
    return (val < 0) ? 0 : val;
}

int GetTimeColumnWidthSetting() {
    return Wh_GetIntSetting(L"timeColumnWidth");
}

bool ShouldHideFocusSession() {
    return Wh_GetIntSetting(L"hideFocusSession") != 0;
}

int GetMinFetchIntervalSetting() {
    int val = Wh_GetIntSetting(L"minFetchInterval");
    return (val < 0) ? 0 : val;
}

bool GetKeyboardShortcutsSetting() {
    return Wh_GetIntSetting(L"keyboardShortcuts") != 0;
}


std::wstring DecodeIcsBytes(const char* data, size_t size) {
    if (!data || size == 0)
        return {};

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);

    // Check UTF-16 LE BOM: FF FE
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        size_t wcharCount = (size - 2) / sizeof(wchar_t);
        return std::wstring(reinterpret_cast<const wchar_t*>(data + 2),
                            wcharCount);
    }

    // Check UTF-16 BE BOM: FE FF
    if (size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        size_t wcharCount = (size - 2) / sizeof(wchar_t);
        std::wstring result(wcharCount, L'\0');
        for (size_t i = 0; i < wcharCount; ++i) {
            unsigned char b1 = bytes[2 + i * 2];
            unsigned char b2 = bytes[2 + i * 2 + 1];
            result[i] = static_cast<wchar_t>((b1 << 8) | b2);
        }
        return result;
    }

    // Check UTF-8 BOM: EF BB BF
    size_t offset = 0;
    if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
        bytes[2] == 0xBF) {
        offset = 3;
    }

    const char* utf8Data = data + offset;
    size_t utf8Size = size - offset;

    // Try decoding as UTF-8
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Data,
                                   static_cast<int>(utf8Size), nullptr, 0);
    if (wlen > 0) {
        std::wstring result(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8Data, static_cast<int>(utf8Size),
                            &result[0], wlen);
        return result;
    }

    // Fallback: CP_ACP (ANSI)
    wlen = MultiByteToWideChar(CP_ACP, 0, utf8Data,
                               static_cast<int>(utf8Size), nullptr, 0);
    if (wlen > 0) {
        std::wstring result(wlen, L'\0');
        MultiByteToWideChar(CP_ACP, 0, utf8Data, static_cast<int>(utf8Size),
                            &result[0], wlen);
        return result;
    }

    return {};
}

std::wstring UnescapeIcsText(const std::wstring& str) {
    std::wstring result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == L'\\' && i + 1 < str.size()) {
            wchar_t next = str[i + 1];
            if (next == L'n' || next == L'N') {
                result.push_back(L'\n');
                ++i;
            } else if (next == L',' || next == L';' || next == L'\\') {
                result.push_back(next);
                ++i;
            } else {
                result.push_back(str[i]);
            }
        } else {
            result.push_back(str[i]);
        }
    }
    return result;
}

std::wstring FindUrlInText(const std::wstring& text) {
    if (text.empty()) return L"";

    const std::wstring schemes[] = { L"https://", L"http://" };
    size_t bestPos = std::wstring::npos;
    for (const auto& scheme : schemes) {
        for (size_t i = 0; i + scheme.size() <= text.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < scheme.size(); ++j) {
                if (towlower(text[i + j]) != scheme[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                if (bestPos == std::wstring::npos || i < bestPos) {
                    bestPos = i;
                }
                break;
            }
        }
    }

    if (bestPos == std::wstring::npos) return L"";

    size_t endPos = bestPos;
    while (endPos < text.size()) {
        wchar_t c = text[endPos];
        if (iswspace(c) || c == L'"' || c == L'\'' || c == L'<' || c == L'>' ||
            c == L'(' || c == L')' || c == L'[' || c == L']' || c == L'{' || c == L'}') {
            break;
        }
        ++endPos;
    }

    while (endPos > bestPos) {
        wchar_t c = text[endPos - 1];
        if (c == L'.' || c == L',' || c == L';' || c == L':' || c == L'?' || c == L'!') {
            --endPos;
        } else {
            break;
        }
    }

    return text.substr(bestPos, endPos - bestPos);
}

std::vector<CalendarEvent> ParseIcs(const std::wstring& icsContent) {
    std::wstring unfolded;
    unfolded.reserve(icsContent.size());
    for (size_t i = 0; i < icsContent.size(); ++i) {
        if ((icsContent[i] == L'\r' && i + 1 < icsContent.size() &&
             icsContent[i + 1] == L'\n') ||
            icsContent[i] == L'\n') {
            size_t nextPos = (icsContent[i] == L'\r') ? i + 2 : i + 1;
            if (nextPos < icsContent.size() &&
                (icsContent[nextPos] == L' ' || icsContent[nextPos] == L'\t')) {
                i = nextPos;
                continue;
            }
        }
        unfolded.push_back(icsContent[i]);
    }

    std::unordered_map<std::wstring, int> tzOffsets;
    tzOffsets[L"UTC"] = 0;
    tzOffsets[L"GMT"] = 0;
    tzOffsets[L"Z"] = 0;

    std::wstring currentTzid;
    bool inVTimezone = false;

    std::vector<CalendarEvent> events;
    std::wistringstream stream(unfolded);
    std::wstring line;
    bool inEvent = false;
    CalendarEvent currentEvent;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (line.empty())
            continue;

        if (line == L"BEGIN:VTIMEZONE") {
            inVTimezone = true;
            currentTzid.clear();
            continue;
        }
        if (line == L"END:VTIMEZONE") {
            inVTimezone = false;
            currentTzid.clear();
            continue;
        }

        if (inVTimezone) {
            size_t colon = line.find(L':');
            if (colon != std::wstring::npos) {
                std::wstring keyPart = line.substr(0, colon);
                std::wstring valPart = line.substr(colon + 1);
                size_t semi = keyPart.find(L';');
                std::wstring key =
                    (semi != std::wstring::npos) ? keyPart.substr(0, semi) : keyPart;

                if (key == L"TZID") {
                    currentTzid = valPart;
                    if (currentTzid.size() >= 2 && currentTzid.front() == L'"' &&
                        currentTzid.back() == L'"') {
                        currentTzid =
                            currentTzid.substr(1, currentTzid.size() - 2);
                    }
                } else if (key == L"TZOFFSETTO" && !currentTzid.empty()) {
                    tzOffsets[currentTzid] = ParseUtcOffsetMinutes(valPart);
                }
            }
            continue;
        }

        if (line == L"BEGIN:VEVENT") {
            inEvent = true;
            currentEvent = CalendarEvent{};
            continue;
        }
        if (line == L"END:VEVENT") {
            if (inEvent) {
                if (currentEvent.url.empty()) {
                    currentEvent.url = FindUrlInText(currentEvent.location);
                    if (currentEvent.url.empty()) {
                        currentEvent.url = FindUrlInText(currentEvent.notes);
                    }
                }
                events.push_back(currentEvent);
                inEvent = false;
            }
            continue;
        }

        if (!inEvent)
            continue;

        size_t colon = line.find(L':');
        if (colon == std::wstring::npos)
            continue;

        std::wstring keyPart = line.substr(0, colon);
        std::wstring valPart = line.substr(colon + 1);

        size_t semi = keyPart.find(L';');
        std::wstring key =
            (semi != std::wstring::npos) ? keyPart.substr(0, semi) : keyPart;

        if (key == L"UID") {
            currentEvent.uid = valPart;
        } else if (key == L"STATUS") {
            currentEvent.status = valPart;
            std::transform(currentEvent.status.begin(),
                           currentEvent.status.end(),
                           currentEvent.status.begin(), ::towupper);
        } else if (key == L"RECURRENCE-ID") {
            currentEvent.recurrenceId = valPart;
        } else if (key == L"RRULE") {
            currentEvent.rrule = ParseRRule(valPart);
            currentEvent.hasRRule =
                (currentEvent.rrule.freq != RecurrenceFreq::None);
        } else if (key == L"EXDATE") {
            std::wstringstream exss(valPart);
            std::wstring exToken;
            while (std::getline(exss, exToken, L',')) {
                if (!exToken.empty()) {
                    currentEvent.exDates.push_back(exToken);
                }
            }
        } else if (key == L"SUMMARY") {
            currentEvent.name = UnescapeIcsText(valPart);
        } else if (key == L"LOCATION") {
            currentEvent.location = UnescapeIcsText(valPart);
        } else if (key == L"DESCRIPTION") {
            currentEvent.notes = UnescapeIcsText(valPart);
            for (auto& ch : currentEvent.notes) {
                if (ch == L'\r' || ch == L'\n')
                    ch = L' ';
            }
        } else if (key == L"URL") {
            std::wstring u = TrimW(valPart);
            if (_wcsnicmp(u.c_str(), L"http://", 7) == 0 ||
                _wcsnicmp(u.c_str(), L"https://", 8) == 0) {
                currentEvent.url = u;
            }
        } else if (key == L"X-MICROSOFT-CDO-ALLDAYEVENT" ||
                   key == L"X-MICROSOFT-MSNCALENDAR-ALL-DAY-EVENT") {
            if (_wcsicmp(valPart.c_str(), L"TRUE") == 0 || valPart == L"1") {
                currentEvent.isAllDay = true;
            }
        } else if (key == L"DTSTART") {
            bool isValueDate =
                (keyPart.find(L"VALUE=DATE") != std::wstring::npos);
            std::wstring tzid;
            size_t tzidPos = keyPart.find(L"TZID=");
            if (tzidPos != std::wstring::npos) {
                tzid = keyPart.substr(tzidPos + 5);
                size_t semi2 = tzid.find(L';');
                if (semi2 != std::wstring::npos)
                    tzid = tzid.substr(0, semi2);
                if (tzid.size() >= 2 && tzid.front() == L'"' &&
                    tzid.back() == L'"')
                    tzid = tzid.substr(1, tzid.size() - 2);
            }

            SYSTEMTIME stUtc{};
            bool isUtc = false;
            bool hasTime = ParseIcsDateTimeW(valPart, stUtc, isUtc);
            if (!hasTime || isValueDate) {
                currentEvent.isAllDay = true;
                currentEvent.startLocal = stUtc;
            } else if (isUtc) {
                currentEvent.startLocal = TzToSystemLocal(stUtc, 0);
            } else if (!tzid.empty()) {
                auto it = tzOffsets.find(tzid);
                if (it != tzOffsets.end()) {
                    currentEvent.startLocal =
                        TzToSystemLocal(stUtc, it->second);
                } else {
                    currentEvent.startLocal = stUtc;
                }
            } else {
                currentEvent.startLocal = stUtc;
            }

            FILETIME ft{};
            SystemTimeToFileTime(&currentEvent.startLocal, &ft);
            currentEvent.sortKey =
                ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        } else if (key == L"DTEND") {
            bool isValueDate =
                (keyPart.find(L"VALUE=DATE") != std::wstring::npos);
            std::wstring tzid;
            size_t tzidPos = keyPart.find(L"TZID=");
            if (tzidPos != std::wstring::npos) {
                tzid = keyPart.substr(tzidPos + 5);
                size_t semi2 = tzid.find(L';');
                if (semi2 != std::wstring::npos)
                    tzid = tzid.substr(0, semi2);
                if (tzid.size() >= 2 && tzid.front() == L'"' &&
                    tzid.back() == L'"')
                    tzid = tzid.substr(1, tzid.size() - 2);
            }

            SYSTEMTIME stUtc{};
            bool isUtc = false;
            bool hasTime = ParseIcsDateTimeW(valPart, stUtc, isUtc);
            if (!hasTime || isValueDate) {
                currentEvent.isAllDay = true;
                currentEvent.endLocal = stUtc;
            } else if (isUtc) {
                currentEvent.endLocal = TzToSystemLocal(stUtc, 0);
            } else if (!tzid.empty()) {
                auto it = tzOffsets.find(tzid);
                if (it != tzOffsets.end()) {
                    currentEvent.endLocal =
                        TzToSystemLocal(stUtc, it->second);
                } else {
                    currentEvent.endLocal = stUtc;
                }
            } else {
                currentEvent.endLocal = stUtc;
            }
            currentEvent.hasEnd = true;
        }
    }

    return events;
}

// Remote-only holiday feed, with in-process TTL and fallback to last good data.
std::wstring FetchIcsContent(std::wstring const& url,
                             bool force,
                             bool& outFromCache) {
    outFromCache = false;
    int minFetchInterval = GetMinFetchIntervalSetting();
    ULONGLONG ttlMs = static_cast<ULONGLONG>(minFetchInterval) * 60000ULL;
    ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = g_calendarCaches.find(url);
        if (it != g_calendarCaches.end() && !force && minFetchInterval > 0 &&
            !it->second.cachedContent.empty() &&
            (now - it->second.lastSuccessfulFetchTick < ttlMs)) {
            outFromCache = true;
            return it->second.cachedContent;
        }
    }
    Wh_Log(L"Fetching statutory holiday ICS: %s", url.c_str());
    std::wstring data;
    const WH_URL_CONTENT* content = Wh_GetUrlContent(url.c_str(), nullptr);
    if (content && content->statusCode == 200 && content->data) {
        data = DecodeIcsBytes(
            reinterpret_cast<const char*>(content->data), content->length);
        Wh_Log(L"Holiday ICS downloaded: %zu characters", data.size());
    } else {
        Wh_Log(L"Holiday ICS download failed (HTTP=%d)",
               content ? content->statusCode : -1);
    }
    if (content) Wh_FreeUrlContent(content);
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto& entry = g_calendarCaches[url];
    if (!data.empty()) {
        entry.cachedContent = data;
        entry.lastSuccessfulFetchTick = GetTickCount64();
        return data;
    }
    if (!entry.cachedContent.empty()) {
        outFromCache = true;
        Wh_Log(L"Using last cached statutory holiday ICS");
        return entry.cachedContent;
    }
    return {};
}

wf::DateTime SystemTimeToWinRtDateTime(SYSTEMTIME const& stLocal) {
    SYSTEMTIME stUtc{};
    TzSpecificLocalTimeToSystemTime(nullptr, &stLocal, &stUtc);
    FILETIME ft{};
    SystemTimeToFileTime(&stUtc, &ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return wf::DateTime{wf::TimeSpan{static_cast<int64_t>(uli.QuadPart)}};
}

wux::CornerRadius GetStandardCornerRadius() {
    try {
        auto res = wux::Application::Current().Resources();
        if (res.HasKey(winrt::box_value(L"ControlCornerRadius"))) {
            return winrt::unbox_value<wux::CornerRadius>(
                res.Lookup(winrt::box_value(L"ControlCornerRadius")));
        }
    } catch (...) {
    }
    return wux::CornerRadius{4, 4, 4, 4};
}

wuxm::Brush GetCardBackgroundBrush() {
    try {
        auto res = wux::Application::Current().Resources();
        if (res.HasKey(
                winrt::box_value(L"CardBackgroundFillColorDefaultBrush"))) {
            auto brush = res.Lookup(winrt::box_value(
                                        L"CardBackgroundFillColorDefaultBrush"))
                             .try_as<wuxm::Brush>();
            if (brush)
                return brush;
        }
        if (res.HasKey(
                winrt::box_value(L"SystemControlBackgroundBaseLowBrush"))) {
            auto brush = res.Lookup(winrt::box_value(
                                        L"SystemControlBackgroundBaseLowBrush"))
                             .try_as<wuxm::Brush>();
            if (brush)
                return brush;
        }
        if (res.HasKey(winrt::box_value(
                L"SystemControlBackgroundAltMediumLowBrush"))) {
            auto brush =
                res.Lookup(winrt::box_value(
                               L"SystemControlBackgroundAltMediumLowBrush"))
                    .try_as<wuxm::Brush>();
            if (brush)
                return brush;
        }
    } catch (...) {
    }

    return wuxm::SolidColorBrush(winrt::Windows::UI::Color{20, 128, 128, 128});
}

wuxm::Brush GetCardBorderBrush() {
    try {
        auto res = wux::Application::Current().Resources();
        if (res.HasKey(winrt::box_value(L"CardStrokeColorDefaultBrush"))) {
            auto brush =
                res.Lookup(winrt::box_value(L"CardStrokeColorDefaultBrush"))
                    .try_as<wuxm::Brush>();
            if (brush)
                return brush;
        }
        if (res.HasKey(winrt::box_value(
                L"SystemControlBackgroundBaseMediumLowBrush"))) {
            auto brush =
                res.Lookup(winrt::box_value(
                               L"SystemControlBackgroundBaseMediumLowBrush"))
                    .try_as<wuxm::Brush>();
            if (brush)
                return brush;
        }
    } catch (...) {
    }

    return wuxm::SolidColorBrush(winrt::Windows::UI::Color{15, 128, 128, 128});
}


inline std::wstring RemoveYearFromDateString(std::wstring str, int year) {
    std::wstring yearStr = std::to_wstring(year);
    size_t pos = str.find(yearStr);
    if (pos == std::wstring::npos) {
        return str;
    }

    size_t start = pos;
    size_t end = pos + yearStr.length();

    // Check for trailing characters/words associated with year in certain locales
    if (end < str.length() && (str[end] == L'年' || str[end] == L'년')) {
        end++;
    } else if (end + 1 < str.length() && str[end] == L'.' && iswspace(str[end + 1])) {
        end++;
    } else if (end + 2 < str.length() && str.substr(end, 3) == L" г.") {
        end += 3;
    } else if (end + 1 < str.length() && str.substr(end, 2) == L" г") {
        end += 2;
    } else if (end + 2 < str.length() && str.substr(end, 3) == L" m.") {
        end += 3;
    } else if (end + 1 < str.length() && str.substr(end, 2) == L" m") {
        end += 2;
    }

    bool atEnd = true;
    for (size_t i = end; i < str.length(); ++i) {
        if (!iswspace(str[i]) && str[i] != L'.' && str[i] != L',') {
            atEnd = false;
            break;
        }
    }

    if (atEnd) {
        while (start > 0 && (str[start - 1] == L' ' || str[start - 1] == L',' || str[start - 1] == L'.')) {
            start--;
        }
        str.erase(start);
    } else if (start == 0) {
        while (end < str.length() && (iswspace(str[end]) || str[end] == L'.' || str[end] == L'-')) {
            end++;
        }
        str.erase(0, end);
    } else {
        if (start >= 2 && str[start - 2] == L',' && str[start - 1] == L' ') {
            start -= 2;
        } else if (start > 0 && str[start - 1] == L' ') {
            start--;
        }
        while (end < str.length() && (str[end] == L' ' || str[end] == L',')) {
            end++;
        }
        str.erase(start, end - start);
    }

    while (!str.empty() && (str.back() == L' ' || str.back() == L',')) {
        str.pop_back();
    }
    size_t lead = 0;
    while (lead < str.length() && (str[lead] == L' ' || str[lead] == L',')) {
        lead++;
    }
    if (lead > 0) {
        str.erase(0, lead);
    }

    return str;
}

inline std::wstring FormatFilterDate(SYSTEMTIME const& st) {
    SYSTEMTIME validSt = st;
    if (validSt.wYear == 0) {
        GetLocalTime(&validSt);
    }
    wchar_t dateBuf[256]{};
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &validSt, nullptr, dateBuf, ARRAYSIZE(dateBuf), nullptr) == 0) {
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &validSt, L"dddd, MMMM d", dateBuf, ARRAYSIZE(dateBuf), nullptr);
    }
    return RemoveYearFromDateString(dateBuf, validSt.wYear);
}

inline std::wstring GetCalendarDatePickerFormat() {
    wchar_t patternBuf[128]{};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SLONGDATE, patternBuf, ARRAYSIZE(patternBuf)) == 0) {
        return L"{dayofweek.full}, {month.full} {day.integer}";
    }

    std::wstring p = patternBuf;

    size_t yPos = p.find_first_of(L"yY");
    if (yPos != std::wstring::npos) {
        size_t yEnd = p.find_last_of(L"yY") + 1;
        if (yEnd < p.length() && p[yEnd] == L'\'') {
            size_t qClose = p.find(L'\'', yEnd + 1);
            if (qClose != std::wstring::npos) {
                yEnd = qClose + 1;
            }
        }
        if (yEnd < p.length() && (p[yEnd] == L'年' || p[yEnd] == L'년')) {
            yEnd++;
        }

        bool atEnd = true;
        for (size_t i = yEnd; i < p.length(); ++i) {
            if (iswalpha(p[i])) { atEnd = false; break; }
        }

        if (atEnd) {
            size_t yStart = yPos;
            while (yStart > 0 && (p[yStart - 1] == L' ' || p[yStart - 1] == L',' || p[yStart - 1] == L'.' || p[yStart - 1] == L'\'')) {
                yStart--;
            }
            p.erase(yStart);
        } else if (yPos == 0) {
            while (yEnd < p.length() && (p[yEnd] == L' ' || p[yEnd] == L'.' || p[yEnd] == L'-')) {
                yEnd++;
            }
            p.erase(0, yEnd);
        } else {
            p.erase(yPos, yEnd - yPos);
        }
    }

    std::wstring result;
    for (size_t i = 0; i < p.length();) {
        if (p.substr(i, 4) == L"dddd") {
            result += L"{dayofweek.full}";
            i += 4;
        } else if (p.substr(i, 3) == L"ddd") {
            result += L"{dayofweek.abbreviated}";
            i += 3;
        } else if (p.substr(i, 4) == L"MMMM") {
            result += L"{month.full}";
            i += 4;
        } else if (p.substr(i, 3) == L"MMM") {
            result += L"{month.abbreviated}";
            i += 3;
        } else if (p.substr(i, 2) == L"MM") {
            result += L"{month.integer}";
            i += 2;
        } else if (p[i] == L'M') {
            result += L"{month.integer}";
            i += 1;
        } else if (p.substr(i, 2) == L"dd") {
            result += L"{day.integer}";
            i += 2;
        } else if (p[i] == L'd') {
            result += L"{day.integer}";
            i += 1;
        } else if (p[i] == L'\'') {
            i += 1;
        } else {
            result += p[i];
            i += 1;
        }
    }

    while (!result.empty() && (result.back() == L' ' || result.back() == L',')) {
        result.pop_back();
    }
    size_t lead = 0;
    while (lead < result.length() && (result[lead] == L' ' || result[lead] == L',')) {
        lead++;
    }
    if (lead > 0) {
        result.erase(0, lead);
    }

    return result.empty() ? L"{dayofweek.full}, {month.full} {day.integer}" : result;
}

namespace {
    [[clang::no_destroy]] wuxc::Grid m_rootGrid{nullptr};
    [[clang::no_destroy]] wuxc::Grid m_headerGrid{nullptr};
    [[clang::no_destroy]] wuxc::Button m_prevDayButton{nullptr};
    [[clang::no_destroy]] wuxc::Button m_nextDayButton{nullptr};
    [[clang::no_destroy]] wuxi::KeyboardAccelerator m_prevDayAccel{nullptr};
    [[clang::no_destroy]] wuxi::KeyboardAccelerator m_nextDayAccel{nullptr};
    [[clang::no_destroy]] wuxc::Button m_refreshButton{nullptr};
    [[clang::no_destroy]] wuxc::Button m_dateButton{nullptr};
    [[clang::no_destroy]] wuxc::TextBlock m_dateButtonText{nullptr};
    [[clang::no_destroy]] wuxc::CalendarView m_calendarView{nullptr};
    [[clang::no_destroy]] wuxc::CalendarDatePicker m_datePicker{nullptr};
    [[clang::no_destroy]] wuxc::TextBlock m_datePickerText{nullptr};
    winrt::event_token m_datePickerLoadedToken{};
    winrt::event_token m_dateChangedToken{};
    [[clang::no_destroy]] wuxc::ScrollViewer m_eventsScrollViewer{nullptr};
    [[clang::no_destroy]] wuxc::ItemsControl m_itemsControl{nullptr};
    [[clang::no_destroy]] wuxc::Border m_todoDetailHost{nullptr};
    ULONGLONG m_todoDetailOpenTick = 0;
    SYSTEMTIME m_currentFilterDate{};
    winrt::event_token m_calendarViewSelectionChangedToken{};
    winrt::event_token m_todoDayChangingToken{};
    winrt::event_token m_dateBtnClickToken{};
    bool m_isUpdatingCalendarSelection{false};
    winrt::event_token m_prevBtnToken{};
    winrt::event_token m_nextBtnToken{};
    winrt::event_token m_refreshBtnToken{};
    [[clang::no_destroy]] wux::FrameworkElement m_focusSessionControl{nullptr};
    int64_t m_focusSessionVisibilityToken{0};
    [[clang::no_destroy]] wuxc::ScrollViewer m_hostScrollViewer{nullptr};
    [[clang::no_destroy]] winrt::Windows::Foundation::IInspectable m_originalCalendarContent{nullptr};
    std::mutex g_pendingActionsMutex;
    [[clang::no_destroy]] std::vector<winrt::Windows::Foundation::IAsyncOperation<bool>> g_pendingDispatcherActions;

    wuxc::ScrollBarVisibility m_originalVerticalScrollBarVisibility{wuxc::ScrollBarVisibility::Auto};
    wuxc::ScrollBarVisibility m_originalHorizontalScrollBarVisibility{wuxc::ScrollBarVisibility::Disabled};
    wux::Visibility m_originalFocusSessionVisibility{wux::Visibility::Visible};
    std::atomic<DWORD> g_ownerThreadId{0};
    std::atomic<HANDLE> g_hWorkerThread{nullptr};
    std::atomic<HANDLE> g_hWorkEvent{nullptr};
    std::atomic<HANDLE> g_hStopEvent{nullptr};
    std::atomic<bool> g_workerForceFetch{false};
}

struct CoreWindowData {
    wuc::CoreWindow coreWindow{nullptr};
    winrt::event_token activatedToken{};
    winrt::event_token visibilityChangedToken{};
    wux::FrameworkElement layoutUpdatedFe{nullptr};
    winrt::event_token layoutUpdatedToken{};
};
thread_local CoreWindowData t_coreWindowData;

void UnregisterFocusSessionControl();
void PopulateItemsControl(std::vector<CalendarEvent> const& events);
void ChangeSelectedDay(int deltaDays);
void OnDatePickerDateChanged(wf::IReference<wf::DateTime> const& newDate);
void ResetDatePickerToToday();
void DispatchUpdateEvents();
void UpdateMaxHeight(int maxHeight);
void ReplaceCalendarContent(wuxc::ScrollViewer const& host);
void HandleFocusSessionControl(wux::FrameworkElement const& element);
void WalkVisualTree(winrt::Windows::UI::Xaml::DependencyObject const& root);
void RegisterCoreWindowEvents();
void UnregisterCoreWindowEvents();
void RevokeLayoutUpdated();

void RevokeLayoutUpdated() {
    if (t_coreWindowData.layoutUpdatedFe && t_coreWindowData.layoutUpdatedToken.value != 0) {
        try {
            t_coreWindowData.layoutUpdatedFe.LayoutUpdated(t_coreWindowData.layoutUpdatedToken);
        } catch (...) {}
    }
    t_coreWindowData.layoutUpdatedToken = {};
    t_coreWindowData.layoutUpdatedFe = nullptr;
}

void UnregisterFocusSessionControl() {
    if (m_focusSessionControl && m_focusSessionVisibilityToken != 0) {
        try {
            if (auto dispatcher = m_focusSessionControl.Dispatcher()) {
                if (!dispatcher.HasThreadAccess()) {
                    return;
                }
            }
            m_focusSessionControl.UnregisterPropertyChangedCallback(
                wux::UIElement::VisibilityProperty(),
                m_focusSessionVisibilityToken);
        } catch (...) {}
        m_focusSessionVisibilityToken = 0;
    }
}

void RestoreCalendarContent() {
    DWORD owner = g_ownerThreadId.load();
    if (owner != 0 && owner != GetCurrentThreadId()) {
        return;
    }

    RevokeLayoutUpdated();
    UnregisterFocusSessionControl();

    if (m_focusSessionControl) {
        try {
            m_focusSessionControl.Visibility(m_originalFocusSessionVisibility);
            Wh_Log(L"Restored FocusSessionControl visibility to original value");
        } catch (...) {}
        m_focusSessionControl = nullptr;
    }

    try {
        if (m_prevDayButton) {
            if (m_prevBtnToken.value != 0) { m_prevDayButton.Click(m_prevBtnToken); m_prevBtnToken = {}; }
            m_prevDayButton.KeyboardAccelerators().Clear();
        }
    } catch (...) {}
    try {
        if (m_nextDayButton) {
            if (m_nextBtnToken.value != 0) { m_nextDayButton.Click(m_nextBtnToken); m_nextBtnToken = {}; }
            m_nextDayButton.KeyboardAccelerators().Clear();
        }
    } catch (...) {}
    try {
        if (m_refreshButton && m_refreshBtnToken.value != 0) {
            m_refreshButton.Click(m_refreshBtnToken); m_refreshBtnToken = {};
        }
    } catch (...) {}
    try {
        if (m_dateButton && m_dateBtnClickToken.value != 0) {
            m_dateButton.Click(m_dateBtnClickToken);
            m_dateBtnClickToken = {};
        }
        if (m_calendarView && m_todoDayChangingToken.value != 0) {
            m_calendarView.CalendarViewDayItemChanging(m_todoDayChangingToken);
            m_todoDayChangingToken = {};
        }
        if (m_calendarView && m_calendarViewSelectionChangedToken.value != 0) {
            m_calendarView.SelectedDatesChanged(m_calendarViewSelectionChangedToken);
            m_calendarViewSelectionChangedToken = {};
        }
        if (m_datePicker) {
            if (m_datePickerLoadedToken.value != 0) {
                m_datePicker.Loaded(m_datePickerLoadedToken);
                m_datePickerLoadedToken = {};
            }
            if (m_dateChangedToken.value != 0) {
                m_datePicker.DateChanged(m_dateChangedToken);
                m_dateChangedToken = {};
            }
        }
    } catch (...) {}

    if (m_hostScrollViewer) {
        try {
            if (m_originalCalendarContent) {
                m_hostScrollViewer.Content(m_originalCalendarContent);
            } else if (m_hostScrollViewer.Content() == m_rootGrid) {
                m_hostScrollViewer.Content(nullptr);
            }
            m_hostScrollViewer.VerticalScrollBarVisibility(m_originalVerticalScrollBarVisibility);
            m_hostScrollViewer.HorizontalScrollBarVisibility(m_originalHorizontalScrollBarVisibility);
        } catch (...) {}
        m_originalCalendarContent = nullptr;
        m_hostScrollViewer = nullptr;
    }

    if (m_rootGrid) {
        try { m_rootGrid.Children().Clear(); } catch (...) {}
        m_rootGrid = nullptr;
    }
    m_todoDetailHost = nullptr;
    m_todoDetailOpenTick = 0;
    {
        std::lock_guard<std::mutex> lock(g_watcherMutex);
        if (m_itemsControl) {
            try { m_itemsControl.Items().Clear(); } catch (...) {}
            m_itemsControl = nullptr;
        }
    }
    m_eventsScrollViewer = nullptr;
    m_headerGrid = nullptr;
    m_prevDayButton = nullptr;
    m_nextDayButton = nullptr;
    m_refreshButton = nullptr;
    m_dateButton = nullptr;
    m_dateButtonText = nullptr;
    m_calendarView = nullptr;
    m_datePicker = nullptr;
    m_datePickerText = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_watcherMutex);
        m_prevDayAccel = nullptr;
        m_nextDayAccel = nullptr;
    }
    g_ownerThreadId.store(0);
    g_lastOpenTick = 0;
}


// ----------------- Native Todo (separate detail panel + official holiday dates) -----------------
// Windhawk storage: v2 fields are tab-separated and %-escaped; the old v1
// storage key is only read for migration. No network access is used by Todo.
struct NativeCategory {
    std::wstring id;
    std::wstring name;
    std::wstring hex;
};
struct NativeTodo {
    std::wstring day;
    bool done = false;
    std::wstring categoryId;
    std::wstring title;
    std::wstring time;
    std::wstring location;
    std::wstring url;
    std::wstring notes;
};
std::wstring TodoDayKey(SYSTEMTIME const& t) { return FormatDateYmd(t); }
std::wstring TodoEscape(std::wstring s) {
    std::wstring out;
    for (wchar_t ch : s) {
        if (ch == L'%') out += L"%25";
        else if (ch == L'\t') out += L"%09";
        else if (ch == L'\n') out += L"%0A";
        else if (ch == L'\r') out += L"%0D";
        else out += ch;
    }
    return out;
}
std::wstring TodoUnescape(std::wstring const& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'%' && i + 2 < s.size()) {
            wchar_t a = towupper(s[i + 1]), b = towupper(s[i + 2]);
            if (a == L'2' && b == L'5') { out += L'%'; i += 2; continue; }
            if (a == L'0' && b == L'9') { out += L'\t'; i += 2; continue; }
            if (a == L'0' && b == L'A') { out += L'\n'; i += 2; continue; }
            if (a == L'0' && b == L'D') { out += L'\r'; i += 2; continue; }
        }
        out += s[i];
    }
    return out;
}
std::vector<std::wstring> TodoSplit(std::wstring const& s) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(L'\t', start);
        if (pos == std::wstring::npos) { parts.push_back(TodoUnescape(s.substr(start))); break; }
        parts.push_back(TodoUnescape(s.substr(start, pos - start)));
        start = pos + 1;
    }
    return parts;
}
bool TodoHexColor(std::wstring const& text, winrt::Windows::UI::Color& color) {
    if (text.size() != 7 || text[0] != L'#') return false;
    unsigned value = 0;
    for (size_t i = 1; i < 7; ++i) {
        wchar_t c = towupper(text[i]);
        int digit = (c >= L'0' && c <= L'9') ? c - L'0' :
                    (c >= L'A' && c <= L'F') ? c - L'A' + 10 : -1;
        if (digit < 0) return false;
        value = (value << 4) | static_cast<unsigned>(digit);
    }
    color = winrt::Windows::UI::Color{255,
        static_cast<uint8_t>((value >> 16) & 255),
        static_cast<uint8_t>((value >> 8) & 255),
        static_cast<uint8_t>(value & 255)};
    return true;
}
std::vector<NativeCategory> TodoDefaultCategories() {
    return {{L"work", L"工作", L"#2563EB"},
            {L"important", L"重要", L"#EA580C"},
            {L"personal", L"个人", L"#059669"}};
}
std::vector<NativeCategory> TodoCategories() {
    wchar_t buf[32768]{};
    Wh_GetStringValue(L"todo-categories-v2", buf, ARRAYSIZE(buf));
    if (buf[0] == L'\0') return TodoDefaultCategories();
    std::wistringstream is(buf);
    std::wstring line;
    std::vector<NativeCategory> out;
    while (std::getline(is, line)) {
        auto p = TodoSplit(line);
        if (p.size() != 3 || p[0].empty() || p[1].empty()) continue;
        winrt::Windows::UI::Color temp{};
        if (!TodoHexColor(p[2], temp)) continue;
        out.push_back({p[0], p[1], p[2]});
    }
    return out.empty() ? TodoDefaultCategories() : out;
}
bool TodoSaveCategories(std::vector<NativeCategory> const& categories) {
    if (categories.empty() || categories.size() > 24) return false;
    std::wstring result;
    for (auto const& c : categories)
        result += TodoEscape(c.id) + L"\t" + TodoEscape(c.name) +
                  L"\t" + TodoEscape(c.hex) + L"\n";
    return result.size() < 32767 &&
           Wh_SetStringValue(L"todo-categories-v2", result.c_str()) != FALSE;
}
std::vector<NativeTodo> TodoLoad() {
    wchar_t buf[65536]{};
    Wh_GetStringValue(L"todo-tasks-v2", buf, ARRAYSIZE(buf));
    std::vector<NativeTodo> out;
    if (std::wstring_view(buf).starts_with(L"#v2\n")) {
        std::wistringstream is(buf + 4);
        std::wstring line;
        while (std::getline(is, line)) {
            auto p = TodoSplit(line);
            if (p.size() != 8 || p[0].size() != 8 || p[3].empty()) continue;
            out.push_back({p[0], p[1] == L"1", p[2], p[3], p[4], p[5], p[6], p[7]});
        }
        return out;
    }
    // Read-only fallback to v0.1 so upgrading does not lose existing tasks.
    wchar_t old[32768]{};
    Wh_GetStringValue(L"todo-tasks-v1", old, ARRAYSIZE(old));
    std::wistringstream is(old);
    std::wstring line;
    while (std::getline(is, line)) {
        auto p = TodoSplit(line);
        if (p.size() != 4 || p[0].size() != 8 || p[3].empty()) continue;
        std::wstring cat = p[2] == L"1" ? L"important" :
                           p[2] == L"2" ? L"personal" : L"work";
        out.push_back({p[0], p[1] == L"1", cat, p[3], L"", L"", L"", L""});
    }
    return out;
}
bool TodoSave(std::vector<NativeTodo> const& tasks) {
    std::wstring s = L"#v2\n";
    for (auto const& t : tasks) {
        s += TodoEscape(t.day) + L"\t" + (t.done ? L"1" : L"0") +
             L"\t" + TodoEscape(t.categoryId) + L"\t" + TodoEscape(t.title) +
             L"\t" + TodoEscape(t.time) + L"\t" + TodoEscape(t.location) +
             L"\t" + TodoEscape(t.url) + L"\t" + TodoEscape(t.notes) + L"\n";
    }
    if (s.size() >= 65535) return false;
    return Wh_SetStringValue(L"todo-tasks-v2", s.c_str()) != FALSE;
}
NativeCategory TodoGetCategory(std::wstring const& id) {
    for (auto const& c : TodoCategories()) if (c.id == id) return c;
    return {L"unknown", L"未分类", L"#909090"};
}
winrt::Windows::UI::Color TodoColor(std::wstring const& id, bool done = false) {
    winrt::Windows::UI::Color c{255, 144, 144, 144};
    if (!done) TodoHexColor(TodoGetCategory(id).hex, c);
    return c;
}
SYSTEMTIME TodoLocalDate(wf::DateTime dt) {
    auto ticks = static_cast<ULONGLONG>(dt.time_since_epoch().count());
    ULARGE_INTEGER ui{}; ui.QuadPart = ticks;
    FILETIME ft{ui.LowPart, ui.HighPart};
    SYSTEMTIME utc{}, local{};
    if (FileTimeToSystemTime(&ft, &utc) &&
        SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return local;
    return utc;
}
bool TodoIsWorkday(std::wstring const& name) {
    return name.find(L"补班") != std::wstring::npos ||
           name.find(L"上班") != std::wstring::npos ||
           name.find(L"调休上班") != std::wstring::npos;
}
bool TodoIsHighlightedHoliday(std::wstring const& name) {
    // Only official holiday names and makeup workdays; no general festivals.
    for (auto const& key : {L"元旦", L"春节", L"清明", L"劳动节",
                            L"端午", L"中秋", L"国庆", L"放假", L"补班", L"上班"}) {
        if (name.find(key) != std::wstring::npos) return true;
    }
    return false;
}

struct TodoHolidayInfo {
    std::wstring name;
    bool holiday = false;
    bool workday = false;
};
std::unordered_map<std::wstring, TodoHolidayInfo> g_todoHolidayMemo;

TodoHolidayInfo TodoGetHolidayInfo(SYSTEMTIME const& date) {
    auto key = TodoDayKey(date);
    std::lock_guard<std::mutex> lock(g_eventsMutex);
    auto found = g_todoHolidayMemo.find(key);
    if (found != g_todoHolidayMemo.end()) return found->second;
    TodoHolidayInfo result;
    auto imported = FilterEventsForDate(g_allParsedEvents, date);
    for (auto const& ev : imported) {
        if (!ev.isAllDay || !TodoIsHighlightedHoliday(ev.name)) continue;
        if (!result.name.empty()) result.name += L"、";
        result.name += ev.name;
        bool workday = TodoIsWorkday(ev.name);
        result.workday = result.workday || workday;
        result.holiday = result.holiday || !workday;
    }
    g_todoHolidayMemo.emplace(key, result);
    return result;
}

// Reapply special date colors after CalendarView changes its PointerOver
// visual state.  Native CalendarView may overwrite Foreground on hover.
void TodoPaintHolidayText(wuxc::CalendarViewDayItem const& item,
                          std::wstring const& expectedDay,
                          bool holiday, bool workday) {
    try {
        if (!item || TodoDayKey(TodoLocalDate(item.Date())) != expectedDay) return;
        bool specialDay = holiday || workday;
        wuxm::SolidColorBrush brush{nullptr};
        if (specialDay) {
            winrt::Windows::UI::Color c{};
            TodoHexColor(workday ? L"#0070C0" : L"#B42336", c);
            brush = wuxm::SolidColorBrush(c);
            item.Foreground(brush);
        } else {
            item.ClearValue(wuxc::Control::ForegroundProperty());
        }
        std::wstring number = std::to_wstring(TodoLocalDate(item.Date()).wDay);
        std::vector<wux::DependencyObject> nodes{item};
        while (!nodes.empty()) {
            auto node = nodes.back();
            nodes.pop_back();
            if (auto tb = node.try_as<wuxc::TextBlock>()) {
                if (std::wstring(tb.Text().c_str()) == number) {
                    if (specialDay) tb.Foreground(brush);
                    else tb.ClearValue(wuxc::TextBlock::ForegroundProperty());
                    break;
                }
            }
            int count = wuxm::VisualTreeHelper::GetChildrenCount(node);
            for (int i = count - 1; i >= 0; --i)
                nodes.push_back(wuxm::VisualTreeHelper::GetChild(node, i));
        }
    } catch (...) {}
}

void TodoQueueHolidayRepaint(wuxc::CalendarViewDayItem const& item) {
    if (!item) return;
    try {
        auto weak = winrt::make_weak(item);
        if (auto dispatcher = item.Dispatcher()) {
            dispatcher.TryRunAsync(wuc::CoreDispatcherPriority::Low, [weak]() {
                if (auto current = weak.get()) {
                    auto date = TodoLocalDate(current.Date());
                    auto info = TodoGetHolidayInfo(date);
                    TodoPaintHolidayText(current, TodoDayKey(date),
                                         info.holiday, info.workday);
                }
            });
        }
    } catch (...) {}
}

void TodoEnsureHoverRepaint(wuxc::CalendarViewDayItem const& item) {
    try {
        // This tag is only used on day items created by our own CalendarView.
        auto tag = item.Tag();
        if (tag) {
            try {
                auto value = winrt::unbox_value<winrt::hstring>(tag);
                if (value == L"NativeTodoHoliday044.Hover") return;
            } catch (...) {}
        }
        item.Tag(winrt::box_value(winrt::hstring(L"NativeTodoHoliday044.Hover")));
        auto weak = winrt::make_weak(item);
        auto repaint = [weak](wf::IInspectable const&,
                              wuxi::PointerRoutedEventArgs const&) {
            if (auto current = weak.get()) TodoQueueHolidayRepaint(current);
        };
        item.PointerEntered(repaint);
        item.PointerExited(repaint);
    } catch (...) {}
}

void TodoDrawMarker(wuxc::CalendarViewDayItem const& item) {
    if (!item) return;
    TodoEnsureHoverRepaint(item);
    try {
        auto targetDate = TodoLocalDate(item.Date());
        auto day = TodoDayKey(targetDate);
        std::vector<winrt::Windows::UI::Color> colors;
        for (auto const& task : TodoLoad()) {
            if (task.day == day)
                colors.push_back(TodoColor(task.categoryId, task.done));
        }
        auto holidayInfo = TodoGetHolidayInfo(targetDate);
        std::wstring holidayName = holidayInfo.name;
        bool isWorkday = holidayInfo.workday;
        bool isHoliday = holidayInfo.holiday;
        // Restore reliable built-in density bars, but only for local tasks.
        // Square corners require a custom CalendarViewDayItem control template.
        auto nativeColors = winrt::single_threaded_vector<winrt::Windows::UI::Color>();
        // Local saved TODOs ONLY. Imported ICS holidays never produce task bars.
        for (size_t i = 0; i < colors.size() && i < 10; ++i) {
            nativeColors.Append(colors[i]);
        }
        item.SetDensityColors(nativeColors);
        if (auto dispatcher = item.Dispatcher()) {
            dispatcher.TryRunAsync(wuc::CoreDispatcherPriority::Low,
                 [item, day, holidayName, isWorkday, isHoliday]() {
                try {
                    // Date item can be reused for another day while async is pending.
                    if (TodoDayKey(TodoLocalDate(item.Date())) != day) return;
                    // NativeTodoHoliday044: apply a single shared painter.
                    TodoPaintHolidayText(item, day, isHoliday, isWorkday);
                    if (!holidayName.empty()) {
                        wuxc::ToolTipService::SetToolTip(item, winrt::box_value(winrt::hstring(holidayName)));
                    } else {
                        wuxc::ToolTipService::SetToolTip(item, nullptr);
                    }
                } catch (...) {}
            });
        }
    } catch (...) {}
}
void TodoRefreshMarkers() {
    if (!m_calendarView) return;
    try {
        std::vector<wux::DependencyObject> nodes{m_calendarView};
        while (!nodes.empty()) {
            auto el = nodes.back(); nodes.pop_back();
            if (auto day = el.try_as<wuxc::CalendarViewDayItem>()) TodoDrawMarker(day);
            int count = wuxm::VisualTreeHelper::GetChildrenCount(el);
            for (int i = 0; i < count; ++i)
                nodes.push_back(wuxm::VisualTreeHelper::GetChild(el, i));
        }
    } catch (...) {}
}
void TodoRefresh() {
    std::vector<CalendarEvent> selectedEvents;
    {
        std::lock_guard<std::mutex> lock(g_eventsMutex);
        selectedEvents = FilterEventsForDate(g_allParsedEvents, m_currentFilterDate);
    }
    PopulateItemsControl(selectedEvents);
}
void TodoUpdateAt(size_t index, bool erase, bool completed) {
    auto tasks = TodoLoad();
    if (index >= tasks.size()) return;
    if (erase) tasks.erase(tasks.begin() + index);
    else tasks[index].done = completed;
    if (TodoSave(tasks)) TodoRefresh();
}
double TodoScreenHeightDip() {
    UINT dpi = GetDpiForSystem();
    if (dpi < 96) dpi = 96;
    return static_cast<double>(GetSystemMetrics(SM_CYFULLSCREEN)) * 96.0 / dpi;
}
double TodoTaskMaxHeight() {
    double calHeight = 380;
    try {
        if (m_calendarView && m_calendarView.ActualHeight() > 200)
            calHeight = m_calendarView.ActualHeight();
    } catch (...) {}
    // Space for calendar, taskbar, date heading, flyout padding and margins.
    double extra = (m_todoDetailHost &&
        m_todoDetailHost.Visibility() == wux::Visibility::Visible) ? 300.0 : 0.0;
    return std::clamp(TodoScreenHeightDip() - calHeight - 155.0 - extra, 105.0, 820.0);
}
void TodoResizeTaskList() {
    if (!m_eventsScrollViewer) return;
    try {
        m_eventsScrollViewer.MaxHeight(TodoTaskMaxHeight());
        m_eventsScrollViewer.Height(std::numeric_limits<double>::quiet_NaN());
    } catch (...) {}
}
wuxc::TextBlock TodoText(std::wstring const& content, int fontSize = 13) {
    auto t = wuxc::TextBlock();
    t.Text(winrt::hstring(content));
    t.FontSize(fontSize);
    t.TextWrapping(wux::TextWrapping::Wrap);
    return t;
}
wuxc::TextBox TodoField(wuxc::StackPanel const& box,
                        std::wstring const& label, std::wstring const& value,
                        bool multiline = false) {
    auto t = TodoText(label, 12);
    t.Margin(wux::Thickness{0, 4, 0, 2});
    t.Opacity(0.8);
    box.Children().Append(t);
    auto input = wuxc::TextBox();
    input.Text(winrt::hstring(value));
    input.MinWidth(0);
    if (multiline) {
        input.AcceptsReturn(true);
        input.TextWrapping(wux::TextWrapping::Wrap);
        input.MinHeight(62);
        input.MaxHeight(200);
        // The notes editor grows for short text; internal scroll only at 200 DIPs.
        auto autoSize = [](wuxc::TextBox const& tb) {
            std::wstring s = tb.Text().c_str();
            int lines = 1, cols = 0;
            for (wchar_t ch : s) {
                if (ch == L'\n') { ++lines; cols = 0; }
                else if (ch != L'\r' && ++cols >= 41) { ++lines; cols = 0; }
            }
            tb.Height(static_cast<double>(std::clamp(24 + lines * 22, 62, 200)));
        };
        input.TextChanged([autoSize](wf::IInspectable const& sender,
                                     wuxc::TextChangedEventArgs const&) {
            autoSize(sender.as<wuxc::TextBox>());
        });
        autoSize(input);
    }
    box.Children().Append(input);
    return input;
}
bool TodoValidDay(std::wstring const& d) {
    if (d.size() != 8) return false;
    for (wchar_t c : d) if (c < L'0' || c > L'9') return false;
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(std::stoi(d.substr(0, 4)));
    st.wMonth = static_cast<WORD>(std::stoi(d.substr(4, 2)));
    st.wDay = static_cast<WORD>(std::stoi(d.substr(6, 2)));
    FILETIME ft{};
    return SystemTimeToFileTime(&st, &ft) != FALSE;
}
std::wstring TodoCleanTitle(std::wstring s) {
    for (auto& ch : s) if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
    if (s.find_first_not_of(L" \t") == std::wstring::npos) return L"";
    return s;
}
void TodoHideDetail() {
    if (!m_todoDetailHost) return;
    try {
        m_todoDetailHost.Visibility(wux::Visibility::Collapsed);
        m_todoDetailHost.Child(nullptr);
        TodoResizeTaskList();
    } catch (...) {}
}
void TodoShowDetailPanel(wuxc::StackPanel const& panel) {
    if (!m_todoDetailHost) return;
    try {
        auto scroll = wuxc::ScrollViewer();
        scroll.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Auto);
        scroll.HorizontalScrollBarVisibility(wuxc::ScrollBarVisibility::Disabled);
        scroll.MaxHeight(std::clamp(TodoScreenHeightDip() * 0.36, 210.0, 420.0));
        scroll.Content(panel);
        m_todoDetailHost.Child(scroll);
        m_todoDetailHost.Visibility(wux::Visibility::Visible);
        m_todoDetailOpenTick = GetTickCount64();
        TodoResizeTaskList();
    } catch (...) { Wh_Log(L"NativeTodo: detail panel display failed"); }
}
double TodoInlineEditorWidth() {
    // The inline editor is measured by the calendar panel, not another HWND.
    // Screen-size growth now happens vertically inside the existing ScrollViewer.
    double available = m_itemsControl ? m_itemsControl.ActualWidth() : 0.0;
    return std::clamp(available >= 320.0 ? available - 18.0 : 410.0,
                      280.0, 455.0);
}
void TodoShowEditor(wux::FrameworkElement const& anchor, size_t editingIndex,
                    std::wstring const& draft = L"") {
    if (!anchor) return;
    try {
        auto tasks = TodoLoad();
        bool isEdit = editingIndex < tasks.size();
        NativeTodo item;
        if (isEdit) item = tasks[editingIndex];
        else {
            item.day = TodoDayKey(m_currentFilterDate);
            item.title = draft;
            auto cats = TodoCategories();
            item.categoryId = cats.empty() ? L"work" : cats.front().id;
        }
        auto panel = wuxc::StackPanel();
        panel.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        panel.Margin(wux::Thickness{8, 6, 8, 6});
        panel.Children().Append(TodoText(isEdit ? L"编辑待办 · 详细信息" : L"添加待办 · 详细信息", 17));
        auto title = TodoField(panel, L"标题 *", item.title);
        auto day = TodoField(panel, L"日期（YYYYMMDD）", item.day);
        auto time = TodoField(panel, L"时间（例如 14:30–15:30）", item.time);
        auto location = TodoField(panel, L"地点 / 会议平台", item.location);
        auto url = TodoField(panel, L"会议链接 / URL", item.url);
        auto notes = TodoField(panel, L"备注 / 参会人员 / 议程", item.notes, true);

        panel.Children().Append(TodoText(L"分类", 12));
        auto cats = TodoCategories();
        auto category = wuxc::ComboBox();
        int initial = 0;
        for (size_t i = 0; i < cats.size(); ++i) {
            category.Items().Append(winrt::box_value(winrt::hstring(cats[i].name)));
            if (cats[i].id == item.categoryId) initial = static_cast<int>(i);
        }
        category.SelectedIndex(initial);
        category.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        panel.Children().Append(category);

        if (isEdit && !item.url.empty()) {
            std::wstring link = item.url;
            if (link.starts_with(L"https://") || link.starts_with(L"http://")) {
                try {
                    auto open = wuxc::HyperlinkButton();
                    open.Content(winrt::box_value(L"在浏览器打开会议链接 ↗"));
                    open.NavigateUri(wf::Uri(winrt::hstring(link)));
                    open.Margin(wux::Thickness{0, 7, 0, 0});
                    panel.Children().Append(open);
                } catch (...) {}
            }
        }
        auto actions = wuxc::StackPanel();
        actions.Orientation(wuxc::Orientation::Horizontal);
        actions.Margin(wux::Thickness{0, 12, 0, 0});
        auto save = wuxc::Button();
        save.Content(winrt::box_value(isEdit ? L"保存修改" : L"创建待办"));
        save.MinWidth(110);
        save.Click([editingIndex, isEdit, item, title, day, time, location, url, notes, category, cats]
                   (wf::IInspectable const&, wux::RoutedEventArgs const&) {
            auto name = TodoCleanTitle(std::wstring(title.Text().c_str()));
            if (name.empty()) { title.Focus(wux::FocusState::Programmatic); return; }
            auto selectedDate = std::wstring(day.Text().c_str());
            if (!TodoValidDay(selectedDate)) { day.Focus(wux::FocusState::Programmatic); return; }
            auto all = TodoLoad();
            NativeTodo updated = item;
            updated.title = name;
            updated.day = selectedDate;
            updated.time = time.Text().c_str();
            updated.location = location.Text().c_str();
            updated.url = url.Text().c_str();
            updated.notes = notes.Text().c_str();
            int selected = category.SelectedIndex();
            if (selected >= 0 && static_cast<size_t>(selected) < cats.size())
                updated.categoryId = cats[selected].id;
            if (isEdit && editingIndex < all.size()) all[editingIndex] = updated;
            else all.push_back(std::move(updated));
            if (!TodoSave(all)) { Wh_Log(L"NativeTodo: task storage limit reached"); return; }
            TodoHideDetail();
            TodoRefresh();
        });
        actions.Children().Append(save);
        auto cancel = wuxc::Button();
        cancel.Content(winrt::box_value(L"取消"));
        cancel.Margin(wux::Thickness{8, 0, 0, 0});
        cancel.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) { TodoHideDetail(); });
        actions.Children().Append(cancel);
        panel.Children().Append(actions);
        TodoShowDetailPanel(panel);
        title.Focus(wux::FocusState::Programmatic);
    } catch (...) { Wh_Log(L"NativeTodo: unable to open task editor"); }
}
void TodoShowCategoryManager(wux::FrameworkElement const& anchor) {
    if (!anchor) return;
    try {
        auto panel = wuxc::StackPanel();
        panel.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        panel.Margin(wux::Thickness{8, 6, 8, 6});
        panel.Children().Append(TodoText(L"管理待办分类", 17));
        panel.Children().Append(TodoText(L"可编辑名称和颜色（#RRGGBB）；保存后立即生效。", 12));
        auto cats = TodoCategories();
        for (size_t idx = 0; idx < cats.size(); ++idx) {
            auto group = wuxc::StackPanel();
            group.Margin(wux::Thickness{0, 8, 0, 7});
            auto inputRow = wuxc::StackPanel();
            inputRow.Orientation(wuxc::Orientation::Horizontal);
            auto mark = wuxc::Border();
            mark.Width(5); mark.Height(30);
            mark.CornerRadius(wux::CornerRadius{3, 3, 3, 3});
            mark.Background(wuxm::SolidColorBrush(TodoColor(cats[idx].id)));
            inputRow.Children().Append(mark);
            auto name = wuxc::TextBox();
            name.Text(winrt::hstring(cats[idx].name));
            name.Width(138);
            name.Margin(wux::Thickness{5, 0, 5, 0});
            inputRow.Children().Append(name);
            auto hex = wuxc::TextBox();
            hex.Text(winrt::hstring(cats[idx].hex));
            hex.Width(92);
            inputRow.Children().Append(hex);
            group.Children().Append(inputRow);
            auto actions = wuxc::StackPanel();
            actions.Orientation(wuxc::Orientation::Horizontal);
            actions.Margin(wux::Thickness{0, 3, 0, 0});
            auto save = wuxc::Button();
            save.Content(winrt::box_value(L"保存"));
            save.Click([idx, name, hex](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                auto list = TodoCategories();
                if (idx >= list.size()) return;
                winrt::Windows::UI::Color color{};
                std::wstring n = TodoCleanTitle(std::wstring(name.Text().c_str()));
                std::wstring h = hex.Text().c_str();
                if (n.empty() || !TodoHexColor(h, color)) { hex.Focus(wux::FocusState::Programmatic); return; }
                list[idx].name = n; list[idx].hex = h;
                if (!TodoSaveCategories(list)) return;
                TodoHideDetail(); TodoRefresh();
            });
            actions.Children().Append(save);
            if (cats.size() > 1) {
                auto remove = wuxc::Button();
                remove.Content(winrt::box_value(L"删除分类"));
                remove.Margin(wux::Thickness{6, 0, 0, 0});
                remove.Click([idx](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                    auto list = TodoCategories();
                    if (idx >= list.size() || list.size() <= 1) return;
                    std::wstring removedId = list[idx].id;
                    list.erase(list.begin() + idx);
                    auto tasks = TodoLoad();
                    for (auto& t : tasks) if (t.categoryId == removedId) t.categoryId = list.front().id;
                    if (!TodoSave(tasks) || !TodoSaveCategories(list)) return;
                    TodoHideDetail(); TodoRefresh();
                });
                actions.Children().Append(remove);
            }
            group.Children().Append(actions);
            auto presets = wuxc::StackPanel();
            presets.Orientation(wuxc::Orientation::Horizontal);
            presets.Margin(wux::Thickness{0, 4, 0, 0});
            for (auto const& preset : {L"#2563EB", L"#EA580C", L"#059669", L"#7C3AED", L"#DB2777", L"#0891B2", L"#B45309"}) {
                auto sw = wuxc::Button();
                sw.Width(26); sw.Height(23);
                sw.Margin(wux::Thickness{1, 0, 2, 0});
                winrt::Windows::UI::Color col{};
                TodoHexColor(preset, col);
                sw.Background(wuxm::SolidColorBrush(col));
                sw.Click([hex, preset](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                    hex.Text(preset);
                });
                presets.Children().Append(sw);
            }
            group.Children().Append(presets);
            panel.Children().Append(group);
        }
        panel.Children().Append(TodoText(L"新建分类", 14));
        auto newName = TodoField(panel, L"名称", L"");
        auto newHex = TodoField(panel, L"颜色（支持十六进制）", L"#7C3AED");
        auto swatches = wuxc::StackPanel();
        swatches.Orientation(wuxc::Orientation::Horizontal);
        swatches.Margin(wux::Thickness{0, 5, 0, 5});
        for (auto const& colorHex : {L"#2563EB", L"#EA580C", L"#059669", L"#7C3AED", L"#DB2777", L"#0891B2", L"#B45309"}) {
            auto swatch = wuxc::Button();
            swatch.Width(27); swatch.Height(25);
            swatch.Margin(wux::Thickness{1, 0, 2, 0});
            winrt::Windows::UI::Color c{};
            TodoHexColor(colorHex, c);
            swatch.Background(wuxm::SolidColorBrush(c));
            wuxc::ToolTipService::SetToolTip(swatch, winrt::box_value(winrt::hstring(colorHex)));
            swatch.Click([newHex, colorHex](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                newHex.Text(colorHex);
            });
            swatches.Children().Append(swatch);
        }
        panel.Children().Append(swatches);
        auto create = wuxc::Button();
        create.Content(winrt::box_value(L"＋ 创建分类"));
        create.Click([newName, newHex](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            auto list = TodoCategories();
            std::wstring n = TodoCleanTitle(std::wstring(newName.Text().c_str()));
            std::wstring h = newHex.Text().c_str();
            winrt::Windows::UI::Color c{};
            if (n.empty() || !TodoHexColor(h, c) || list.size() >= 24) return;
            // An id must remain stable after renaming the category.
            auto id = L"custom-" + std::to_wstring(GetTickCount64()) +
                      L"-" + std::to_wstring(list.size());
            list.push_back({id, n, h});
            if (!TodoSaveCategories(list)) return;
            TodoHideDetail(); TodoRefresh();
        });
        panel.Children().Append(create);
        TodoShowDetailPanel(panel);
    } catch (...) { Wh_Log(L"NativeTodo: unable to open category manager"); }
}
void PopulateItemsControl(std::vector<CalendarEvent> const& events) {
    if (!m_itemsControl) return;
    try {
        m_itemsControl.Items().Clear();
        auto tasks = TodoLoad();
        auto selectedDay = TodoDayKey(m_currentFilterDate);
        int unfinished = 0;
        for (auto const& t : tasks) if (t.day == selectedDay && !t.done) ++unfinished;
        auto heading = TodoText(L"待办事项 · " + std::to_wstring(unfinished) + L" 项未完成", 17);
        heading.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        heading.Margin(wux::Thickness{0, 4, 0, 10});
        m_itemsControl.Items().Append(heading);
        // NativeTodoHoliday042: separate read-only holiday notice for selected day.
        // Imported events never become editable todos or todo density marks.
        std::wstring holidayLine;
        {
            std::lock_guard<std::mutex> lock(g_eventsMutex);
            auto holidayEvents = FilterEventsForDate(g_allParsedEvents, m_currentFilterDate);
            for (auto const& ev : holidayEvents) {
                if (!ev.isAllDay || !TodoIsHighlightedHoliday(ev.name)) continue;
                if (!holidayLine.empty()) holidayLine += L"  ·  ";
                holidayLine += ev.name;
            }
        }
        if (!holidayLine.empty()) {
            auto holidayText = TodoText(L"节假日  ·  " + holidayLine, 12);
            holidayText.TextWrapping(wux::TextWrapping::Wrap);
            holidayText.Margin(wux::Thickness{3, 0, 3, 9});
            winrt::Windows::UI::Color holidayColor{};
            TodoHexColor(holidayLine.find(L"补班") != std::wstring::npos
                        ? L"#0070C0" : L"#B42336", holidayColor);
            holidayText.Foreground(wuxm::SolidColorBrush(holidayColor));
            m_itemsControl.Items().Append(holidayText);
        }

        bool any = false;
        for (size_t i = 0; i < tasks.size(); ++i) {
            auto const& t = tasks[i];
            if (t.day != selectedDay) continue;
            any = true;
            auto row = wuxc::Grid();
            wuxc::ColumnDefinition a{}; a.Width(wux::GridLength{3, wux::GridUnitType::Pixel});
            wuxc::ColumnDefinition b{}; b.Width(wux::GridLength{0, wux::GridUnitType::Auto});
            wuxc::ColumnDefinition c{}; c.Width(wux::GridLength{1, wux::GridUnitType::Star});
            wuxc::ColumnDefinition d{}; d.Width(wux::GridLength{0, wux::GridUnitType::Auto});
            row.ColumnDefinitions().Append(a); row.ColumnDefinitions().Append(b);
            row.ColumnDefinitions().Append(c); row.ColumnDefinitions().Append(d);
            auto stripe = wuxc::Border();
            stripe.Background(wuxm::SolidColorBrush(TodoColor(t.categoryId, t.done)));
            stripe.CornerRadius(wux::CornerRadius{2, 2, 2, 2});
            wuxc::Grid::SetColumn(stripe, 0); row.Children().Append(stripe);
            auto check = wuxc::CheckBox();
            check.IsChecked(winrt::box_value(t.done).as<wf::IReference<bool>>());
            check.MinWidth(0); check.Margin(wux::Thickness{4, 0, 0, 0});
            wuxc::Grid::SetColumn(check, 1);
            check.Checked([i](wf::IInspectable const&, wux::RoutedEventArgs const&) { TodoUpdateAt(i, false, true); });
            check.Unchecked([i](wf::IInspectable const&, wux::RoutedEventArgs const&) { TodoUpdateAt(i, false, false); });
            row.Children().Append(check);
            auto info = wuxc::Button();
            info.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
            info.HorizontalContentAlignment(wux::HorizontalAlignment::Left);
            info.Background(wuxm::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            info.BorderThickness(wux::Thickness{0, 0, 0, 0});
            info.Padding(wux::Thickness{0, 3, 0, 3});
            auto infoText = wuxc::StackPanel();
            auto title = TodoText(t.title, 14);
            if (t.done) title.Opacity(0.55);
            infoText.Children().Append(title);
            std::wstring meta = TodoGetCategory(t.categoryId).name;
            if (!t.time.empty()) meta += L" · " + t.time;
            if (!t.location.empty()) meta += L" · " + t.location;
            auto metaText = TodoText(meta, 11);
            metaText.Opacity(0.65);
            infoText.Children().Append(metaText);
            info.Content(infoText);
            wuxc::ToolTipService::SetToolTip(info, winrt::box_value(L"点击查看 / 修改详情"));
            info.Click([i, info](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                TodoShowEditor(info, i);
            });
            wuxc::Grid::SetColumn(info, 2); row.Children().Append(info);
            auto remove = wuxc::Button();
            remove.Content(winrt::box_value(L"×"));
            remove.MinWidth(29); remove.Padding(wux::Thickness{3, 0, 3, 0});
            remove.Margin(wux::Thickness{5, 0, 0, 0});
            wuxc::ToolTipService::SetToolTip(remove, winrt::box_value(L"删除待办"));
            remove.Click([i](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                TodoUpdateAt(i, true, false);
            });
            wuxc::Grid::SetColumn(remove, 3); row.Children().Append(remove);
            auto card = wuxc::Border();
            card.Padding(wux::Thickness{8, 4, 8, 4});
            card.Margin(wux::Thickness{0, 0, 0, 6});
            card.CornerRadius(GetStandardCornerRadius());
            card.Background(GetCardBackgroundBrush());
            card.Child(row);
            m_itemsControl.Items().Append(card);
        }
        if (!any) {
            auto empty = TodoText(L"这一天没有待办事项", 13);
            empty.Opacity(0.65);
            empty.Margin(wux::Thickness{4, 6, 0, 12});
            m_itemsControl.Items().Append(empty);
        }

        auto inputRow = wuxc::Grid();
        inputRow.Margin(wux::Thickness{0, 6, 0, 8});
        wuxc::ColumnDefinition a{}; a.Width(wux::GridLength{1, wux::GridUnitType::Star});
        wuxc::ColumnDefinition b{}; b.Width(wux::GridLength{0, wux::GridUnitType::Auto});
        wuxc::ColumnDefinition c{}; c.Width(wux::GridLength{0, wux::GridUnitType::Auto});
        inputRow.ColumnDefinitions().Append(a); inputRow.ColumnDefinitions().Append(b);
        inputRow.ColumnDefinitions().Append(c);
        auto draft = wuxc::TextBox();
        draft.PlaceholderText(L"新待办标题（点击＋补充详情）");
        draft.MinWidth(0);
        wuxc::Grid::SetColumn(draft, 0); inputRow.Children().Append(draft);
        auto add = wuxc::Button();
        add.Content(winrt::box_value(L"＋"));
        add.MinWidth(32);
        add.Margin(wux::Thickness{6, 0, 0, 0});
        wuxc::ToolTipService::SetToolTip(add, winrt::box_value(L"创建详细待办"));
        add.Click([add, draft](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            TodoShowEditor(add, static_cast<size_t>(-1), std::wstring(draft.Text().c_str()));
        });
        wuxc::Grid::SetColumn(add, 1); inputRow.Children().Append(add);
        auto settings = wuxc::Button();
        settings.Content(winrt::box_value(L"⚙"));
        settings.MinWidth(32);
        settings.Margin(wux::Thickness{5, 0, 0, 0});
        wuxc::ToolTipService::SetToolTip(settings, winrt::box_value(L"自定义分类和颜色"));
        settings.Click([settings](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            TodoShowCategoryManager(settings);
        });
        wuxc::Grid::SetColumn(settings, 2); inputRow.Children().Append(settings);
        m_itemsControl.Items().Append(inputRow);
        TodoResizeTaskList();
        TodoRefreshMarkers();
    } catch (...) { Wh_Log(L"NativeTodo: render task panel failed"); }
}
// ---------------- end Native Todo v0.4 ----------------

void UpdateDateDisplayAndCalendar(SYSTEMTIME const& st) {
    if (m_dateButtonText) {
        m_dateButtonText.Text(FormatFilterDate(st));
    }
    if (m_datePicker) {
        try {
            m_datePicker.Date(SystemTimeToWinRtDateTime(st));
        } catch (...) {}
    }
    if (m_datePickerText) {
        try {
            m_datePickerText.Text(FormatFilterDate(st));
        } catch (...) {}
    }
    if (m_calendarView) {
        auto winrtDt = SystemTimeToWinRtDateTime(st);
        m_isUpdatingCalendarSelection = true;
        try {
            m_calendarView.SetDisplayDate(winrtDt);
            m_calendarView.SelectedDates().Clear();
            m_calendarView.SelectedDates().Append(winrtDt);
        } catch (...) {}
        m_isUpdatingCalendarSelection = false;
    }
}

void ChangeSelectedDay(int deltaDays) {
    SYSTEMTIME newDate = ShiftLocalDate(m_currentFilterDate, deltaDays);
    m_currentFilterDate = newDate;

    UpdateDateDisplayAndCalendar(newDate);

    std::vector<CalendarEvent> filtered;
    {
        std::lock_guard<std::mutex> lock(g_eventsMutex);
        filtered = FilterEventsForDate(g_allParsedEvents, newDate);
    }
    PopulateItemsControl(filtered);
}

void OnDatePickerDateChanged(wf::IReference<wf::DateTime> const& newDate) {
    SYSTEMTIME selected{};
    if (newDate) {
        auto dt = newDate.Value();
        auto ticks = dt.time_since_epoch().count();
        ULARGE_INTEGER uli;
        uli.QuadPart = static_cast<ULONGLONG>(ticks);
        FILETIME ft;
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;

        SYSTEMTIME stUtc{}, stLocal{};
        FileTimeToSystemTime(&ft, &stUtc);
        SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal);
        selected = stLocal;
    } else {
        GetLocalTime(&selected);
    }

    if (selected.wYear == m_currentFilterDate.wYear &&
        selected.wMonth == m_currentFilterDate.wMonth &&
        selected.wDay == m_currentFilterDate.wDay) return;

    m_currentFilterDate = selected;
    if (m_dateButtonText) {
        m_dateButtonText.Text(FormatFilterDate(selected));
    }
    if (m_datePickerText) {
        try {
            m_datePickerText.Text(FormatFilterDate(selected));
        } catch (...) {}
    }
    std::vector<CalendarEvent> filtered;
    {
        std::lock_guard<std::mutex> lock(g_eventsMutex);
        filtered = FilterEventsForDate(g_allParsedEvents, selected);
    }
    PopulateItemsControl(filtered);
}

void OnCalendarViewSelectedDatesChanged(wf::IReference<wf::DateTime> const& newDate) {
    if (m_isUpdatingCalendarSelection) return;
    TodoHideDetail();
    SYSTEMTIME selected{};
    if (newDate) {
        auto dt = newDate.Value();
        auto ticks = dt.time_since_epoch().count();
        ULARGE_INTEGER uli;
        uli.QuadPart = static_cast<ULONGLONG>(ticks);
        FILETIME ft;
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;

        SYSTEMTIME stUtc{}, stLocal{};
        FileTimeToSystemTime(&ft, &stUtc);
        SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal);
        selected = stLocal;
    } else {
        GetLocalTime(&selected);
    }

    if (selected.wYear == m_currentFilterDate.wYear &&
        selected.wMonth == m_currentFilterDate.wMonth &&
        selected.wDay == m_currentFilterDate.wDay) return;

    m_currentFilterDate = selected;
    if (m_dateButtonText) {
        m_dateButtonText.Text(FormatFilterDate(selected));
    }
    if (m_datePickerText) {
        try {
            m_datePickerText.Text(FormatFilterDate(selected));
        } catch (...) {}
    }
    std::vector<CalendarEvent> filtered;
    {
        std::lock_guard<std::mutex> lock(g_eventsMutex);
        filtered = FilterEventsForDate(g_allParsedEvents, selected);
    }
    PopulateItemsControl(filtered);
}

void ResetDatePickerToToday() {
    SYSTEMTIME today;
    GetLocalTime(&today);
    m_currentFilterDate = today;
    UpdateDateDisplayAndCalendar(today);
    std::vector<CalendarEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_eventsMutex);
        events = FilterEventsForDate(g_allParsedEvents, today);
    }
    PopulateItemsControl(events);
}

void DispatchUpdateEvents() {
    wuxc::ItemsControl itemsControl{nullptr};
    {
        std::lock_guard<std::mutex> lock(g_watcherMutex);
        itemsControl = m_itemsControl;
    }
    if (!itemsControl) return;
    auto dispatcher = itemsControl.Dispatcher();
    if (!dispatcher) return;

    auto action = dispatcher.TryRunAsync(wuc::CoreDispatcherPriority::Normal, []() {
        if (HANDLE s = g_hStopEvent.load(); s && WaitForSingleObject(s, 0) == WAIT_OBJECT_0) {
            return;
        }
        std::vector<CalendarEvent> filtered;
        {
            std::lock_guard<std::mutex> lock(g_eventsMutex);
            filtered = FilterEventsForDate(g_allParsedEvents, m_currentFilterDate);
        }
        try { PopulateItemsControl(filtered); TodoRefreshMarkers(); } catch (...) {}
    });

    if (action) {
        std::lock_guard<std::mutex> lock(g_pendingActionsMutex);
        std::erase_if(
            g_pendingDispatcherActions,
            [](winrt::Windows::Foundation::IAsyncOperation<bool> const& a) {
                return !a || a.Status() !=
                                 winrt::Windows::Foundation::AsyncStatus::Started;
            });
        g_pendingDispatcherActions.push_back(std::move(action));
    }
}

void UpdateMaxHeight(int maxHeight) {
    wuxc::ScrollViewer sv{nullptr};
    {
        std::lock_guard<std::mutex> lock(g_watcherMutex);
        sv = m_eventsScrollViewer;
    }
    if (sv) {
        sv.MaxHeight(maxHeight > 0 ? (double)maxHeight : std::numeric_limits<double>::infinity());
        sv.Height(std::numeric_limits<double>::quiet_NaN());
    }
}

void ReplaceCalendarContent(wuxc::ScrollViewer const& host) {
    if (!host) return;
    DWORD currentThread = GetCurrentThreadId();
    DWORD expected = 0;
    if (!g_ownerThreadId.compare_exchange_strong(expected, currentThread) && expected != currentThread) {
        return;
    }
    RegisterCoreWindowEvents();

    if (m_currentFilterDate.wYear == 0) {
        GetLocalTime(&m_currentFilterDate);
    }

    try {
        if (m_hostScrollViewer && m_hostScrollViewer != host) {
            try {
                if (m_originalCalendarContent) m_hostScrollViewer.Content(m_originalCalendarContent);
                m_hostScrollViewer.VerticalScrollBarVisibility(m_originalVerticalScrollBarVisibility);
                m_hostScrollViewer.HorizontalScrollBarVisibility(m_originalHorizontalScrollBarVisibility);
            } catch (...) {}
            m_originalCalendarContent = nullptr;
            m_hostScrollViewer = nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(g_watcherMutex);
            if (!m_itemsControl) m_itemsControl = wuxc::ItemsControl();
        }

        bool isInline = IsInlineCalendarMode();

        if (!m_eventsScrollViewer) {
            m_eventsScrollViewer = wuxc::ScrollViewer();
            m_eventsScrollViewer.Name(L"CustomCalendarScrollViewer");
            if (isInline) {
                m_eventsScrollViewer.Margin(wux::Thickness{12, 0, 12, 0});
            }
            m_eventsScrollViewer.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Auto);
            m_eventsScrollViewer.HorizontalScrollBarVisibility(wuxc::ScrollBarVisibility::Disabled);
            m_eventsScrollViewer.Content(m_itemsControl);
        }

        int maxHeight = static_cast<int>(TodoTaskMaxHeight());
        if (maxHeight > 0) m_eventsScrollViewer.MaxHeight((double)maxHeight);
        else m_eventsScrollViewer.MaxHeight(std::numeric_limits<double>::infinity());
        m_eventsScrollViewer.Height(std::numeric_limits<double>::quiet_NaN());

        if (isInline) {
            if (!m_dateButton) {
                m_dateButton = wuxc::Button();
                m_dateButton.Name(L"CustomCalendarDateButton");
                m_dateButton.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
                m_dateButton.HorizontalContentAlignment(wux::HorizontalAlignment::Stretch);
                m_dateButton.VerticalAlignment(wux::VerticalAlignment::Center);
                m_dateButton.Margin(wux::Thickness{0, 0, 0, 6});
                wuxc::ToolTipService::SetToolTip(m_dateButton, winrt::box_value(L"Show calendar"));

                m_dateButtonText = wuxc::TextBlock();
                m_dateButtonText.Text(FormatFilterDate(m_currentFilterDate));
                m_dateButtonText.VerticalAlignment(wux::VerticalAlignment::Center);
                m_dateButtonText.HorizontalTextAlignment(wux::TextAlignment::Left);
                m_dateButton.Content(m_dateButtonText);

                m_dateBtnClickToken = m_dateButton.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                    if (!m_calendarView) return;
                    bool isAbove = IsCalendarAboveAgenda();
                    if (m_calendarView.Visibility() == wux::Visibility::Visible) {
                        m_calendarView.Visibility(wux::Visibility::Collapsed);
                        wuxc::ToolTipService::SetToolTip(m_dateButton, winrt::box_value(L"Show calendar"));
                        if (isAbove) {
                            if (m_headerGrid) m_headerGrid.Margin(wux::Thickness{12, 10, 12, 0});
                        }
                        UpdateMaxHeight(GetMaxHeightCollapsedSetting());
                    } else {
                        m_calendarView.Visibility(wux::Visibility::Visible);
                        wuxc::ToolTipService::SetToolTip(m_dateButton, winrt::box_value(L"Hide calendar"));
                        if (isAbove) {
                            if (m_headerGrid) m_headerGrid.Margin(wux::Thickness{12, 2, 12, 0});
                        }
                        UpdateMaxHeight(GetMaxHeightExpandedSetting());
                    }
                });
            }

            if (!m_calendarView) {
                m_calendarView = wuxc::CalendarView();
                m_calendarView.Name(L"CustomCalendarView");
                m_calendarView.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
                m_calendarView.Margin(wux::Thickness{0, 0, 0, 4});
                m_calendarView.BorderThickness(wux::Thickness{0, 0, 0, 0});
                m_calendarView.Background(wuxm::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
                m_calendarView.IsTodayHighlighted(true);
                m_calendarView.SelectionMode(wuxc::CalendarViewSelectionMode::Single);
                m_calendarView.Visibility(wux::Visibility::Visible);
                m_todoDayChangingToken = m_calendarView.CalendarViewDayItemChanging(
                    [](wuxc::CalendarView const&, wuxc::CalendarViewDayItemChangingEventArgs const& a) {
                        try { if (a.Phase() == 0) TodoDrawMarker(a.Item()); } catch (...) {}
                    });
                m_calendarViewSelectionChangedToken = m_calendarView.SelectedDatesChanged([](wuxc::CalendarView const& cv, wuxc::CalendarViewSelectedDatesChangedEventArgs const& args) {
                    if (args.AddedDates().Size() > 0) {
                        OnCalendarViewSelectedDatesChanged(args.AddedDates().GetAt(0));
                    }
                });
            }
        } else {
            if (!m_datePicker) {
                m_datePicker = wuxc::CalendarDatePicker();
                m_datePicker.Name(L"CustomCalendarDatePicker");
                m_datePicker.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
                m_datePicker.VerticalAlignment(wux::VerticalAlignment::Center);
                m_datePicker.Margin(wux::Thickness{0, 0, 0, 6});
                m_datePicker.IsTodayHighlighted(true);
                m_datePicker.DateFormat(GetCalendarDatePickerFormat());
                m_datePicker.Date(winrt::clock::now());
                m_dateChangedToken = m_datePicker.DateChanged([](wuxc::CalendarDatePicker const&, wuxc::CalendarDatePickerDateChangedEventArgs const& args) {
                    OnDatePickerDateChanged(args.NewDate());
                });
                m_datePickerLoadedToken = m_datePicker.Loaded([](wf::IInspectable const& sender, wux::RoutedEventArgs const&) {
                    try {
                        if (auto cdp = sender.try_as<wux::FrameworkElement>()) {
                            std::vector<wux::DependencyObject> queue;
                            queue.push_back(cdp);
                            while (!queue.empty()) {
                                auto current = queue.back();
                                queue.pop_back();

                                if (auto grid = current.try_as<wuxc::Grid>()) {
                                    if (grid.ColumnDefinitions().Size() >= 3) {
                                        grid.ColumnDefinitions().GetAt(2).Width(
                                            wux::GridLength{0.0, wux::GridUnitType::Pixel}
                                        );
                                    }
                                }

                                int count = wuxm::VisualTreeHelper::GetChildrenCount(current);
                                for (int i = 0; i < count; ++i) {
                                    auto child = wuxm::VisualTreeHelper::GetChild(current, i);
                                    if (auto fe = child.try_as<wux::FrameworkElement>()) {
                                        if (fe.Name() == L"CalendarGlyph") {
                                            fe.Visibility(wux::Visibility::Collapsed);
                                        } else if (fe.Name() == L"DateText") {
                                            m_datePickerText = fe.try_as<wuxc::TextBlock>();
                                            wuxc::Grid::SetColumnSpan(fe, 2);
                                            if (m_datePickerText) {
                                                try {
                                                    m_datePickerText.Text(FormatFilterDate(m_currentFilterDate));
                                                } catch (...) {}
                                            }
                                        }
                                    }
                                    queue.push_back(child);
                                }
                            }
                        }
                    } catch (...) {}
                });
            }
        }

        if (!m_prevDayButton) {
            m_prevDayButton = wuxc::Button();
            m_prevDayButton.Name(L"CustomCalendarPrevDayButton");
            m_prevDayButton.Width(32);
            m_prevDayButton.Height(32);
            m_prevDayButton.Padding(wux::Thickness{0, 0, 0, 0});
            m_prevDayButton.Margin(wux::Thickness{0, 0, 6, 6});
            m_prevDayButton.VerticalAlignment(wux::VerticalAlignment::Center);
            auto prevIcon = wuxc::FontIcon(); prevIcon.Glyph(L"\uE76B"); prevIcon.FontSize(12);
            m_prevDayButton.Content(prevIcon);
            m_prevBtnToken = m_prevDayButton.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) { ChangeSelectedDay(-1); });
            m_prevDayAccel = wuxi::KeyboardAccelerator();
            m_prevDayAccel.Key(ws::VirtualKey::Left);
            wuxc::ToolTipService::SetToolTip(
                m_prevDayButton,
                winrt::box_value(L"Previous day")
            );
            m_prevDayAccel.IsEnabled(GetKeyboardShortcutsSetting());
            m_prevDayAccel.Invoked([](wuxi::KeyboardAccelerator const&, wuxi::KeyboardAcceleratorInvokedEventArgs const& args) {
                if (!GetKeyboardShortcutsSetting()) return;
                if (m_calendarView && m_calendarView.Visibility() == wux::Visibility::Visible) return;
                if (m_datePicker && m_datePicker.IsCalendarOpen()) return;
                ChangeSelectedDay(-1); args.Handled(true);
            });
            m_prevDayButton.KeyboardAccelerators().Append(m_prevDayAccel);
        }

        if (!m_nextDayButton) {
            m_nextDayButton = wuxc::Button();
            m_nextDayButton.Name(L"CustomCalendarNextDayButton");
            m_nextDayButton.Width(32);
            m_nextDayButton.Height(32);
            m_nextDayButton.Padding(wux::Thickness{0, 0, 0, 0});
            m_nextDayButton.Margin(wux::Thickness{6, 0, 6, 6});
            m_nextDayButton.VerticalAlignment(wux::VerticalAlignment::Center);
            auto nextIcon = wuxc::FontIcon(); nextIcon.Glyph(L"\uE76C"); nextIcon.FontSize(12);
            m_nextDayButton.Content(nextIcon);
            m_nextBtnToken = m_nextDayButton.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) { ChangeSelectedDay(1); });
            m_nextDayAccel = wuxi::KeyboardAccelerator();
            m_nextDayAccel.Key(ws::VirtualKey::Right);
            wuxc::ToolTipService::SetToolTip(
                m_nextDayButton,
                winrt::box_value(L"Next day")
            );
            m_nextDayAccel.IsEnabled(GetKeyboardShortcutsSetting());
            m_nextDayAccel.Invoked([](wuxi::KeyboardAccelerator const&, wuxi::KeyboardAcceleratorInvokedEventArgs const& args) {
                if (!GetKeyboardShortcutsSetting()) return;
                if (m_calendarView && m_calendarView.Visibility() == wux::Visibility::Visible) return;
                if (m_datePicker && m_datePicker.IsCalendarOpen()) return;
                ChangeSelectedDay(1); args.Handled(true);
            });
            m_nextDayButton.KeyboardAccelerators().Append(m_nextDayAccel);
        }

        if (!m_refreshButton) {
            m_refreshButton = wuxc::Button();
            m_refreshButton.Name(L"CustomCalendarRefreshButton");
            m_refreshButton.Visibility(wux::Visibility::Collapsed);
            m_refreshButton.Width(32);
            m_refreshButton.Height(32);
            m_refreshButton.Padding(wux::Thickness{0, 0, 0, 0});
            m_refreshButton.Margin(wux::Thickness{0, 0, 0, 6});
            m_refreshButton.VerticalAlignment(wux::VerticalAlignment::Center);
            auto refreshIcon = wuxc::FontIcon(); refreshIcon.Glyph(L"\uE72C"); refreshIcon.FontSize(12);
            m_refreshButton.Content(refreshIcon);
            wuxc::ToolTipService::SetToolTip(m_refreshButton, winrt::box_value(L"Refresh events"));
            m_refreshBtnToken = m_refreshButton.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) { TriggerBackgroundFetch(true); });
        }

        if (!m_headerGrid) {
            m_headerGrid = wuxc::Grid();
            m_headerGrid.Name(L"CustomCalendarHeaderGrid");
            if (isInline) {
                if (IsCalendarAboveAgenda()) {
                    m_headerGrid.Margin(wux::Thickness{12, 10, 12, 0});
                } else {
                    m_headerGrid.Margin(wux::Thickness{12, 6, 12, 0});
                }
            } else {
                m_headerGrid.Margin(wux::Thickness{0, 6, 0, 0});
            }
            wuxc::ColumnDefinition col0{}; col0.Width(wux::GridLength{0, wux::GridUnitType::Auto}); m_headerGrid.ColumnDefinitions().Append(col0);
            wuxc::ColumnDefinition col1{}; col1.Width(wux::GridLength{1, wux::GridUnitType::Star}); m_headerGrid.ColumnDefinitions().Append(col1);
            wuxc::ColumnDefinition col2{}; col2.Width(wux::GridLength{0, wux::GridUnitType::Auto}); m_headerGrid.ColumnDefinitions().Append(col2);
            wuxc::ColumnDefinition col3{}; col3.Width(wux::GridLength{0, wux::GridUnitType::Auto}); m_headerGrid.ColumnDefinitions().Append(col3);
            wuxc::Grid::SetColumn(m_prevDayButton, 0); m_headerGrid.Children().Append(m_prevDayButton);
            if (isInline) {
                wuxc::Grid::SetColumn(m_dateButton, 1); m_headerGrid.Children().Append(m_dateButton);
            } else {
                wuxc::Grid::SetColumn(m_datePicker, 1); m_headerGrid.Children().Append(m_datePicker);
            }
            wuxc::Grid::SetColumn(m_nextDayButton, 2); m_headerGrid.Children().Append(m_nextDayButton);
            wuxc::Grid::SetColumn(m_refreshButton, 3); m_headerGrid.Children().Append(m_refreshButton);
            m_headerGrid.Visibility(wux::Visibility::Collapsed);
        }

        if (!m_rootGrid) {
            m_rootGrid = wuxc::Grid();
            m_rootGrid.Name(L"CustomCalendarRootGrid");
            bool isAbove = IsCalendarAboveAgenda();
            if (isInline) {
                m_rootGrid.Margin(wux::Thickness{0, 0, 0, 4});
                wuxc::RowDefinition row0{}; row0.Height(wux::GridLength{0, wux::GridUnitType::Auto}); m_rootGrid.RowDefinitions().Append(row0);
                wuxc::RowDefinition row1{}; row1.Height(wux::GridLength{0, wux::GridUnitType::Auto}); m_rootGrid.RowDefinitions().Append(row1);
                wuxc::RowDefinition row2{}; row2.Height(wux::GridLength{0, wux::GridUnitType::Auto}); m_rootGrid.RowDefinitions().Append(row2);
                if (isAbove) {
                    wuxc::Grid::SetRow(m_calendarView, 0); m_rootGrid.Children().Append(m_calendarView);
                    wuxc::Grid::SetRow(m_headerGrid, 1); m_rootGrid.Children().Append(m_headerGrid);
                    wuxc::Grid::SetRow(m_eventsScrollViewer, 2); m_rootGrid.Children().Append(m_eventsScrollViewer);
                    m_eventsScrollViewer.Margin(wux::Thickness{12, 0, 12, 0});
                } else {
                    wuxc::Grid::SetRow(m_eventsScrollViewer, 0); m_rootGrid.Children().Append(m_eventsScrollViewer);
                    wuxc::Grid::SetRow(m_headerGrid, 1); m_rootGrid.Children().Append(m_headerGrid);
                    wuxc::Grid::SetRow(m_calendarView, 2); m_rootGrid.Children().Append(m_calendarView);
                    m_eventsScrollViewer.Margin(wux::Thickness{12, 10, 12, 0});
                    m_headerGrid.Margin(wux::Thickness{12, 6, 12, 0});
                    m_calendarView.Margin(wux::Thickness{0, 0, 0, 4});
                }
            } else {
                m_rootGrid.Margin(wux::Thickness{12, 4, 12, 4});
                wuxc::RowDefinition row0{}; row0.Height(wux::GridLength{0, wux::GridUnitType::Auto}); m_rootGrid.RowDefinitions().Append(row0);
                wuxc::RowDefinition row1{}; row1.Height(wux::GridLength{0, wux::GridUnitType::Auto}); m_rootGrid.RowDefinitions().Append(row1);
                if (isAbove) {
                    wuxc::Grid::SetRow(m_headerGrid, 0); m_rootGrid.Children().Append(m_headerGrid);
                    wuxc::Grid::SetRow(m_eventsScrollViewer, 1); m_rootGrid.Children().Append(m_eventsScrollViewer);
                    m_headerGrid.Margin(wux::Thickness{0, 6, 0, 0});
                    m_eventsScrollViewer.Margin(wux::Thickness{0, 0, 0, 0});
                } else {
                    wuxc::Grid::SetRow(m_eventsScrollViewer, 0); m_rootGrid.Children().Append(m_eventsScrollViewer);
                    wuxc::Grid::SetRow(m_headerGrid, 1); m_rootGrid.Children().Append(m_headerGrid);
                    m_eventsScrollViewer.Margin(wux::Thickness{0, 6, 0, 0});
                    m_headerGrid.Margin(wux::Thickness{0, 6, 0, 0});
                }
            }
        }

        if (!m_todoDetailHost && m_rootGrid) {
            m_todoDetailHost = wuxc::Border();
            m_todoDetailHost.Name(L"NativeTodoDetailSection");
            m_todoDetailHost.Visibility(wux::Visibility::Collapsed);
            m_todoDetailHost.Background(GetCardBackgroundBrush());
            m_todoDetailHost.BorderBrush(GetCardBorderBrush());
            m_todoDetailHost.BorderThickness(wux::Thickness{1, 1, 1, 1});
            m_todoDetailHost.CornerRadius(GetStandardCornerRadius());
            m_todoDetailHost.Padding(wux::Thickness{12, 8, 12, 10});
            m_todoDetailHost.Margin(wux::Thickness{12, 5, 12, 10});
            auto topRow = wuxc::RowDefinition();
            topRow.Height(wux::GridLength{0, wux::GridUnitType::Auto});
            m_rootGrid.RowDefinitions().InsertAt(0, topRow);
            for (auto const& child : m_rootGrid.Children()) {
                if (auto element = child.try_as<winrt::Windows::UI::Xaml::FrameworkElement>()) {
                    wuxc::Grid::SetRow(
                        element,
                        wuxc::Grid::GetRow(element) + 1
                    );
                }
            }
            wuxc::Grid::SetRow(m_todoDetailHost, 0);
            m_rootGrid.Children().Append(m_todoDetailHost);
            // Click elsewhere in the calendar panel to dismiss.
            m_rootGrid.Tapped([](wf::IInspectable const&,
                                 wuxi::TappedRoutedEventArgs const& e) {
                if (!m_todoDetailHost ||
                    m_todoDetailHost.Visibility() != wux::Visibility::Visible) return;
                // Avoid closing on the very click that opened the detail card.
                if (GetTickCount64() - m_todoDetailOpenTick < 180) return;
                try {
                    auto current = e.OriginalSource().try_as<wux::DependencyObject>();
                    while (current) {
                        if (current == m_todoDetailHost.as<wux::DependencyObject>()) return;
                        current = wuxm::VisualTreeHelper::GetParent(current);
                    }
                    TodoHideDetail();
                } catch (...) {}
            });
        }
        SYSTEMTIME today; GetLocalTime(&today); m_currentFilterDate = today;
        std::vector<CalendarEvent> currentEvents;
        { std::lock_guard<std::mutex> lock(g_eventsMutex); currentEvents = FilterEventsForDate(g_allParsedEvents, today); }
        PopulateItemsControl(currentEvents);
        UpdateDateDisplayAndCalendar(m_currentFilterDate);

        if (m_rootGrid) {
            if (auto parent = m_rootGrid.Parent()) {
                if (auto parentContentControl = parent.try_as<wuxc::ContentControl>()) {
                    if (parentContentControl.Content() == m_rootGrid) parentContentControl.Content(nullptr);
                } else if (auto parentPanel = parent.try_as<wuxc::Panel>()) {
                    uint32_t index = 0;
                    if (parentPanel.Children().IndexOf(m_rootGrid, index)) parentPanel.Children().RemoveAt(index);
                }
            }
        }

        if (!m_originalCalendarContent && host.Content() != m_rootGrid) {
            m_originalCalendarContent = host.Content();
            m_originalVerticalScrollBarVisibility = host.VerticalScrollBarVisibility();
            m_originalHorizontalScrollBarVisibility = host.HorizontalScrollBarVisibility();
        }
        m_hostScrollViewer = host;

        host.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Disabled);
        host.HorizontalScrollBarVisibility(wuxc::ScrollBarVisibility::Disabled);
        host.Content(m_rootGrid);
        RevokeLayoutUpdated();
        OnCalendarOpened();
    } catch (...) {}
}

void HandleFocusSessionControl(wux::FrameworkElement const& element) {
    if (!element || m_focusSessionControl == element) return;
    DWORD currentThread = GetCurrentThreadId();
    DWORD expected = 0;
    if (!g_ownerThreadId.compare_exchange_strong(expected, currentThread) && expected != currentThread) {
        return;
    }

    UnregisterFocusSessionControl();
    m_focusSessionControl = element;
    m_originalFocusSessionVisibility = element.Visibility();

    try {
        bool hide = ShouldHideFocusSession();
        if (hide) element.Visibility(wux::Visibility::Collapsed);

        if (m_focusSessionVisibilityToken == 0) {
            m_focusSessionVisibilityToken = element.RegisterPropertyChangedCallback(
                wux::UIElement::VisibilityProperty(),
                [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
                    if (auto fe = sender.try_as<wux::FrameworkElement>()) {
                        if (ShouldHideFocusSession() && fe.Visibility() != wux::Visibility::Collapsed) {
                            fe.Visibility(wux::Visibility::Collapsed);
                        }
                    }
                });
        }
    } catch (...) {}
}

void WalkVisualTree(winrt::Windows::UI::Xaml::DependencyObject const& root) {
    if (!root) return;
    DWORD owner = g_ownerThreadId.load();
    if (owner != 0 && owner != GetCurrentThreadId()) return;

    try {
        if (auto fe = root.try_as<wux::FrameworkElement>()) {
            auto name = fe.Name();
            if (name == L"CalendarControlScrollViewer") {
                if (auto scrollViewer = fe.try_as<wuxc::ScrollViewer>()) {
                    if (scrollViewer != m_hostScrollViewer) ReplaceCalendarContent(scrollViewer);
                }
            } else if (name == L"FocusSessionControl" || (!m_focusSessionControl && winrt::get_class_name(fe) == L"ActionCenter.FocusSessionControl")) {
                HandleFocusSessionControl(fe);
            }
        }
        int count = wuxm::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; i++) WalkVisualTree(wuxm::VisualTreeHelper::GetChild(root, i));
    } catch (...) {}
}

DWORD WINAPI WorkerThreadProc(LPVOID) {
    while (true) {
        HANDLE stopEvent = g_hStopEvent.load();
        HANDLE workEvent = g_hWorkEvent.load();
        if (!stopEvent || !workEvent) {
            break;
        }

        HANDLE events[2] = {stopEvent, workEvent};
        DWORD waitRes = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (waitRes == WAIT_OBJECT_0) {
            // Stop event signaled - exit worker thread immediately
            break;
        }
        if (waitRes != WAIT_OBJECT_0 + 1) {
            // Error or unexpected
            break;
        }

        bool forceFetch = g_workerForceFetch.exchange(false);
        auto icsPaths = GetIcsPathsSetting();
        if (icsPaths.empty()) {
            Wh_Log(L"No ICS path configured in settings");
            {
                std::lock_guard<std::mutex> lock(g_eventsMutex);
                g_allParsedEvents.clear();
            }
            DispatchUpdateEvents();
            continue;
        }

        std::vector<CalendarEvent> allEvents;
        std::unordered_set<std::wstring> seenKeys;

        for (const auto& icsPath : icsPaths) {
            if (HANDLE s = g_hStopEvent.load(); s && WaitForSingleObject(s, 0) == WAIT_OBJECT_0) {
                break;
            }

            Wh_Log(L"Processing calendar from: %s (force=%d)", icsPath.c_str(),
                   forceFetch ? 1 : 0);

            bool fromCache = false;
            std::wstring content = FetchIcsContent(icsPath, forceFetch, fromCache);

            if (content.empty()) {
                Wh_Log(
                    L"No content retrieved for %s, skipping", icsPath.c_str());
                continue;
            }

            std::vector<CalendarEvent> calendarEvents;
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                auto& entry = g_calendarCaches[icsPath];
                if (fromCache && !entry.cachedEvents.empty()) {
                    calendarEvents = entry.cachedEvents;
                } else {
                    calendarEvents = ParseIcs(content);
                    entry.cachedEvents = calendarEvents;
                    Wh_Log(L"Parsed %zu events from %s", calendarEvents.size(), icsPath.c_str());
                }
            }

            for (auto& ev : calendarEvents) {
                if (!ev.uid.empty()) {
                    std::wstring key = ev.uid + L"#" + ev.recurrenceId;
                    if (!seenKeys.insert(key).second) {
                        continue;
                    }
                }
                allEvents.push_back(std::move(ev));
            }
        }

        if (HANDLE s = g_hStopEvent.load(); s && WaitForSingleObject(s, 0) == WAIT_OBJECT_0) {
            break;
        }

        {
            std::lock_guard<std::mutex> lock(g_eventsMutex);
            g_allParsedEvents = std::move(allEvents);
            g_todoHolidayMemo.clear();
        }

        DispatchUpdateEvents();
    }
    return 0;
}

void StartWorkerThread() {
    if (g_hWorkerThread.load()) {
        return;
    }
    g_hWorkEvent.store(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    g_hStopEvent.store(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    g_hWorkerThread.store(CreateThread(nullptr, 0, WorkerThreadProc, nullptr, 0, nullptr));
}

void StopWorkerThread() {
    HANDLE workerThread = g_hWorkerThread.exchange(nullptr);
    if (HANDLE stopEvent = g_hStopEvent.load()) {
        SetEvent(stopEvent);
    }
    if (workerThread) {
        WaitForSingleObject(workerThread, INFINITE);
        CloseHandle(workerThread);
    }
    if (HANDLE workEvent = g_hWorkEvent.exchange(nullptr)) {
        CloseHandle(workEvent);
    }
    if (HANDLE stopEvent = g_hStopEvent.exchange(nullptr)) {
        CloseHandle(stopEvent);
    }
}

void TriggerBackgroundFetch(bool force) {
    if (force) {
        g_workerForceFetch = true;
    }
    if (HANDLE workEvent = g_hWorkEvent.load()) {
        SetEvent(workEvent);
    }
}

void OnCalendarOpened() {
    DWORD owner = g_ownerThreadId.load();
    if (owner != 0 && owner != GetCurrentThreadId()) {
        return;
    }

    ULONGLONG currentTick = GetTickCount64();
    if (m_rootGrid && currentTick - g_lastOpenTick < 500) {
        return;
    }
    g_lastOpenTick = currentTick;

    Wh_Log(
        L"Calendar opened, resetting date picker to today and triggering "
        L"background fetch");

    ResetDatePickerToToday();

    bool force = (GetMinFetchIntervalSetting() <= 0);
    TriggerBackgroundFetch(force);

    try {
        auto window = winrt::Windows::UI::Xaml::Window::Current();
        if (window && window.Content()) {
            if (auto fe = window.Content().try_as<winrt::Windows::UI::Xaml::FrameworkElement>()) {
                if (!m_rootGrid && t_coreWindowData.layoutUpdatedToken.value == 0) {
                    t_coreWindowData.layoutUpdatedFe = fe;
                    t_coreWindowData.layoutUpdatedToken = fe.LayoutUpdated([](winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&) {
                        if (m_rootGrid) {
                            RevokeLayoutUpdated();
                            return;
                        }
                        auto w = winrt::Windows::UI::Xaml::Window::Current();
                        if (w) {
                            if (w.Content()) WalkVisualTree(w.Content());
                            auto popups = winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopups(w);
                            for (auto popup : popups) {
                                if (popup.Child()) WalkVisualTree(popup.Child());
                            }
                        }
                        if (m_rootGrid) {
                            RevokeLayoutUpdated();
                        }
                    });
                }
            }
            WalkVisualTree(window.Content());
            auto popups = winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopups(window);
            for (auto popup : popups) {
                if (popup.Child()) WalkVisualTree(popup.Child());
            }
        }
    } catch (...) {
        Wh_Log(L"Exception getting Window::Current()");
    }
}
using RunFromWindowThreadProc_t = void(WINAPI*)(PVOID parameter);

bool RunFromWindowThreadViaPostMessage(HWND hWnd,
                                       RunFromWindowThreadProc_t proc,
                                       PVOID procParam) {
    static const UINT runFromWindowThreadRegisteredMsgViaPostMessage =
        RegisterWindowMessage(
            L"Windhawk_RunFromWindowThreadViaPostMessage_" WH_MOD_ID);

    struct RUN_FROM_WINDOW_THREAD_PARAM {
        RunFromWindowThreadProc_t proc;
        PVOID procParam;
        HHOOK hook;
    };

    DWORD dwThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (dwThreadId == 0) {
        return false;
    }

    HHOOK hook = SetWindowsHookEx(
        WH_GETMESSAGE,
        [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (nCode == HC_ACTION && wParam == PM_REMOVE) {
                MSG* msg = (MSG*)lParam;
                if (msg->message ==
                    runFromWindowThreadRegisteredMsgViaPostMessage) {
                    auto* param = (RUN_FROM_WINDOW_THREAD_PARAM*)msg->lParam;
                    if (param) {
                        param->proc(param->procParam);
                        UnhookWindowsHookEx(param->hook);
                        delete param;
                        msg->lParam = 0;
                    }
                }
            }

            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        },
        nullptr, dwThreadId);
    if (!hook) {
        return false;
    }

    auto* param = new RUN_FROM_WINDOW_THREAD_PARAM{
        .proc = proc,
        .procParam = procParam,
        .hook = hook,
    };
    if (!PostMessage(hWnd, runFromWindowThreadRegisteredMsgViaPostMessage, 0,
                     (LPARAM)param)) {
        UnhookWindowsHookEx(hook);
        delete param;
        return false;
    }

    return true;
}

bool RunFromWindowThread(HWND hWnd,
                         RunFromWindowThreadProc_t proc,
                         PVOID procParam) {
    static const UINT runFromWindowThreadRegisteredMsg =
        RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);

    struct RUN_FROM_WINDOW_THREAD_PARAM {
        RunFromWindowThreadProc_t proc;
        PVOID procParam;
    };

    DWORD dwThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (dwThreadId == 0) {
        return false;
    }

    if (dwThreadId == GetCurrentThreadId()) {
        proc(procParam);
        return true;
    }

    HHOOK hook = SetWindowsHookEx(
        WH_CALLWNDPROC,
        [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (nCode == HC_ACTION) {
                const CWPSTRUCT* cwp = (const CWPSTRUCT*)lParam;
                if (cwp->message == runFromWindowThreadRegisteredMsg) {
                    RUN_FROM_WINDOW_THREAD_PARAM* param =
                        (RUN_FROM_WINDOW_THREAD_PARAM*)cwp->lParam;
                    param->proc(param->procParam);
                }
            }

            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        },
        nullptr, dwThreadId);
    if (!hook) {
        return false;
    }

    RUN_FROM_WINDOW_THREAD_PARAM param;
    param.proc = proc;
    param.procParam = procParam;
    SendMessage(hWnd, runFromWindowThreadRegisteredMsg, 0, (LPARAM)&param);

    UnhookWindowsHookEx(hook);

    return true;
}

void RegisterCoreWindowEvents() {
    try {
        auto coreWindow = wuc::CoreWindow::GetForCurrentThread();
        if (!coreWindow) {
            return;
        }
        if (t_coreWindowData.coreWindow && t_coreWindowData.coreWindow == coreWindow) {
            return;
        }
        Wh_Log(
            L"Registering CoreWindow Activated & VisibilityChanged for thread %u",
            GetCurrentThreadId());

        auto activatedToken = coreWindow.Activated(
            [](auto&&, wuc::WindowActivatedEventArgs const& args) {
                if (args.WindowActivationState() !=
                    wuc::CoreWindowActivationState::Deactivated) {
                    Wh_Log(L"CoreWindow Activated");
                    OnCalendarOpened();
                }
            });

        auto visibilityChangedToken = coreWindow.VisibilityChanged(
            [](auto&&, wuc::VisibilityChangedEventArgs const& args) {
                if (args.Visible()) {
                    Wh_Log(L"CoreWindow VisibilityChanged: Visible");
                    OnCalendarOpened();
                }
            });

        t_coreWindowData.coreWindow = coreWindow;
        t_coreWindowData.activatedToken = activatedToken;
        t_coreWindowData.visibilityChangedToken = visibilityChangedToken;
    } catch (...) {
        Wh_Log(L"Failed to register CoreWindow events: %08X",
               winrt::to_hresult());
    }
}

void UnregisterCoreWindowEvents() {
    RevokeLayoutUpdated();
    try {
        if (t_coreWindowData.coreWindow) {
            if (t_coreWindowData.activatedToken.value != 0) {
                t_coreWindowData.coreWindow.Activated(t_coreWindowData.activatedToken);
            }
            if (t_coreWindowData.visibilityChangedToken.value != 0) {
                t_coreWindowData.coreWindow.VisibilityChanged(t_coreWindowData.visibilityChangedToken);
            }
            t_coreWindowData = {};
        }
    } catch (...) {}
}

void OnWindowCreated(HWND hWnd, LPCWSTR lpClassName, PCSTR funcName) {
    BOOL bTextualClassName = ((ULONG_PTR)lpClassName & ~(ULONG_PTR)0xffff) != 0;
    if (bTextualClassName &&
        _wcsicmp(lpClassName, L"Windows.UI.Core.CoreWindow") == 0) {
        Wh_Log(L"Initializing - Created core window: %08X via %S",
               (DWORD)(ULONG_PTR)hWnd, funcName);

        RunFromWindowThreadViaPostMessage(
            hWnd,
            [](PVOID) {
                RegisterCoreWindowEvents();
            },
            nullptr);
    }
}

using CreateWindowInBand_t = HWND(WINAPI*)(DWORD dwExStyle,
                                           LPCWSTR lpClassName,
                                           LPCWSTR lpWindowName,
                                           DWORD dwStyle,
                                           int X,
                                           int Y,
                                           int nWidth,
                                           int nHeight,
                                           HWND hWndParent,
                                           HMENU hMenu,
                                           HINSTANCE hInstance,
                                           PVOID lpParam,
                                           DWORD dwBand);
CreateWindowInBand_t CreateWindowInBand_Original;

HWND WINAPI CreateWindowInBand_Hook(DWORD dwExStyle,
                                    LPCWSTR lpClassName,
                                    LPCWSTR lpWindowName,
                                    DWORD dwStyle,
                                    int X,
                                    int Y,
                                    int nWidth,
                                    int nHeight,
                                    HWND hWndParent,
                                    HMENU hMenu,
                                    HINSTANCE hInstance,
                                    PVOID lpParam,
                                    DWORD dwBand) {
    HWND hWnd = CreateWindowInBand_Original(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam, dwBand);
    if (!hWnd) {
        return hWnd;
    }

    OnWindowCreated(hWnd, lpClassName, __FUNCTION__);
    return hWnd;
}

using CreateWindowInBandEx_t = HWND(WINAPI*)(DWORD dwExStyle,
                                             LPCWSTR lpClassName,
                                             LPCWSTR lpWindowName,
                                             DWORD dwStyle,
                                             int X,
                                             int Y,
                                             int nWidth,
                                             int nHeight,
                                             HWND hWndParent,
                                             HMENU hMenu,
                                             HINSTANCE hInstance,
                                             PVOID lpParam,
                                             DWORD dwBand,
                                             DWORD dwTypeFlags);
CreateWindowInBandEx_t CreateWindowInBandEx_Original;

HWND WINAPI CreateWindowInBandEx_Hook(DWORD dwExStyle,
                                      LPCWSTR lpClassName,
                                      LPCWSTR lpWindowName,
                                      DWORD dwStyle,
                                      int X,
                                      int Y,
                                      int nWidth,
                                      int nHeight,
                                      HWND hWndParent,
                                      HMENU hMenu,
                                      HINSTANCE hInstance,
                                      PVOID lpParam,
                                      DWORD dwBand,
                                      DWORD dwTypeFlags) {
    HWND hWnd = CreateWindowInBandEx_Original(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam, dwBand, dwTypeFlags);
    if (!hWnd) {
        return hWnd;
    }

    OnWindowCreated(hWnd, lpClassName, __FUNCTION__);
    return hWnd;
}

std::vector<HWND> GetCoreWnds() {
    struct ENUM_WINDOWS_PARAM {
        std::vector<HWND>* hWnds;
    };

    std::vector<HWND> hWnds;
    ENUM_WINDOWS_PARAM param = {&hWnds};
    EnumWindows(
        [](HWND hWnd, LPARAM lParam) -> BOOL {
            ENUM_WINDOWS_PARAM& param = *(ENUM_WINDOWS_PARAM*)lParam;

            DWORD dwProcessId = 0;
            if (!GetWindowThreadProcessId(hWnd, &dwProcessId) ||
                dwProcessId != GetCurrentProcessId()) {
                return TRUE;
            }

            WCHAR szClassName[32];
            if (GetClassName(hWnd, szClassName, ARRAYSIZE(szClassName)) == 0) {
                return TRUE;
            }

            if (_wcsicmp(szClassName, L"Windows.UI.Core.CoreWindow") == 0) {
                param.hWnds->push_back(hWnd);
            }

            return TRUE;
        },
        (LPARAM)&param);

    return hWnds;
}

// -----------------------------------------------------------------------------
// Windhawk lifecycle
// -----------------------------------------------------------------------------

BOOL Wh_ModInit() {
    Wh_Log(L"Calendar XAML mod initializing");

    StartWorkerThread();

    HMODULE user32Module = GetModuleHandleW(L"user32.dll");
    if (user32Module) {
        auto pCreateWindowInBand = (CreateWindowInBand_t)GetProcAddress(
            user32Module, "CreateWindowInBand");
        if (pCreateWindowInBand) {
            WindhawkUtils::SetFunctionHook(pCreateWindowInBand,
                                           CreateWindowInBand_Hook,
                                           &CreateWindowInBand_Original);
        }

        auto pCreateWindowInBandEx = (CreateWindowInBandEx_t)GetProcAddress(
            user32Module, "CreateWindowInBandEx");
        if (pCreateWindowInBandEx) {
            WindhawkUtils::SetFunctionHook(pCreateWindowInBandEx,
                                           CreateWindowInBandEx_Hook,
                                           &CreateWindowInBandEx_Original);
        }
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L">");

    for (HWND hCoreWnd : GetCoreWnds()) {
        Wh_Log(L"Initializing for existing CoreWindow: %08X",
               (DWORD)(ULONG_PTR)hCoreWnd);
        RunFromWindowThread(
            hCoreWnd,
            [](PVOID) {
                RegisterCoreWindowEvents();
                OnCalendarOpened();
            },
            nullptr);
    }
}

void Wh_ModUninit() {
    Wh_Log(L">");

    StopWorkerThread();

    {
        std::lock_guard<std::mutex> lock(g_pendingActionsMutex);
        for (auto const& action : g_pendingDispatcherActions) {
            if (action && action.Status() ==
                              winrt::Windows::Foundation::AsyncStatus::Started) {
                try {
                    action.Cancel();
                } catch (...) {}
            }
        }
        g_pendingDispatcherActions.clear();
    }

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_calendarCaches.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_eventsMutex);
        g_allParsedEvents.clear();
    }

    for (HWND hCoreWnd : GetCoreWnds()) {
        Wh_Log(L"Uninitializing for %08X", (DWORD)(ULONG_PTR)hCoreWnd);
        RunFromWindowThread(
            hCoreWnd,
            [](PVOID) {
                RevokeLayoutUpdated();
                UnregisterCoreWindowEvents();
                RestoreCalendarContent();
            },
            nullptr);
    }
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    Wh_Log(L">");
    *bReload = TRUE;
    return TRUE;
}
