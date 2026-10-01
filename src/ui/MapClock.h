#pragma once
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <string>

namespace noven::ui {
// 沿用 DEV Time.jsx 的 UTC 换算；仅系统时钟参考，不读取游戏或对局状态。
// Follow DEV Time.jsx UTC conversion; a system-clock reference, never game/raid state.
inline int MapGameSeconds(std::int64_t utcMilliseconds,bool second=false) noexcept {
    constexpr std::int64_t day=86400000;
    const auto normalized=(utcMilliseconds%day+day)%day;
    return static_cast<int>((normalized*7+10800000+(second?43200000:0))%day/1000);
}
inline std::int64_t MapUtcMilliseconds() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline std::wstring MapClockText(std::int64_t utcMilliseconds,bool second=false){
    const int seconds=MapGameSeconds(utcMilliseconds,second);wchar_t text[9]{};
    swprintf_s(text,L"%02d:%02d:%02d",seconds/3600,seconds/60%60,seconds%60);return text;
}
}
