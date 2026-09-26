#include "ui/MainWindowUi.h"
#include "ui/localization/LocalizationService.h"
#include <cstdlib>
#include <iostream>

void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int wmain(int argc, wchar_t** argv) try {
    using namespace noven::ui;
    Require(argc == 2, "locale directory required");
    std::wstring error;
    Require(UiLocalization().DiscoverLocales(argv[1], error), "UI source loads");
    const HWND window = CreateWindowExW(0, L"STATIC", L"Noven localization test", WS_OVERLAPPEDWINDOW,
        0, 0, 1100, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "hidden test window created");
    {
        MainWindowUi ui;
        Require(ui.Initialize(window, error), "native text formats initialize");
        ui.SetScannerState({noven::data::GameMode::Pve, true, 5441});
        const float scale = GetDpiForWindow(window) / 96.0F;
        RECT client{}; GetClientRect(window, &client);
        const float height = client.bottom / scale;
        const auto click = [&](float x, float y) {
            ui.MouseDown(static_cast<int>(x * scale), static_cast<int>(y * scale));
            return ui.MouseUp(static_cast<int>(x * scale), static_cast<int>(y * scale));
        };
        const UiTheme theme;
        Sidebar sidebar;
        const auto selectPage = [&](MainPage page) {
            const auto rect = sidebar.ItemRect(page, height, theme);
            Require(!click(rect.left + 30, (rect.top + rect.bottom) / 2), "page selection emits no GameMode change");
            Require(ui.ActivePage() == page, "page selection preserved");
            ui.Paint();
        };
        selectPage(MainPage::Settings);
        const auto& locales = UiLocalization().AvailableLocales();
        std::size_t english = 0;
        while (english < locales.size() && locales[english].locale != "en-US") ++english;
        Require(english < locales.size(), "English row discovered");
        Require(!click(theme.sidebarWidth + theme.contentPadding + 30, 175 + english * 42.0F),
            "language click emits no GameMode change");
        Require(UiLocalization().ActiveLocale() == "en-US", "Settings click switches locale immediately");
        for (const auto& page : kPages) selectPage(page.id);
        Require(!click(theme.sidebarWidth + theme.contentPadding + 30, 175), "Chinese selection leaves game mode alone");
        Require(UiLocalization().ActiveLocale() == "zh-CN", "Settings switches back to Chinese");
        selectPage(MainPage::Scanner);
        const PageHost host;
        const auto rect = host.ModeSelectorRect(theme);
        click(rect.left + 30, rect.top + 10);
        Require(!click(rect.left + 30, rect.bottom + 4 + 32 + 10), "PvE selection survived locale and page switches");
        ui.Paint();
    }
    DestroyWindow(window);
    std::cout << "Native localization interaction tests passed (hidden window, not visual acceptance)\n";
} catch (const std::exception& error) {
    std::cerr << "Localization UI test failed: " << error.what() << '\n';
    return 1;
}
