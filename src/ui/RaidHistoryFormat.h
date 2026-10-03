#pragma once
#include "ui/localization/LocalizationService.h"
#include "ui/localization/TextKeys.h"
#include "raid/RaidSession.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>

namespace noven::ui {
// 列表、详情与筛选共用同一译名契约；格式化只展示证据，不把 Unknown 补猜成 PMC 或结果。
// Lists, details and filters share one localization contract; format evidence without guessing Unknown into PMC/results.
inline std::wstring RaidModeText(raid::GameMode mode) {
    switch(mode) {
    case raid::GameMode::PvP:return Tr(TextKey::RaidPvp);
    case raid::GameMode::PvE:return Tr(TextKey::RaidPve);
    case raid::GameMode::Practice:return Tr(TextKey::RaidPractice);
    case raid::GameMode::Offline:return Tr(TextKey::RaidOffline);
    default:return Tr(TextKey::Unknown);
    }
}
inline std::wstring RaidTypeText(raid::RaidType type) {
    return Tr(type==raid::RaidType::PMC?TextKey::RaidPmc:type==raid::RaidType::Scav?TextKey::RaidScav:TextKey::Unknown);
}
inline std::wstring RaidOutcomeText(raid::RaidOutcome value) {
    switch(value) {
    case raid::RaidOutcome::Survived:return Tr(TextKey::RaidSurvived);
    case raid::RaidOutcome::RunThrough:return Tr(TextKey::RaidRunThrough);
    case raid::RaidOutcome::KIA:return Tr(TextKey::RaidKia);
    case raid::RaidOutcome::MIA:return Tr(TextKey::RaidMia);
    case raid::RaidOutcome::Left:return Tr(TextKey::RaidLeft);
    default:return Tr(TextKey::Unknown);
    }
}
inline std::wstring RaidPriceText(std::optional<std::int64_t> value) {
    return value?L"₽"+std::to_wstring(*value):Tr(TextKey::Unknown);
}
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
