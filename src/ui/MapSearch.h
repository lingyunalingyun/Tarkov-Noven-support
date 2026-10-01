#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>

namespace noven::ui {
// # 查询只解释标记名；普通查询由页面解释地图、楼层和任务内容，不混入图标类型。
// Hash queries target marker names; the page resolves ordinary map/floor/task content, not icon types.
struct MapSearchQuery final {
    bool markers{};
    std::wstring term;
    static std::wstring Fold(std::wstring_view value){
        std::wstring result(value);for(auto& c:result)c=static_cast<wchar_t>(std::towlower(c));return result;
    }
    explicit MapSearchQuery(std::wstring_view value){
        while(!value.empty()&&std::iswspace(value.front()))value.remove_prefix(1);
        while(!value.empty()&&std::iswspace(value.back()))value.remove_suffix(1);
        markers=value.starts_with(L"#");if(markers)value.remove_prefix(1);
        while(!value.empty()&&std::iswspace(value.front()))value.remove_prefix(1);
        term=Fold(value);
    }
    bool Matches(std::wstring_view folded) const {return folded.find(term)!=std::wstring_view::npos;}
};
}
