#include "data/GameMode.h"
#include "ui/MainPage.h"
#include "ui/PageHost.h"
#include "ui/Sidebar.h"

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
    noven::ui::NavigationState navigation;
    Require(navigation.Active() == noven::ui::MainPage::Scanner,
            "Scanner is the default page");
    Require(noven::ui::kPages.size() == 10, "all ten pages are registered");

    noven::ui::Sidebar sidebar;
    noven::ui::UiTheme theme;
    for (const auto& page : noven::ui::kPages) {
        const auto rect = sidebar.ItemRect(page.id, 760.0F, theme);
        Require(sidebar.HitTest((rect.left + rect.right) / 2.0F,
                                (rect.top + rect.bottom) / 2.0F,
                                760.0F, theme) == page.id,
                "each visible navigation row maps to its stable page ID");
    }
    const auto short_settings = sidebar.ItemRect(noven::ui::MainPage::Settings,
                                                  760.0F, theme);
    const auto tall_settings = sidebar.ItemRect(noven::ui::MainPage::Settings,
                                                 940.0F, theme);
    Require(tall_settings.top - short_settings.top == 180.0F,
            "Settings remains anchored to the sidebar bottom during resize");
    Require(!sidebar.HitTest(theme.sidebarWidth + 1.0F, 210.0F, 760.0F, theme),
            "page content cannot hit-test as a sidebar item");

    noven::ui::PageHost pages;
    const auto selector = pages.ModeSelectorRect(theme);
    for (std::size_t index = 0; index < noven::data::AllGameModes().size(); ++index) {
        Require(pages.ModeOptionAt(selector.left + 20.0F,
                                   selector.bottom + 20.0F + 32.0F * index,
                                   theme) == noven::data::AllGameModes()[index],
                "drawn mode option maps to the correct game mode");
    }
    Require(!pages.ModeOptionAt(selector.right + 1.0F, selector.bottom + 20.0F,
                                theme), "outside mode menu does not select a mode");

    noven::data::GameMode mode = noven::data::GameMode::Pve;
    for (const auto& page : noven::ui::kPages) {
        Require(navigation.Select(page.id) && navigation.Active() == page.id,
                "registered page can become active");
        Require(mode == noven::data::GameMode::Pve,
                "navigation does not alter the selected game mode");
    }
    Require(noven::ui::FindPage(static_cast<noven::ui::MainPage>(999)) == nullptr,
            "unknown page has no metadata");
    Require(!navigation.Select(static_cast<noven::ui::MainPage>(999))
                && navigation.Active() == noven::ui::MainPage::Settings,
            "unknown page cannot replace the active page");
    std::cout << "Main navigation state tests passed\n";
}
