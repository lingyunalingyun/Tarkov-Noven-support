#pragma once

#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace noven::ui {

struct LocaleInfo final {
    std::string locale;
    std::wstring name;
    std::wstring nameEnglish;
};

// 仅由 UI 线程初始化、切换和读取；不影响 OCR、目录或历史数据。
// Initialize, switch and read only on the UI thread; OCR, catalog and history remain independent.
class LocalizationService final {
public:
    bool DiscoverLocales(const std::filesystem::path& directory, std::wstring& error);
    bool SetLocale(std::string_view locale);
    [[nodiscard]] const std::string& ActiveLocale() const noexcept { return active_; }
    [[nodiscard]] const std::vector<LocaleInfo>& AvailableLocales() const noexcept { return available_; }
    [[nodiscard]] const std::vector<std::wstring>& Warnings() const noexcept { return warnings_; }
    [[nodiscard]] std::wstring Get(std::string_view key) const;
    using Argument = std::pair<std::wstring_view, std::wstring_view>;
    [[nodiscard]] std::wstring Format(std::string_view key, std::initializer_list<Argument> arguments) const;
private:
    using Dictionary = std::map<std::wstring, std::wstring>;
    std::map<std::string, Dictionary> dictionaries_;
    std::vector<LocaleInfo> available_;
    std::vector<std::wstring> warnings_;
    std::string active_{"zh-CN"};
};

// App 在创建 UI 前加载此共享实例；页面不读取文件，也不拥有语言包。
// App loads this shared instance before UI creation; pages neither read files nor own language packs.
LocalizationService& UiLocalization();
inline std::wstring Tr(std::string_view key) { return UiLocalization().Get(key); }

} // namespace noven::ui
