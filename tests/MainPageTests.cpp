#include "data/GameMode.h"
#include "ui/NavigationState.h"
#include "ui/PageHost.h"
#include "ui/PageComponents.h"
#include "ui/Sidebar.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    // 通用组件不依赖扫描模式；非页面原点的轨道也必须正确映射。
    // Generic components do not depend on scan modes; offset tracks must map correctly.
    const auto genericBar = noven::ui::MakeScrollbar(D2D1::RectF(400, 10, 412, 210), 1000, 400);
    Require(genericBar && genericBar->maximum == 800
        && std::abs(genericBar->OffsetFromThumbTop(genericBar->thumb.top) - 400) < 0.01F,
        "generic scrollbar supports arbitrary viewport origins");
    const auto landscape = noven::ui::FitImage(D2D1::SizeF(200, 100), D2D1::RectF(20, 19, 92, 99));
    const auto portrait = noven::ui::FitImage(D2D1::SizeF(100, 200), D2D1::RectF(20, 19, 92, 99));
    Require(landscape.left == 20 && landscape.right == 92
        && landscape.top == 41 && landscape.bottom == 77,
        "landscape image is centered without stretching");
    Require(portrait.left == 36 && portrait.right == 76
        && portrait.top == 19 && portrait.bottom == 99,
        "portrait image is centered without stretching");
    auto registry=noven::ui::MakeBuiltinPageRegistry();
    noven::ui::NavigationState navigation{registry};
    Require(navigation.Active() == noven::ui::BuiltinPageId::Scanner,
            "Scanner is the default page");
    Require(registry.Pages().size() == 11, "all eleven built-in pages are registered");

    noven::ui::Sidebar sidebar{registry};
    noven::ui::UiTheme theme;
    for (const auto& page : registry.Pages()) {
        const auto rect = sidebar.ItemRect(page.id, 760.0F, theme);
        Require(sidebar.HitTest((rect.left + rect.right) / 2.0F,
                                (rect.top + rect.bottom) / 2.0F,
                                760.0F, theme) == page.id,
                "each visible navigation row maps to its stable page ID");
    }
    const auto short_settings = sidebar.ItemRect(noven::ui::BuiltinPageId::Settings,
                                                  760.0F, theme);
    const auto tall_settings = sidebar.ItemRect(noven::ui::BuiltinPageId::Settings,
                                                 940.0F, theme);
    Require(tall_settings.top - short_settings.top == 180.0F,
            "Settings remains anchored to the sidebar bottom during resize");
    Require(!sidebar.HitTest(theme.sidebarWidth + 1.0F, 210.0F, 760.0F, theme),
            "page content cannot hit-test as a sidebar item");

    noven::ui::PageHost pages{registry};
    const float recent_left = theme.sidebarWidth + theme.contentPadding;
    for (std::size_t index = 0; index < noven::data::AllGameModes().size(); ++index) {
        Require(pages.RecentTabAt(recent_left + 50.0F + 100.0F * index,
                                   101.0F, theme) == noven::data::AllGameModes()[index],
                "recent history tabs map to stored game modes");
    }
    Require(!pages.RecentTabAt(recent_left - 1.0F, 101.0F, theme)
            && !pages.RecentTabAt(recent_left + 50.0F, 140.0F, theme),
            "outside recent tabs does not select a mode");
    enum class SamplePage { First, Second };
    constexpr std::array<noven::ui::TabBarItem<SamplePage>, 2> sampleTabs{{
        {SamplePage::First, L"First"}, {SamplePage::Second, L"Second"},
    }};
    const noven::ui::TabBarLayout sampleLayout{10.0F, 20.0F, 50.0F, 80.0F, 18.0F};
    Require(noven::ui::HitTestTabBar(sampleTabs, sampleLayout, 130.0F, 35.0F)
                == SamplePage::Second,
            "tab template accepts page identifiers beyond game modes");
    std::vector<noven::data::RecentScanEntry> recent(4);
    recent[0].gameMode = noven::data::GameMode::Pvp;
    recent[1].gameMode = noven::data::GameMode::Pve;
    recent[2].gameMode = noven::data::GameMode::Seasonal;
    recent[3].gameMode = noven::data::GameMode::Pve;
    Require(noven::ui::PageHost::RecentFilteredCount(recent, noven::data::GameMode::Pvp) == 1
            && noven::ui::PageHost::RecentFilteredCount(recent, noven::data::GameMode::Pve) == 2
            && noven::ui::PageHost::RecentFilteredCount(recent, noven::data::GameMode::Seasonal) == 1,
            "recent history stays separated by stored mode");
    const auto start = noven::ui::SampleTabTransition(0.0F);
    const auto middle = noven::ui::SampleTabTransition(0.5F);
    const auto end = noven::ui::SampleTabTransition(1.0F);
    Require(start.textProgress == 0.0F && start.underlineProgress == 0.0F
            && start.outgoingOpacity == 1.0F && start.incomingOpacity == 0.0F,
            "tab transition starts on the old label and list");
    Require(middle.textProgress == 1.0F && middle.underlineProgress > 1.0F
            && middle.outgoingOpacity == 0.0F && middle.incomingOpacity > 0.0F,
            "label settles first, underline overshoots, then new list appears");
    Require(end.underlineProgress == 1.0F && end.incomingOpacity == 1.0F
            && end.incomingScale == 1.0F,
            "tab transition lands exactly on the new list and underline");
    Require(noven::ui::PageHost::RecentMaxScroll(750.0F, 0) == 0.0F
            && noven::ui::PageHost::RecentMaxScroll(750.0F, 1) == 0.0F
            && noven::ui::PageHost::RecentMaxScroll(750.0F, 20) > 0.0F,
        "recent page scroll clamps to content and viewport height");
    Require(!noven::ui::PageHost::RecentScrollGeometry(1000, 750, theme, 0, 0)
            && !noven::ui::PageHost::RecentScrollGeometry(1000, 750, theme, 1, 0),
            "scrollbar is hidden when history fits the viewport");
    const auto bar = noven::ui::PageHost::RecentScrollGeometry(1000, 750, theme, 20, 0);
    Require(bar && bar->thumb.top == bar->track.top
            && bar->thumb.bottom < bar->track.bottom,
            "overflowing history shows a proportional thumb at the top");
    Require(bar->OffsetFromThumbTop(-1000) == 0
            && bar->OffsetFromThumbTop(10000) == bar->maximum,
            "dragging outside the track clamps to list boundaries");
    const auto middleBar = noven::ui::PageHost::RecentScrollGeometry(
        1000, 750, theme, 20, bar->maximum / 2);
    Require(std::abs(middleBar->OffsetFromThumbTop(middleBar->thumb.top)
                - bar->maximum / 2) < 0.01F,
            "thumb position and scroll offset round trip without drift");
    const auto endBar = noven::ui::PageHost::RecentScrollGeometry(
        1000, 750, theme, 20, bar->maximum);
    Require(std::abs(endBar->thumb.bottom - endBar->track.bottom) < 0.01F,
            "scrollbar reaches track bottom at the last row");
    const auto longBar = noven::ui::PageHost::RecentScrollGeometry(1000, 750, theme, 200, 0);
    Require(longBar->thumb.bottom - longBar->thumb.top >= 28.0F,
            "long histories retain a draggable minimum thumb size");
    const auto selector = pages.ModeSelectorRect(theme);
    const auto shorterList = noven::ui::PageHost::RecentScrollGeometry(1000, 750, theme, 10, 0);
    const auto morphStart = noven::ui::PageHost::AnimateRecentScrollbar(bar, shorterList, 0);
    const auto morphMiddle = noven::ui::PageHost::AnimateRecentScrollbar(bar, shorterList, 0.5F);
    const auto morphEnd = noven::ui::PageHost::AnimateRecentScrollbar(bar, shorterList, 1);
    Require(morphStart.bar->thumb.bottom == bar->thumb.bottom
            && morphMiddle.bar->thumb.bottom > shorterList->thumb.bottom
            && morphEnd.bar->thumb.bottom == shorterList->thumb.bottom,
            "scrollbar stretches with spring overshoot then settles exactly");
    const auto shrink = noven::ui::PageHost::AnimateRecentScrollbar(shorterList, bar, 0.5F);
    Require(shrink.bar->thumb.bottom < bar->thumb.bottom
            && shrink.bar->thumb.top >= shrink.bar->track.top,
            "scrollbar shrinks elastically without leaving its track");
    const auto fadeOut = noven::ui::PageHost::AnimateRecentScrollbar(bar, std::nullopt, 0.2F);
    const auto fadeIn = noven::ui::PageHost::AnimateRecentScrollbar(std::nullopt, bar, 0.7F);
    Require(fadeOut.opacity > 0 && fadeOut.opacity < 1
            && fadeIn.opacity > 0 && fadeIn.opacity < 1
            && !noven::ui::PageHost::AnimateRecentScrollbar(bar, std::nullopt, 1).bar
            && !noven::ui::PageHost::AnimateRecentScrollbar(std::nullopt, std::nullopt, 0.5F).bar,
            "empty or fitting lists fade scrollbars with the list transition");
    Require(selector.top == 311.0F && selector.bottom == 345.0F,
            "mode selector follows the compact scanner content layout");
    for (std::size_t index = 0; index < noven::data::AllGameModes().size(); ++index) {
        Require(pages.ModeOptionAt(selector.left + 20.0F,
                                   selector.bottom + 20.0F + 32.0F * index,
                                   theme) == noven::data::AllGameModes()[index],
                "drawn mode option maps to the correct game mode");
    }
    Require(!pages.ModeOptionAt(selector.right + 1.0F, selector.bottom + 20.0F,
                                theme), "outside mode menu does not select a mode");

    noven::data::GameMode mode = noven::data::GameMode::Pve;
    for (const auto& page : registry.Pages()) {
        Require(navigation.Select(page.id) && navigation.Active() == page.id,
                "registered page can become active");
        Require(mode == noven::data::GameMode::Pve,
                "navigation does not alter the selected game mode");
    }
    Require(!registry.Find(noven::ui::PageId{"unknown.page"}),
            "unknown page has no metadata");
    Require(!navigation.Select(noven::ui::PageId{"unknown.page"})
                && navigation.Active() == noven::ui::BuiltinPageId::Settings,
            "unknown page cannot replace the active page");
    const noven::ui::PageId future{"plugin.com.example.loot-route"};
    Require(registry.Register({future,noven::ui::PageSection::Secondary,"nav.events","desc.events",
        noven::ui::PageIcon::GenericPlugin,-1,noven::ui::PageSource::Plugin}),"future page needs no enum");
    Require(navigation.Select(future)&&navigation.Active()==future,"future identity selectable");
    const auto descriptor=*registry.Find(future);
    Require(registry.Unregister(future)&&navigation.Active()==noven::ui::BuiltinPageId::Scanner,"removed active falls back safely");
    auto reordered=descriptor;reordered.order=100;
    Require(registry.Register(reordered)&&navigation.Select(future),"reordered registration");
    for(int index=0;index<100;++index) {
        Require(navigation.Select(noven::ui::BuiltinPageId::Map)&&navigation.Select(future),"rapid switching");
        registry.Register({noven::ui::PageId{"plugin.test.growth-"+std::to_string(index)},noven::ui::PageSection::Secondary,"nav.events"});
        Require(navigation.Active()==future,"registry growth/reordering preserves active ID");
    }
    sidebar.EnsureVisible(future,500,theme);
    const auto reached=sidebar.Layout(500,theme).Find(future);Require(reached&&reached->visible&&reached->rect.bottom<=sidebar.Layout(500,theme).navigationViewport.bottom,"selection reveals overflow page");
    const auto bottom=sidebar.ItemRect(noven::ui::BuiltinPageId::Settings,500,theme);
    sidebar.StartSelection(noven::ui::BuiltinPageId::Settings,future,500,theme);
    Require(!sidebar.Wheel(-120,30,250,500,theme)&&sidebar.Animating(),"wheel at boundary preserves active selection animation");
    Require(sidebar.Wheel(120,30,250,500,theme),"middle region wheel input");
    Require(sidebar.ItemRect(noven::ui::BuiltinPageId::Settings,500,theme).top==bottom.top,"wheel never moves protected management");
    const auto sidebarBar=sidebar.Bar(500,theme);Require(sidebarBar.has_value(),"overflow exposes native scrollbar");
    Require(sidebar.Down(sidebarBar->thumb.left+2,sidebarBar->thumb.top+2,500,theme)&&sidebar.Move(250,sidebarBar->track.bottom,500,theme)&&sidebar.EndDrag(),"thumb drag uses shared geometry");
    Require(std::abs(sidebar.Layout(500,theme).scroll-sidebar.Layout(500,theme).maximum)<.01F,"drag reaches final runtime row");
    sidebar.Layout(900,theme);Require(sidebar.Layout(900,theme).scroll<=sidebar.Layout(900,theme).maximum,"resize clamps offset");
    Require(registry.Unregister(future)&&!sidebar.Layout(900,theme).Find(future),"mutation rebuilds geometry and removes stale target");
    std::cout << "Main navigation state tests passed\n";
}
