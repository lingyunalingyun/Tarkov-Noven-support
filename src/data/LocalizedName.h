#pragma once
#include <string>
#include <string_view>

namespace noven::data {
// 游戏名称归数据目录所有；UI 翻译只提供界面文案。
// Game names belong to catalogs; UI translations provide interface labels only.
inline const std::string& LocalizedName(const std::string& zh, const std::string& en,
    std::string_view locale) {
    return locale.starts_with("en") ? (en.empty() ? zh : en) : (zh.empty() ? en : zh);
}
}
