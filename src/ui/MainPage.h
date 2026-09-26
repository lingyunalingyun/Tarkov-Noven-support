#pragma once

#include "ui/localization/TextKeys.h"
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
    std::string_view titleKey;
    std::string_view descriptionKey;
    PageSection section;
};

inline constexpr std::array<PageInfo, 10> kPages{{
    {MainPage::Scanner, TextKey::NavScan, TextKey::DescScan, PageSection::Primary},
    {MainPage::Prices, TextKey::NavPrices, TextKey::DescPrices, PageSection::Primary},
    {MainPage::Hideout, TextKey::NavHideout, TextKey::DescHideout, PageSection::Primary},
    {MainPage::Tasks, TextKey::NavTasks, TextKey::DescTasks, PageSection::Primary},
    {MainPage::Map, TextKey::NavMap, TextKey::DescMap, PageSection::Primary},
    {MainPage::RaidHistory, TextKey::NavRaidHistory, TextKey::DescRaidHistory, PageSection::Secondary},
    {MainPage::Squad, TextKey::NavSquad, TextKey::DescSquad, PageSection::Secondary},
    {MainPage::Events, TextKey::NavEvents, TextKey::DescEvents, PageSection::Secondary},
    {MainPage::RecentScans, TextKey::NavRecentScans, TextKey::EmptyDescription, PageSection::Secondary},
    {MainPage::Settings, TextKey::NavSettings, TextKey::LanguageHint, PageSection::Bottom},
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
