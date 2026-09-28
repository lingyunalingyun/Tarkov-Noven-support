#include "ui/PageHost.h"
#include "ui/PageComponents.h"
#include "ui/SearchBox.h"
#include "ui/Dropdown.h"
#include "ui/ItemTypeLabel.h"
#include "ui/ValueFormat.h"
#include "data/LocalizedName.h"

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
constexpr float kPricesTop = 188.0F;
constexpr float kPricesRowHeight = 118.0F;
// 窄窗口把筛选栏和价格分行；绘制与滚动必须使用相同断点。
// Narrow windows stack controls and prices; drawing and scrolling share this breakpoint.
bool NarrowPrices(float width, const UiTheme& theme) noexcept {
    return width - theme.sidebarWidth - 2.0F * theme.contentPadding < 690.0F;
}
float PricesTop(float width, const UiTheme& theme) noexcept {
    return NarrowPrices(width, theme) ? 228.0F : kPricesTop;
}
constexpr std::array<TabBarItem<data::GameMode>, 3> kRecentTabs{{
    {data::GameMode::Pvp, L"PvP"},
    {data::GameMode::Pve, L"PvE"},
    {data::GameMode::Seasonal, L"PVPS"},
}};
constexpr std::array<TabBarItem<data::GameMode>, 3> kPriceTabs{{
    {data::GameMode::Pvp, L"PvP"},
    {data::GameMode::Pve, L"PvE"},
    {data::GameMode::Seasonal, L"PVPS"},
}};

TabBarLayout RecentTabLayout(const UiTheme& theme) noexcept {
    return {theme.sidebarWidth + theme.contentPadding, 80.0F, 123.0F,
            100.0F, 22.0F};
}

TabBarLayout PriceTabLayout(const UiTheme& theme) noexcept {
    return {theme.sidebarWidth + theme.contentPadding, 128.0F, 171.0F,
            100.0F, 22.0F};
}

struct PriceToolbarLayout final {
    float left{};
    float top{};
    float buttonWidth{};
    float buttonHeight{};
    float gap{};
};

PriceToolbarLayout PriceToolbar(const UiTheme& theme, float width) noexcept {
    const float right = width - theme.contentPadding;
    const float buttonWidth = 118.0F;
    return {right - buttonWidth * 3.0F - 16.0F, NarrowPrices(width, theme) ? 182.0F : 132.0F,
            buttonWidth, 32.0F, 8.0F};
}

