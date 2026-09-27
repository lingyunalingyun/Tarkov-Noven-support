#pragma once
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace noven::ui {
// 只格式化有效金额，不计算涨跌、不把缺失当零；调用方通过 i18n 组合标签和 Unknown。
// Format a valid amount only, without calculating changes or converting absence to zero; callers localize labels/Unknown.
// 固定分组规则沿用现有卢布显示；正负号、小数最多两位，显示舍入不影响排序原值。
// Preserve the existing rouble grouping style, sign and up to two decimals; display rounding never changes sort values.
inline std::wstring FormatSignedRoubles(double value) {
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(2) << std::abs(value);
    auto amount = stream.str();
    while (amount.ends_with(L"0")) amount.pop_back();
    if (amount.ends_with(L".")) amount.pop_back();
    const auto dot = amount.find(L'.');
    for (int i = static_cast<int>(dot == std::wstring::npos ? amount.size() : dot) - 3; i > 0; i -= 3)
        amount.insert(i, L",");
    return (value < 0 ? L"−₽" : value > 0 ? L"+₽" : L"₽") + amount;
}
}
