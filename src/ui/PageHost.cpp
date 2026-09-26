#include "ui/PageHost.h"
#include "ui/PageComponents.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cwchar>
#include <string>

namespace noven::ui {
namespace {

constexpr float kRecentTop = 139.0F;
constexpr float kRecentRowHeight = 130.0F;
constexpr std::array<TabBarItem<data::GameMode>, 3> kRecentTabs{{
    {data::GameMode::Pvp, L"PvP"},
    {data::GameMode::Pve, L"PvE"},
    {data::GameMode::Seasonal, L"PVPS"},
}};

TabBarLayout RecentTabLayout(const UiTheme& theme) noexcept {
    return {theme.sidebarWidth + theme.contentPadding, 80.0F, 123.0F,
            100.0F, 22.0F};
}

std::wstring Wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), result.data(), size);
    return result;
}

std::wstring Price(const std::optional<std::int64_t>& amount) {
    if (!amount) return L"未知";
    std::wstring digits = std::to_wstring(*amount);
    for (std::size_t pos = digits.size(); pos > 3; pos -= 3)
        digits.insert(pos - 3, 1, L',');
    return L"₽" + digits;
}

std::wstring LocalTime(std::int64_t unixMs) {
    const std::time_t seconds = static_cast<std::time_t>(unixMs / 1000);
    const std::time_t current = std::time(nullptr);
    std::tm local{}, today{};
    if (localtime_s(&local, &seconds) != 0 || localtime_s(&today, &current) != 0)
        return L"--:--";
    wchar_t buffer[24]{};
    if (local.tm_year == today.tm_year && local.tm_yday == today.tm_yday)
        std::swprintf(buffer, 24, L"%02d:%02d", local.tm_hour, local.tm_min);
    else
        std::swprintf(buffer, 24, L"%02d-%02d %02d:%02d",
                      local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
    return buffer;
}

std::wstring Mode(data::GameMode mode) {
    switch (mode) {
    case data::GameMode::Pvp: return L"PvP";
    case data::GameMode::Pve: return L"PvE";
    case data::GameMode::Seasonal: return L"Seasonal";
    }
    return L"?";
}

} // namespace

float PageHost::RecentMaxScroll(float height, std::size_t count) noexcept {
    return (std::max)(0.0F, static_cast<float>(count) * kRecentRowHeight
        - (std::max)(0.0F, height - kRecentTop - 22.0F));
}

std::optional<RecentScrollbar> PageHost::RecentScrollGeometry(
    float width, float height, const UiTheme& theme, std::size_t count,
    float scroll) noexcept {
    const float left = width - theme.contentPadding + 12.0F;
    return MakeScrollbar(D2D1::RectF(left, kRecentTop, left + 12.0F, height - 22.0F),
                         static_cast<float>(count) * kRecentRowHeight, scroll);
}

RecentScrollbarPose PageHost::AnimateRecentScrollbar(
    const std::optional<RecentScrollbar>& outgoing,
    const std::optional<RecentScrollbar>& incoming, float progress) noexcept {
    return SampleScrollbarTransition(outgoing, incoming, progress);
}

std::size_t PageHost::RecentFilteredCount(
    const std::vector<data::RecentScanEntry>& recent, data::GameMode mode) noexcept {
    return static_cast<std::size_t>(std::count_if(recent.begin(), recent.end(),
        [mode](const auto& entry) { return entry.gameMode == mode; }));
}

std::optional<data::GameMode> PageHost::RecentTabAt(
    float x, float y, const UiTheme& theme) const noexcept {
    return HitTestTabBar(kRecentTabs, RecentTabLayout(theme), x, y);
}

D2D1_RECT_F PageHost::ModeSelectorRect(const UiTheme& theme) const noexcept {
    const float x = theme.sidebarWidth + theme.contentPadding;
    return D2D1::RectF(x + 256, 311, x + 436, 345);
}

std::optional<data::GameMode> PageHost::ModeOptionAt(
    float x, float y, const UiTheme& theme) const noexcept {
    const auto selector = ModeSelectorRect(theme);
    if (x < selector.left || x >= selector.right || y < selector.bottom + 4
        || y >= selector.bottom + 100) return std::nullopt;
    return data::AllGameModes()[static_cast<std::size_t>((y - selector.bottom - 4) / 32)];
}