std::optional<D2D1_RECT_F> PriceControlRect(
    PriceToolbarControl control, const UiTheme& theme, float width) noexcept {
    const auto layout = PriceToolbar(theme, width);
    const auto header = [&](std::size_t index) {
        const float left = layout.left + static_cast<float>(index)
            * (layout.buttonWidth + layout.gap);
        return D2D1::RectF(left, layout.top, left + layout.buttonWidth,
            layout.top + layout.buttonHeight);
    };
    const auto option = [&](std::size_t index, std::size_t row) {
        const auto base = header(index);
        return DropdownLayout{base}.Option(row);
    };
    switch (control) {
    case PriceToolbarControl::SortDropdown:
    case PriceToolbarControl::FleaPrice:
    case PriceToolbarControl::FleaChange:
    case PriceToolbarControl::TraderPrice:
        return control == PriceToolbarControl::SortDropdown ? header(0)
            : option(0, control == PriceToolbarControl::FleaPrice ? 0
                : control == PriceToolbarControl::FleaChange ? 1 : 2);
    case PriceToolbarControl::OrderDropdown:
    case PriceToolbarControl::Ascending:
    case PriceToolbarControl::Descending:
        return control == PriceToolbarControl::OrderDropdown ? header(1)
            : option(1, control == PriceToolbarControl::Ascending ? 0 : 1);
    case PriceToolbarControl::SideDropdown:
    case PriceToolbarControl::TraderSell:
    case PriceToolbarControl::TraderBuy:
        return control == PriceToolbarControl::SideDropdown ? header(2)
            : option(2, control == PriceToolbarControl::TraderSell ? 0 : 1);
    case PriceToolbarControl::FleaSell: return std::nullopt;
    }
    return std::nullopt;
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
    if (!amount) return Tr(TextKey::Unknown);
    std::wstring digits = std::to_wstring(*amount);
    for (std::size_t pos = digits.size(); pos > 3; pos -= 3)
        digits.insert(pos - 3, 1, L',');
    return L"\x20BD" + digits;
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

std::wstring DisplayName(const data::ItemRecord& item) {
    return Wide(data::LocalizedName(item.nameZh,item.nameEn,UiLocalization().ActiveLocale()));
}

std::wstring PriceText(const std::optional<std::int64_t>& value) {
    if (!value) return Tr(TextKey::Unknown);
    std::wstring digits = std::to_wstring(*value);
    for (std::size_t pos = digits.size(); pos > 3; pos -= 3) digits.insert(pos - 3, 1, L',');
    return L"₽" + digits;
}

std::wstring EconomyFlea(const data::ItemEconomyInfo* economy) {
    if (!economy) return UiLocalization().Format(TextKey::PricesFlea,
        {{L"price", Tr(TextKey::Unknown)}});
    if (economy->fleaStatus == data::FleaStatus::Banned)
        return Tr(TextKey::PricesFleaBanned);
    if (economy->fleaStatus == data::FleaStatus::LockedOrUnavailable)
        return Tr(TextKey::PricesFleaUnavailable);
    return UiLocalization().Format(TextKey::PricesFlea,
        {{L"price", PriceText(economy->fleaPrice)}});
}

std::wstring EconomyTrader(const data::ItemEconomyInfo* economy,
                           data::PriceTraderSide side) {
    if (side == data::PriceTraderSide::Buy)
        return Tr(TextKey::PricesTraderBuyUnavailable);
    std::wstring price = economy && economy->bestTrader
        ? PriceText(economy->bestTrader->priceRoubles) : Tr(TextKey::Unknown);
    if (economy && economy->bestTrader && !economy->bestTrader->traderName.empty())
        price += L" \x00B7 " + Wide(economy->bestTrader->traderName);
    return UiLocalization().Format(TextKey::PricesTrader, {{L"price", price}});
}

} // namespace

float PageHost::RecentMaxScroll(float height, std::size_t count) noexcept {
    return (std::max)(0.0F, static_cast<float>(count) * kRecentRowHeight
        - (std::max)(0.0F, height - kRecentTop - 22.0F));
}

float PageHost::PriceRowHeight(float width, const UiTheme& theme) noexcept {
    return NarrowPrices(width, theme) ? 154.0F : kPricesRowHeight;
}

float PageHost::PriceListTop(float width, const UiTheme& theme) noexcept {
    return PricesTop(width, theme);
}

