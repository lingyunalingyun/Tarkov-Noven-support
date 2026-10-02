#pragma once
#include "ui/localization/LocalizationService.h"
#include "ui/localization/TextKeys.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>

namespace noven::ui {
// 日志墙钟时间不经过 UTC/本地时区二次转换；缺失值不显示假日期或零时长。
// Log wall-clock times never undergo a second timezone conversion; absence is not a fake date/zero duration.
inline std::wstring RaidTimeText(std::optional<std::int64_t> ms) {
    if(!ms)return Tr(TextKey::Unknown);
    using namespace std::chrono;
    const sys_time<milliseconds> wall{milliseconds{*ms}};
    const auto dateDay=floor<days>(wall);const year_month_day date{dateDay};const hh_mm_ss clock{wall-dateDay};
    wchar_t text[40]{};
    swprintf_s(text,L"%04d-%02u-%02u %02d:%02d:%02d",int(date.year()),unsigned(date.month()),unsigned(date.day()),
        static_cast<int>(clock.hours().count()),static_cast<int>(clock.minutes().count()),static_cast<int>(clock.seconds().count()));
    return text;
}
inline std::wstring RaidDurationText(std::optional<std::int64_t> ms) {
    if(!ms)return Tr(TextKey::Unknown);
    const auto minutes=std::to_wstring(*ms/60000),seconds=std::to_wstring(*ms/1000%60);
    return UiLocalization().Format(TextKey::DurationMinute,{{L"count",minutes}})+L" "
        +UiLocalization().Format(TextKey::DurationSecond,{{L"count",seconds}});
}
}
