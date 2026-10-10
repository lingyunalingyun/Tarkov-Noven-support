#pragma once
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace noven::ui {
// 二进制单位统一用于资源大小与磁盘空间；零是有效大小，不代表未知。
// Binary units are shared by resource sizes and disk space; zero is valid, not unknown.
inline std::wstring FormatBytes(std::uint64_t bytes) {
    constexpr const wchar_t* units[]{L"B",L"KiB",L"MiB",L"GiB",L"TiB",L"PiB",L"EiB"};
    long double value=static_cast<long double>(bytes);unsigned unit{};
    while(value>=1024&&unit<6){value/=1024;++unit;}
    std::wostringstream stream;stream.imbue(std::locale::classic());
    stream<<std::fixed<<std::setprecision(unit?1:0)<<value;
    auto text=stream.str();if(text.ends_with(L".0"))text.resize(text.size()-2);
    return text+L" "+units[unit];
}
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