float PageHost::PriceMaxScroll(float height, std::size_t count,
                               float width, const UiTheme& theme, float extraHeight) noexcept {
    return (std::max)(0.0F, static_cast<float>(count) * PriceRowHeight(width, theme) + extraHeight
        - (std::max)(0.0F, height - PricesTop(width, theme) - 22.0F));
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

std::optional<RecentScrollbar> PageHost::PriceScrollGeometry(
    float width, float height, const UiTheme& theme, std::size_t count,
    float scroll, float extraHeight) noexcept {
    const float left = width - theme.contentPadding + 12.0F;
    return MakeScrollbar(D2D1::RectF(left, PricesTop(width, theme), left + 12.0F, height - 22.0F),
        static_cast<float>(count) * PriceRowHeight(width, theme) + extraHeight, scroll);
}

RecentScrollbarPose PageHost::AnimatePriceScrollbar(
    const std::optional<RecentScrollbar>& outgoing,
    const std::optional<RecentScrollbar>& incoming, float progress) noexcept {
    // Prices tabs use the same scrollbar transition as Recent Scans; only the data source differs.
    // 物价 TAB 与最近扫描共用同一滚动条过渡，区别只在于列表数据来源不同。
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

std::optional<data::GameMode> PageHost::PriceTabAt(
    float x, float y, const UiTheme& theme) const noexcept {
    return HitTestTabBar(kPriceTabs, PriceTabLayout(theme), x, y);
}

std::optional<PriceToolbarControl> PageHost::PriceControlAt(
    float x, float y, const UiTheme& theme, float width,
    std::optional<PriceDropdown> openDropdown) const noexcept {
    const auto header = [&](PriceToolbarControl control) {
        const auto rect = PriceControlRect(control, theme, width);
        return rect && HitTestDropdownRect(*rect, x, y);
    };
    if (header(PriceToolbarControl::SortDropdown))
        return PriceToolbarControl::SortDropdown;
    if (header(PriceToolbarControl::OrderDropdown))
        return PriceToolbarControl::OrderDropdown;
    if (header(PriceToolbarControl::SideDropdown))
        return PriceToolbarControl::SideDropdown;
    if (!openDropdown) return std::nullopt;
    for (const auto control : {
        PriceToolbarControl::FleaPrice, PriceToolbarControl::FleaChange,
        PriceToolbarControl::TraderPrice, PriceToolbarControl::Ascending,
        PriceToolbarControl::Descending, PriceToolbarControl::FleaSell,
        PriceToolbarControl::TraderSell, PriceToolbarControl::TraderBuy}) {
        const bool belongs =
            (*openDropdown == PriceDropdown::Sort
                && (control == PriceToolbarControl::FleaPrice
                    || control == PriceToolbarControl::FleaChange
                    || control == PriceToolbarControl::TraderPrice))
            || (*openDropdown == PriceDropdown::Order
                && (control == PriceToolbarControl::Ascending
                    || control == PriceToolbarControl::Descending))
            || (*openDropdown == PriceDropdown::Side
                && (control == PriceToolbarControl::FleaSell
                    || control == PriceToolbarControl::TraderSell
                    || control == PriceToolbarControl::TraderBuy));
        if (!belongs) continue;
        const auto rect = PriceControlRect(control, theme, width);
        if (rect && HitTestDropdownRect(*rect, x, y)) {
            // 涨跌排序暂时没有历史价差数据，因此保留展示但不响应点击。
            // Change sorting is visible but disabled until historical delta data exists.
            return control;
        }
    }
    return std::nullopt;
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
                    data::GameMode priceMode,
                    std::optional<data::GameMode> hoveredPriceTab,
                    float priceScroll,
                    const PriceTabTransition& priceTransition,
                    const PriceSearchTransition& searchTransition,
                    const PriceDetailsState& details,
                    std::optional<PriceToolbarControl> hoveredPriceControl,
                    std::optional<PriceDropdown> openPriceDropdown,
                    float priceDropdownProgress,
                    data::PriceSortMode priceSort,
                    bool priceSortDescending,
                    data::PriceTraderSide priceTraderSide,
                    const SearchBox& priceSearch,
                    bool priceCaretVisible,
                    const std::vector<data::PriceRow>& prices,
                    const ItemBitmapMap& images) const {
    const PageInfo* page = FindPage(active);
    if (page == nullptr) return;
    const float x = theme.sidebarWidth + theme.contentPadding;
    const float right = (std::max)(x + 1.0F, width - theme.contentPadding);

    // 所有页面共用紧凑标题行；内容与可选标签栏保持各自的布局。
    // All pages share a compact title row; content and optional tabs keep their own layout.
    DrawPageHeader(canvas, theme, x, right, Tr(page->titleKey));
    if (active == MainPage::Settings) return;

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
                    right - 20, Tr(TextKey::EmptyTitle), Tr(TextKey::EmptyDescription));
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
                            ? Tr(TextKey::Strict) : Tr(TextKey::Possible));
                    if (entry.ambiguous) detail += L" · " + Tr(TextKey::Ambiguous);
                    DrawItemCard(canvas, theme, D2D1::RectF(x, top, cardRight, top + 118),
                        {Wide(entry.canonicalName), detail, UiLocalization().Format(TextKey::FleaSale, {{L"price", Price(entry.fleaPrice)}}),
                         UiLocalization().Format(TextKey::TraderSale, {{L"price", Price(entry.bestTraderPrice)}})
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

    if (active == MainPage::Prices) {
        const float listTop = PricesTop(width, theme);
        const float rowHeight = PriceRowHeight(width, theme);
        const auto pose = SampleTabTransition(priceTransition.progress);
        const D2D1_RECT_F search = D2D1::RectF(x, 84, right, 122);
        // 绘制实际输入组件，禁止用文本副本重建并丢失游标/选中状态。
        // Draw the actual input component; rebuilding from text loses caret/selection state.
        priceSearch.Draw(canvas, theme, search, Tr(TextKey::PricesSearchPlaceholder), priceCaretVisible);
        const auto& priceQuery=priceSearch.Text();
        DrawTabBar(canvas, theme, canvas.body, kPriceTabs, PriceTabLayout(theme),
            priceMode, hoveredPriceTab, priceTransition.outgoingMode,
            priceTransition.progress, priceTransition.underlineIndex);
        const auto drawHeader = [&](PriceToolbarControl control,
                                    std::wstring_view label, bool selected) {
            const auto rect = PriceControlRect(control, theme, width);
            if (!rect) return;
            DrawDropdownHeader(canvas, theme, *rect, label, selected,
                hoveredPriceControl == control);
        };
        drawHeader(PriceToolbarControl::SortDropdown,
            Tr(priceSort == data::PriceSortMode::TraderPrice
                ? TextKey::PricesSortTrader : priceSort == data::PriceSortMode::FleaChange
                ? TextKey::PricesSortFleaChange : TextKey::PricesSortFlea),
            openPriceDropdown == PriceDropdown::Sort);
        drawHeader(PriceToolbarControl::OrderDropdown,
            Tr(priceSortDescending ? TextKey::PricesOrderDescending
                                   : TextKey::PricesOrderAscending),
            openPriceDropdown == PriceDropdown::Order);
        drawHeader(PriceToolbarControl::SideDropdown,
            Tr(priceTraderSide == data::PriceTraderSide::Buy
                ? TextKey::PricesTraderBuy : TextKey::PricesTraderSell),
            openPriceDropdown == PriceDropdown::Side);
        const float bottom = (std::max)(listTop, height - 22.0F);
        canvas.target.PushAxisAlignedClip(D2D1::RectF(x, listTop, right, bottom),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float cardRight = right;
        const auto center = D2D1::Point2F((x + cardRight) / 2.0F, listTop + 150.0F);
        {
            // 过渡只作用于价格列表；滚动条必须在作用域结束后独立绘制。
            // The transition applies only to the price list; draw the scrollbar after this scope.
            const ScopedContentTransition transition(canvas, center, pose.incomingOpacity, pose.incomingScale);
        if (prices.empty() && (searchTransition.progress >= 0.28F || searchTransition.rows.empty())) {
            DrawEmptyState(canvas, theme, D2D1::RectF(x, listTop, cardRight, listTop + 160),
                cardRight - 20, priceQuery.empty() ? Tr(TextKey::PricesEmptyTitle)
                    : Tr(TextKey::PricesNoResultsTitle),
                priceQuery.empty() ? Tr(TextKey::PricesEmptyDescription)
                    : Tr(TextKey::PricesNoResultsDescription));
        } else {
            const auto expanded = std::find_if(prices.begin(), prices.end(), [&](const auto& row) {
                return row.item->id == details.id;
            });
            const auto expandedIndex = static_cast<std::size_t>(expanded - prices.begin());
            const auto drawRow = [&](const data::PriceRow& row, float slot, float opacity) {
                const bool selected = row.item->id == details.id;
                const float extra = selected ? details.extent : 0;
                const float top = listTop + slot * rowHeight - priceScroll
                    + (slot > static_cast<float>(expandedIndex) ? details.extent : 0);
                if (opacity <= 0 || top + rowHeight + extra < listTop || top > bottom) return;
                const ScopedContentTransition fade(canvas, center, opacity, 1);
                const auto image = images.find(row.item->id);
                const auto* economy = row.economy ? &*row.economy : nullptr;
                std::wstring detail = std::to_wstring(row.item->width) + L"×"
                    + std::to_wstring(row.item->height) + L"  ·  ";
                if (priceSort == data::PriceSortMode::FleaChange) {
                    detail += UiLocalization().Format(TextKey::PricesChangeAmount,
                        {{L"amount", economy && economy->fleaChangeAmount
                            ? FormatSignedRoubles(*economy->fleaChangeAmount) : Tr(TextKey::Unknown)}});
                } else detail += economy && economy->valuePerSlot
                    ? UiLocalization().Format(TextKey::PricesValuePerSlot,
                        {{L"price", PriceText(static_cast<std::int64_t>(*economy->valuePerSlot))}})
                    : UiLocalization().Format(TextKey::PricesValuePerSlot,
                        {{L"price", Tr(TextKey::Unknown)}});
                if (extra > 0)
                    canvas.Round(D2D1::RectF(x, top, cardRight, top + rowHeight - 10 + extra),
                        theme.cornerRadius, theme.surface);
                const auto typeKey = ItemTypeKey(row.item->types);
                const auto tag = typeKey.empty() ? std::wstring{} : Tr(typeKey);
                DrawItemCard(canvas, theme, D2D1::RectF(x, top, cardRight, top + rowHeight - 10),
                     {DisplayName(*row.item), detail, EconomyFlea(economy), EconomyTrader(economy, priceTraderSide),
                     image == images.end() ? nullptr : image->second.Get(), tag}, NarrowPrices(width, theme));
                if (extra > 0) {
                    canvas.target.PushAxisAlignedClip(D2D1::RectF(x, top + rowHeight - 10,
                        cardRight, top + rowHeight - 10 + extra), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    // 悬浮只读取当前快照，不触发历史下载。
                    // Hover reads the current snapshot without triggering history downloads.
                    DrawPriceHistory(canvas, theme,
                        D2D1::RectF(x, top + rowHeight - 10, cardRight, top + rowHeight - 10 + kPriceDetailsHeight),
                        details, listTop);
                    canvas.target.PopAxisAlignedClip();
                }
            };
            if (searchTransition.progress < 1) {
                for (const auto& row : searchTransition.rows) {
                    const auto position = SampleListReflow(row.fromSlot, row.toSlot, row.opacity,
                        row.retained, searchTransition.progress);
                    drawRow(row.row, position.slot, position.opacity);
                }
            } else {
                for (std::size_t i = 0; i < prices.size(); ++i)
                    drawRow(prices[i], static_cast<float>(i), 1);
            }
            }
        }
        canvas.target.PopAxisAlignedClip();
        const auto scrollbar = AnimatePriceScrollbar(
            PriceScrollGeometry(width, height, theme, priceTransition.outgoingCount,
                priceTransition.outgoingScroll),
            PriceScrollGeometry(width, height, theme, prices.size(), priceScroll,
                DetailsScrollExtra(details, prices.size(), PriceRowHeight(width, theme),
                    (std::max)(0.0F, height - PriceListTop(width, theme) - 22))),
            priceTransition.progress);
        DrawScrollbar(canvas, theme, scrollbar);
        if (openPriceDropdown && priceDropdownProgress > 0.0F) {
            const auto dropdownPose = SampleDropdownTransition(priceDropdownProgress);
            const auto dropdownControl = *openPriceDropdown == PriceDropdown::Sort
                ? PriceToolbarControl::SortDropdown
                : *openPriceDropdown == PriceDropdown::Order
                    ? PriceToolbarControl::OrderDropdown
                    : PriceToolbarControl::SideDropdown;
            const auto base = PriceControlRect(dropdownControl, theme, width);
            if (base) {
                const auto menuAnchor = D2D1::Point2F((base->left + base->right) / 2.0F,
                    base->bottom);
                const ScopedContentTransition transition(
                    canvas, menuAnchor, dropdownPose.opacity, dropdownPose.scale);
                const std::size_t rows = *openPriceDropdown == PriceDropdown::Sort ? 3 : 2;
                DrawDropdownPanel(canvas, theme, DropdownLayout{*base}, rows);
                const auto drawOption = [&](PriceToolbarControl control,
                                            std::wstring_view label, bool selected,
                                            bool enabled) {
                    const auto rect = PriceControlRect(control, theme, width);
                    if (!rect) return;
                    DrawDropdownOption(canvas, theme, *rect, label, selected, enabled,
                        hoveredPriceControl == control);
                };
                if (*openPriceDropdown == PriceDropdown::Sort) {
                    drawOption(PriceToolbarControl::FleaPrice, Tr(TextKey::PricesSortFlea),
                        priceSort == data::PriceSortMode::FleaPrice, true);
                    drawOption(PriceToolbarControl::FleaChange, Tr(TextKey::PricesSortFleaChange),
                        priceSort == data::PriceSortMode::FleaChange, true);
                    drawOption(PriceToolbarControl::TraderPrice, Tr(TextKey::PricesSortTrader),
                        priceSort == data::PriceSortMode::TraderPrice, true);
                } else if (*openPriceDropdown == PriceDropdown::Order) {
                    drawOption(PriceToolbarControl::Ascending, Tr(TextKey::PricesOrderAscending),
                        !priceSortDescending, true);
                    drawOption(PriceToolbarControl::Descending, Tr(TextKey::PricesOrderDescending),
                        priceSortDescending, true);
                } else {
                    drawOption(PriceToolbarControl::TraderSell, Tr(TextKey::PricesTraderSell),
                        priceTraderSide == data::PriceTraderSide::Sell, true);
                    drawOption(PriceToolbarControl::TraderBuy, Tr(TextKey::PricesTraderBuy),
                        priceTraderSide == data::PriceTraderSide::Buy, true);
                }
            }
        }
        return;
    }

    if (active != MainPage::Scanner) {
        const float card_right = (std::min)(right, x + 600.0F);
        canvas.Round(D2D1::RectF(x, 95, card_right, 245),
                     theme.cornerRadius, theme.surface);
        canvas.Circle(D2D1::Point2F(x + 39, 136), 14, theme.selected);
        canvas.Circle(D2D1::Point2F(x + 39, 136), 5, theme.accent);
        canvas.Text(Tr(page->titleKey), canvas.label,
                    D2D1::RectF(x + 70, 112, card_right - 20, 148), theme.primaryText);
        canvas.Text(Tr(page->descriptionKey), canvas.body,
                    D2D1::RectF(x + 24, 170, card_right - 24, 227), theme.secondaryText);
        return;
    }

    const float card_right = (std::min)(right, x + 620.0F);
    canvas.Round(D2D1::RectF(x, 95, card_right, 218),
                 theme.cornerRadius, theme.surface);
    canvas.Round(D2D1::RectF(x + 22, 117, x + 70, 165), 10.0F, theme.selected);
    canvas.Text(L"F2", canvas.label, D2D1::RectF(x + 30, 122, x + 68, 159), theme.accent);
    canvas.Text(Tr(TextKey::ScanReady), canvas.label,
                D2D1::RectF(x + 88, 111, card_right - 20, 148), theme.primaryText);
    canvas.Text(Tr(TextKey::ScanHint),
                canvas.body, D2D1::RectF(x + 88, 150, card_right - 20, 201),
                theme.secondaryText);

    canvas.Text(Tr(TextKey::ScannerStatus), canvas.label,
                D2D1::RectF(x, 243, card_right, 275), theme.primaryText);
    canvas.Round(D2D1::RectF(x, 298, card_right, 482),
                 theme.cornerRadius, theme.surface);
    canvas.Text(Tr(TextKey::GameMode), canvas.body,
                D2D1::RectF(x + 22, 312, x + 218, 345), theme.secondaryText);
    canvas.Fill(D2D1::RectF(x + 22, 356, card_right - 22, 357), theme.divider);
    canvas.Text(L"OCR", canvas.body,
                D2D1::RectF(x + 22, 368, x + 170, 400), theme.secondaryText);
    canvas.Text(Tr(scanner.ocrReady ? TextKey::Ready : TextKey::Unavailable), canvas.body,
                D2D1::RectF(x + 256, 368, card_right - 20, 400),
                scanner.ocrReady ? theme.primaryText : theme.secondaryText);
    canvas.Fill(D2D1::RectF(x + 22, 410, card_right - 22, 411), theme.divider);
    canvas.Text(Tr(TextKey::Catalog), canvas.body,
                D2D1::RectF(x + 22, 422, x + 200, 454), theme.secondaryText);
    const std::wstring catalog_status = scanner.catalogItems == 0
        ? Tr(TextKey::Unavailable) : UiLocalization().Format(TextKey::CatalogReady,
            {{L"count", std::to_wstring(scanner.catalogItems)}});
    canvas.Text(catalog_status, canvas.body,
                D2D1::RectF(x + 256, 422, card_right - 20, 455),
                scanner.catalogItems != 0 ? theme.primaryText : theme.secondaryText);

    if (height > 550.0F) {
        canvas.Text(Tr(TextKey::EconomyHint), canvas.smallFormat,
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
