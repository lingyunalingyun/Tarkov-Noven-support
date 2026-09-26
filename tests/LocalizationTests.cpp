#include "ui/localization/LocalizationService.h"
#include "ui/localization/TextKeys.h"
#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <iostream>

void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int wmain(int argc, wchar_t** argv) try {
    using namespace noven::ui;
    Require(argc == 2, "locale source directory required");
    const auto directory = std::filesystem::temp_directory_path()
        / (L"NovenI18n-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(directory);
    const auto write = [&](const wchar_t* filename, const std::string& contents) {
        std::ofstream output(directory / filename, std::ios::binary); output << contents;
    };
    for (const auto& file : std::filesystem::directory_iterator(std::filesystem::path(argv[1])))
        if (file.path().extension() == L".json") std::filesystem::copy_file(file.path(), directory / file.path().filename());
    LocalizationService service;
    std::wstring error;
    Require(service.DiscoverLocales(directory, error), "source and English load");
    Require(service.ActiveLocale() == "zh-CN" && service.Get(TextKey::NavScan) == L"扫描", "Chinese is source and default");
    // 回归：中文分组文字不再拼接固定英语；重复副标题键已移除。
    // Regression: Chinese section labels have no fixed English suffix; duplicate subtitle keys are removed.
    Require(service.Get(TextKey::Primary) == L"主要功能" && service.Get(TextKey::More) == L"更多",
        "Chinese section labels use only the selected language");
    Require(service.Get("scan.status.subtitle") == L"[missing:scan.status.subtitle]"
        && service.Get("recent.empty.secondary") == L"[missing:recent.empty.secondary]",
        "obsolete duplicate subtitle keys are absent");
    Require(service.SetLocale("en-US") && service.Get(TextKey::NavScan) == L"Scan", "switch to English");
    Require(!service.SetLocale("missing") && service.ActiveLocale() == "en-US", "unknown selection preserves active locale");
    Require(service.Format(TextKey::CatalogReady, {{L"count", L"5441"}}) == L"Ready · 5441 items", "named formatting");
    Require(service.Get("not.registered") == L"[missing:not.registered]", "visible missing key");
    // 仅添加文件，不注册 C++ 枚举；元数据可在任意位置。
    // Add only a file, without a C++ enum registration; metadata can appear anywhere.
    write(L"xx-TEST.json", R"({"nav.scan":"Mock","_meta":{"nameEnglish":"Mock","locale":"xx-TEST","name":"测试"}})");
    write(L"en-US.json", R"({"catalog.ready":"Missing token","unused":"Ignored","_meta":{"name":"English","locale":"en-US","nameEnglish":"English"}})");
    write(L"bad-TEST.json", "{broken");
    Require(service.DiscoverLocales(directory, error), "malformed optional pack does not break source");
    Require(service.AvailableLocales().size() == 3 && service.SetLocale("xx-TEST"), "mock locale dynamically discovered");
    Require(service.Get(TextKey::NavScan) == L"Mock", "mock dictionary active");
    Require(service.SetLocale("en-US") && service.Get(TextKey::NavScan) == L"扫描", "missing English falls back to Chinese");
    Require(service.Format(TextKey::CatalogReady, {{L"count", L"3"}}) == L"就绪 · 3 个物品", "placeholder mismatch falls back");
    Require(service.Get("unused") == L"[missing:unused]", "unknown translation is ignored safely");
    Require(service.Format(TextKey::CatalogReady, {{L"count", L"{name}"}}) == L"就绪 · {name} 个物品", "replacement is not recursively expanded");
    Require(service.Format(TextKey::CatalogReady, {}) == L"就绪 · {count} 个物品", "missing argument remains visible");
    write(L"xx-TEST.json", R"({"_meta":{"locale":"xx-TEST","name":"\u4e2d\u6587","nameEnglish":"Test"},"nav.scan":"\u626b\u63cf"})");
    Require(service.DiscoverLocales(directory, error) && service.SetLocale("xx-TEST")
        && service.Get(TextKey::NavScan) == L"扫描", "escaped Unicode round trip");
    write(L"xx-TEST.json", R"({"_meta":{"locale":"xx-TEST","name":"Test","nameEnglish":"Test"},"nav.scan":"a","nav.scan":"b"})");
    Require(service.DiscoverLocales(directory, error) && !service.SetLocale("xx-TEST"), "duplicate JSON keys rejected");
    write(L"xx-TEST.json", std::string(1, '\xff'));
    Require(service.DiscoverLocales(directory, error) && !service.SetLocale("xx-TEST"), "invalid UTF8 pack skipped");
    write(L"zh-CN.json", "{}");
    Require(!service.DiscoverLocales(directory, error) && service.AvailableLocales().empty(), "invalid source fails initialization");
    write(L"zh-CN.json", "{broken");
    Require(!service.DiscoverLocales(directory, error), "malformed source JSON fails initialization");
    std::filesystem::remove(directory / L"zh-CN.json");
    Require(!service.DiscoverLocales(directory, error), "missing source fails initialization");
    Require(std::filesystem::weakly_canonical(directory).parent_path()
        == std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()), "isolated cleanup target");
    std::filesystem::remove_all(directory);
    std::cout << "Localization tests passed\n";
} catch (const std::exception& error) {
    std::cerr << "Localization test setup failed: " << error.what() << '\n';
    return 1;
}
