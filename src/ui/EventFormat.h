#pragma once
#include "events/EventTypes.h"
#include "ui/localization/LocalizationService.h"
#include "ui/localization/TextKeys.h"
#include <windows.h>

namespace noven::ui {
inline std::wstring EventWide(std::string_view text) {
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring value(size,L'\0');if(size)MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),value.data(),size);return value;
}
inline std::string EventUtf8(std::wstring_view text) {
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string value(size,'\0');if(size)WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),value.data(),size,nullptr,nullptr);return value;
}
// 变更区只展示有界预览；不删改缓存事实，原始支持证据通过来源链接查看。
// Show bounded change previews without altering cached facts; source links expose the supporting record.
inline std::wstring EventEvidencePreview(std::string_view text) {
    auto value=EventWide(text);
    if(value.size()>160){std::size_t size=160;if(value[size-1]>=0xD800&&value[size-1]<=0xDBFF)--size;value.resize(size);value+=L"…";}
    return value;
}
inline std::wstring EventStatusText(events::EventStatus status) {
    switch(status){case events::EventStatus::Active:return Tr(TextKey::EventActive);
        case events::EventStatus::Upcoming:return Tr(TextKey::EventUpcoming);case events::EventStatus::Ended:return Tr(TextKey::EventEnded);
        default:return Tr(TextKey::Unknown);}
}
inline std::wstring EventScopeText(const events::EventRecord& event) {
    std::wstring result;for(auto mode:event.modes) {
        if(!result.empty())result+=L" / ";
        result+=mode==events::EventMode::PvP?Tr(TextKey::RaidPvp):mode==events::EventMode::PvE?Tr(TextKey::RaidPve):Tr(TextKey::EventSeasonal);
    }return result.empty()?Tr(TextKey::EventUnspecified):result;
}
// UTC 秒转换为用户本地时间；缺失时间不会被替换成发布时间或 epoch。
// Convert UTC seconds to user-local time; missing values never become publication time or epoch.
inline std::wstring EventTimeText(std::optional<events::Timestamp> value) {
    if(!value||*value<0||*value>253402300799LL)return Tr(TextKey::Unknown);
    ULARGE_INTEGER ticks{};ticks.QuadPart=(static_cast<ULONGLONG>(*value)+11644473600ULL)*10000000ULL;
    FILETIME file{ticks.LowPart,ticks.HighPart};SYSTEMTIME utc{},local{};
    if(!FileTimeToSystemTime(&file,&utc)||!SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local))return Tr(TextKey::Unknown);
    wchar_t text[32]{};swprintf_s(text,L"%04u-%02u-%02u %02u:%02u",local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute);return text;
}
}
