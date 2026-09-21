#include "overlay/OverlayTypes.h"

#include <algorithm>
#include <string>

namespace noven::overlay {

std::string DisplayNameForItem(const data::ItemRecord& item) {
    if (!item.nameZh.empty()) {
        return item.nameZh;
    }
    return item.nameEn;
}

std::wstring FormatRoubles(std::int64_t value) {
    const bool negative = value < 0;
    std::uint64_t magnitude = negative
        ? static_cast<std::uint64_t>(-(value + 1)) + 1U
        : static_cast<std::uint64_t>(value);
    std::wstring digits = std::to_wstring(magnitude);
    for (std::ptrdiff_t position = static_cast<std::ptrdiff_t>(digits.size()) - 3;
         position > 0;
         position -= 3) {
        digits.insert(static_cast<std::size_t>(position), 1, L',');
    }
    if (negative) {
        digits.insert(digits.begin(), L'-');
    }
    return digits + L" ₽";
}

std::wstring FormatOptionalRoubles(const std::optional<std::int64_t>& value) {
    return value.has_value() ? FormatRoubles(*value) : L"未知";
}

const wchar_t* FleaStatusDisplayName(data::FleaStatus status) noexcept {
    switch (status) {
    case data::FleaStatus::Allowed:
        return L"可上架";
    case data::FleaStatus::Banned:
        return L"禁止上架";
    case data::FleaStatus::LockedOrUnavailable:
        return L"不可用";
    case data::FleaStatus::Unknown:
        return L"未知";
    }
    return L"未知";
}

OverlayPlacement CalculateOverlayPlacement(
    long anchor_x,
    long anchor_y,
    long card_width,
    long card_height,
    long work_left,
    long work_top,
    long work_right,
    long work_bottom
) noexcept {
    constexpr long kGap = 14;
    constexpr long kMargin = 8;
    const long available_width = std::max(0L, work_right - work_left);
    const long available_height = std::max(0L, work_bottom - work_top);
    const long width = std::min(std::max(0L, card_width), available_width);
    const long height = std::min(std::max(0L, card_height), available_height);

    long left = anchor_x + kGap;
    if (left + width > work_right - kMargin) {
        left = anchor_x - width - kGap;
    }
    long top = anchor_y - height - kGap;
    if (top < work_top + kMargin) {
        top = anchor_y + kGap;
    }
    const long min_left = work_left + kMargin;
    const long max_left = std::max(min_left, work_right - width - kMargin);
    const long min_top = work_top + kMargin;
    const long max_top = std::max(min_top, work_bottom - height - kMargin);
    left = std::clamp(left, min_left, max_left);
    top = std::clamp(top, min_top, max_top);
    return OverlayPlacement{left, top, width, height};
}

} // namespace noven::overlay
