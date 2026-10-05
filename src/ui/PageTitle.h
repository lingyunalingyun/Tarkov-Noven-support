#pragma once
#include "ui/PageRegistry.h"
#include "ui/localization/LocalizationService.h"
#include <windows.h>
namespace noven::ui {
inline std::wstring PluginWide(std::string_view value){
    if(value.empty())return {};const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!count)return {};std::wstring text(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),text.data(),count);return text;
}
inline std::wstring PageTitle(const PageDescriptor& page){
    return page.source==PageSource::Plugin&&!page.displayTitle.empty()?PluginWide(page.displayTitle):Tr(page.titleKey);
}
}