void PageHost::Draw(const UiCanvas& canvas, const UiTheme& theme, float width,
                    float height, MainPage active, const ScannerPageState& scanner,
                    bool modeMenuOpen, bool modeHovered,
                    std::optional<data::GameMode> hoveredMode,
                    const std::vector<data::RecentScanEntry>& recent,
                    data::GameMode recentFilter,
                    std::optional<data::GameMode> hoveredRecentTab,
                    float recentScroll,
                    const RecentTabTransition& recentTransition,
                    const ItemBitmapMap& images) const {
    const PageInfo* page = FindPage(active);
    if (page == nullptr) return;
    const float x = theme.sidebarWidth + theme.contentPadding;
    const float right = (std::max)(x + 1.0F, width - theme.contentPadding);

    // 所有页面共用紧凑标题行；内容与可选标签栏保持各自的布局。
    // All pages share a compact title row; content and optional tabs keep their own layout.
    DrawPageHeader(canvas, theme, x, right, page->chinese, page->english);

    if (active == MainPage::RecentScans) {
        const auto pose = SampleTabTransition(recentTransition.progress);
        DrawTabBar(canvas, theme, canvas.body, kRecentTabs, RecentTabLayout(theme),
                   recentFilter, hoveredRecentTab, recentTransition.outgoingMode,
                   recentTransition.progress, recentTransition.underlineIndex);

        const float bottom = (std::max)(kRecentTop, height - 22.0F);
        canvas.target.PushAxisAlignedClip(D2D1::RectF(x, kRecentTop, right, bottom),
                                          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const auto drawList = [&](data::GameMode mode, float scroll, float opacity, float scale) {
            if (opacity <= 0.0F) return;
            const float cardRight = (std::min)(right, x + 760.0F);
            const auto center = D2D1::Point2F((x + cardRight) / 2.0F, kRecentTop + 150.0F);
            const ScopedContentTransition transition(canvas, center, opacity, scale);
            if (RecentFilteredCount(recent, mode) == 0) {
                DrawEmptyState(canvas, theme,
                    D2D1::RectF(x, kRecentTop, (std::min)(right, x + 650), kRecentTop + 160),
                    right - 20, L"暂无扫描记录", L"按 F2 扫描物品后，结果会显示在这里。",
                    L"No recent scans · Press F2 on an item to build your local scan history.");
            } else {
                std::size_t filteredIndex = 0;
                for (const auto& entry : recent) {
                    if (entry.gameMode != mode) continue;
                    const float top = kRecentTop + static_cast<float>(filteredIndex++) * kRecentRowHeight
                        - scroll;
                    if (top + kRecentRowHeight < kRecentTop || top > bottom) continue;
                    const auto image = images.find(entry.stableItemId);
                    std::wstring detail = LocalTime(entry.scannedAtUnixMs) + L"  ·  "
                        + Mode(entry.gameMode) + L"  ·  "
                        + (entry.matchMode == data::RecentMatchMode::Strict
                            ? L"严格匹配" : L"可能匹配");
                    if (entry.ambiguous) detail += L" · 有歧义";
                    DrawItemCard(canvas, theme, D2D1::RectF(x, top, cardRight, top + 118),
                        {Wide(entry.canonicalName), detail, L"跳蚤出售  " + Price(entry.fleaPrice),
                         L"商人出售  " + Price(entry.bestTraderPrice)
                            + (entry.bestTraderName.empty() ? L"" : L" · " + Wide(entry.bestTraderName)),
                         image == images.end() ? nullptr : image->second.Get()});
                }
            }
        };
        if (recentTransition.progress < 1.0F) {
            drawList(recentTransition.outgoingMode, recentTransition.outgoingScroll,
                     pose.outgoingOpacity, pose.outgoingScale);
            drawList(recentFilter, recentScroll, pose.incomingOpacity, pose.incomingScale);
        } else {
            drawList(recentFilter, recentScroll, 1.0F, 1.0F);
        }
        canvas.target.PopAxisAlignedClip();
        const auto scrollbar = AnimateRecentScrollbar(
            RecentScrollGeometry(width, height, theme,
                RecentFilteredCount(recent, recentTransition.outgoingMode),
                recentTransition.outgoingScroll),
            RecentScrollGeometry(width, height, theme,
                RecentFilteredCount(recent, recentFilter), recentScroll),
            recentTransition.progress);
        DrawScrollbar(canvas, theme, scrollbar);
        return;
    }

    if (active != MainPage::Scanner) {
        const float card_right = (std::min)(right, x + 600.0F);
        canvas.Round(D2D1::RectF(x, 95, card_right, 245),
                     theme.cornerRadius, theme.surface);
        canvas.Circle(D2D1::Point2F(x + 39, 136), 14, theme.selected);
        canvas.Circle(D2D1::Point2F(x + 39, 136), 5, theme.accent);
        canvas.Text(page->chinese, canvas.label,
                    D2D1::RectF(x + 70, 112, card_right - 20, 148), theme.primaryText);
        canvas.Text(page->description, canvas.body,
                    D2D1::RectF(x + 24, 170, card_right - 24, 227), theme.secondaryText);
        return;
    }

    const float card_right = (std::min)(right, x + 620.0F);
    canvas.Round(D2D1::RectF(x, 95, card_right, 218),
                 theme.cornerRadius, theme.surface);
    canvas.Round(D2D1::RectF(x + 22, 117, x + 70, 165), 10.0F, theme.selected);
    canvas.Text(L"F2", canvas.label, D2D1::RectF(x + 30, 122, x + 68, 159), theme.accent);
    canvas.Text(L"快捷扫描已就绪", canvas.label,
                D2D1::RectF(x + 88, 111, card_right - 20, 148), theme.primaryText);
    canvas.Text(L"将鼠标悬停在物品上，按 F2 触发单次扫描。",
                canvas.body, D2D1::RectF(x + 88, 150, card_right - 20, 201),
                theme.secondaryText);

    canvas.Text(L"扫描器状态", canvas.label,
                D2D1::RectF(x, 243, card_right, 275), theme.primaryText);
    canvas.Text(L"SCANNER STATUS", canvas.smallFormat,
                D2D1::RectF(x, 271, card_right, 292), theme.secondaryText);
    canvas.Round(D2D1::RectF(x, 298, card_right, 482),
                 theme.cornerRadius, theme.surface);
    canvas.Text(L"当前游戏模式", canvas.body,
                D2D1::RectF(x + 22, 312, x + 218, 345), theme.secondaryText);
    canvas.Fill(D2D1::RectF(x + 22, 356, card_right - 22, 357), theme.divider);
    canvas.Text(L"OCR", canvas.body,
                D2D1::RectF(x + 22, 368, x + 170, 400), theme.secondaryText);
    canvas.Text(scanner.ocrReady ? L"Ready" : L"Unavailable", canvas.body,
                D2D1::RectF(x + 256, 368, card_right - 20, 400),
                scanner.ocrReady ? theme.primaryText : theme.secondaryText);
    canvas.Fill(D2D1::RectF(x + 22, 410, card_right - 22, 411), theme.divider);
    canvas.Text(L"Item Catalog", canvas.body,
                D2D1::RectF(x + 22, 422, x + 200, 454), theme.secondaryText);
    const std::wstring catalog_status = scanner.catalogItems == 0
        ? L"Unavailable" : L"Ready  ·  " + std::to_wstring(scanner.catalogItems) + L" items";
    canvas.Text(catalog_status, canvas.body,
                D2D1::RectF(x + 256, 422, card_right - 20, 455),
                scanner.catalogItems != 0 ? theme.primaryText : theme.secondaryText);

    if (height > 550.0F) {
        canvas.Text(L"经济数据在后台独立更新；扫描使用本地缓存。", canvas.smallFormat,
                    D2D1::RectF(x + 2, 502, right, 531), theme.secondaryText);
    }

    // 模式选择器与卡片共用 Direct2D 画面，避免子窗口在缩放时独立闪烁。
    // Paint the selector in the same Direct2D frame as the card to avoid
    // independent child-window repainting during resize.
    const auto selector = ModeSelectorRect(theme);
    canvas.Round(selector, 6.0F, modeHovered || modeMenuOpen ? theme.hover : theme.selected);
    canvas.Text(data::GameModeName(scanner.mode), canvas.body,
                D2D1::RectF(selector.left + 12, selector.top, selector.right - 28,
                            selector.bottom), theme.primaryText);
    canvas.brush.SetColor(theme.secondaryText);
    canvas.target.DrawLine(D2D1::Point2F(selector.right - 21, selector.top + 14),
                           D2D1::Point2F(selector.right - 15, selector.top + 20),
                           &canvas.brush, 1.5F);
    canvas.target.DrawLine(D2D1::Point2F(selector.right - 15, selector.top + 20),
                           D2D1::Point2F(selector.right - 9, selector.top + 14),
                           &canvas.brush, 1.5F);
    if (modeMenuOpen) {
        for (std::size_t index = 0; index < data::AllGameModes().size(); ++index) {
            const auto mode = data::AllGameModes()[index];
            const float top = selector.bottom + 4 + static_cast<float>(index) * 32;
            const auto row = D2D1::RectF(selector.left, top, selector.right, top + 32);
            canvas.Round(row, 4.0F, hoveredMode == mode || scanner.mode == mode
                ? theme.selected : theme.surface);
            canvas.Text(data::GameModeName(mode), canvas.body,
                        D2D1::RectF(row.left + 12, row.top, row.right - 12, row.bottom),
                        scanner.mode == mode ? theme.accent : theme.primaryText);
        }
    }
}

} // namespace noven::ui
