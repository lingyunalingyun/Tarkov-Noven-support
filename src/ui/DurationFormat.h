#pragma once
#include "ui/localization/LocalizationService.h"
#include "ui/localization/TextKeys.h"
#include <cstdint>
#include <optional>

namespace noven::ui {
inline std::wstring FormatDuration(std::optional<std::int64_t> seconds) {
    if (!seconds) return Tr(TextKey::Unknown);
    std::wstring result;
    const std::int64_t units[]{86400,3600,60,1};
    const std::string_view keys[]{TextKey::DurationDay,TextKey::DurationHour,TextKey::DurationMinute,TextKey::DurationSecond};
    auto remaining=*seconds;
    for (int i=0;i<4;++i) {
        const auto n=remaining/units[i]; remaining%=units[i];
        if (n || (i==3 && result.empty())) {
            if (!result.empty()) result+=L" ";
            result+=UiLocalization().Format(keys[i],{{L"count",std::to_wstring(n)}});
        }
    }
    return result;
}
}
