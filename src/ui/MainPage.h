#pragma once

#include <array>
#include <string_view>

namespace noven::ui {

enum class MainPage {
    Scanner,
    Prices,
    Hideout,
    Tasks,
    Map,
    RaidHistory,
    Squad,
    Events,
    RecentScans,
    Settings,
};

enum class PageSection { Primary, Secondary, Bottom };

struct PageInfo final {
    MainPage id;
    std::wstring_view chinese;
    std::wstring_view english;
    std::wstring_view description;
    PageSection section;
};

inline constexpr std::array<PageInfo, 10> kPages{{
    {MainPage::Scanner, L"扫描", L"Scan", L"使用 F2 快捷扫描游戏内物品。", PageSection::Primary},
    {MainPage::Prices, L"物价", L"Prices", L"物价浏览功能将在后续阶段接入。", PageSection::Primary},
    {MainPage::Hideout, L"藏身处", L"Hideout", L"藏身处功能将在后续阶段接入。", PageSection::Primary},
    {MainPage::Tasks, L"任务", L"Tasks", L"任务追踪功能将在后续阶段接入。", PageSection::Primary},
    {MainPage::Map, L"地图", L"Map", L"地图功能将在后续阶段接入。", PageSection::Primary},
    {MainPage::RaidHistory, L"对局记录", L"Raid History", L"对局记录功能将在后续阶段接入。", PageSection::Secondary},
    {MainPage::Squad, L"开黑伙伴", L"Squad", L"开黑伙伴功能将在后续阶段接入。", PageSection::Secondary},
    {MainPage::Events, L"当前活动", L"Events", L"活动信息功能将在后续阶段接入。", PageSection::Secondary},
    {MainPage::RecentScans, L"最近扫描", L"Recent Scans", L"扫描记录功能将在后续阶段接入。", PageSection::Secondary},
    {MainPage::Settings, L"设置", L"Settings", L"设置功能将在后续阶段接入。", PageSection::Bottom},
}};

[[nodiscard]] constexpr const PageInfo* FindPage(MainPage id) noexcept {
    for (const PageInfo& page : kPages) {
        if (page.id == id) return &page;
    }
    return nullptr;
}

// 页面只保存导航身份；OCR、目录与经济服务继续由 App 持有。
// Pages keep only navigation identity; App continues to own OCR, catalog, and economy services.
class NavigationState final {
public:
    [[nodiscard]] constexpr MainPage Active() const noexcept { return active_; }
    [[nodiscard]] constexpr bool Select(MainPage page) noexcept {
        if (FindPage(page) == nullptr) return false;
        active_ = page;
        return true;
    }

private:
    MainPage active_{MainPage::Scanner};
};

} // namespace noven::ui
