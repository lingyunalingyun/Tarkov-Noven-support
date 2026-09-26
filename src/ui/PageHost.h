#pragma once

#include "data/GameMode.h"
#include "data/RecentScanStore.h"
#include "ui/MainPage.h"
#include "ui/TabBar.h"
#include "ui/Scrollbar.h"
#include "ui/UiCanvas.h"
#include "ui/Theme.h"

#include <cstddef>
#include <optional>
#include <vector>
#include <unordered_map>
#include <wrl/client.h>

namespace noven::ui {

using ItemBitmapMap = std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap>>;

struct ScannerPageState final {
    data::GameMode mode{data::GameMode::Pvp};
    bool ocrReady{};
    std::size_t catalogItems{};
};

struct RecentTabTransition final {
    data::GameMode outgoingMode{data::GameMode::Pvp};
    float progress{1.0F};
    float underlineIndex{};
    float outgoingScroll{};
};

// 轨道和滑块均为客户区 DIP；绘制与拖动使用同一映射。
// Track and thumb use client-area DIPs; drawing and dragging share one mapping.
using RecentScrollbar = ScrollbarGeometry;
using RecentScrollbarPose = ScrollbarPose;

// 仅绘制当前页；未接入的模块使用同一占位模板，不持有扫描服务。
// Render only the active page; future modules share a placeholder template
// and do not own scanner services.
class PageHost final {
public:
    [[nodiscard]] D2D1_RECT_F ModeSelectorRect(const UiTheme& theme) const noexcept;
    [[nodiscard]] std::optional<data::GameMode> ModeOptionAt(
        float x, float y, const UiTheme& theme) const noexcept;
    [[nodiscard]] std::optional<data::GameMode> RecentTabAt(
        float x, float y, const UiTheme& theme) const noexcept;
    [[nodiscard]] static std::size_t RecentFilteredCount(
        const std::vector<data::RecentScanEntry>& recent, data::GameMode mode) noexcept;
    void Draw(const UiCanvas& canvas, const UiTheme& theme, float width,
              float height, MainPage active, const ScannerPageState& scanner,
              bool modeMenuOpen, bool modeHovered,
              std::optional<data::GameMode> hoveredMode,
              const std::vector<data::RecentScanEntry>& recent,
              data::GameMode recentFilter,
              std::optional<data::GameMode> hoveredRecentTab,
              float recentScroll,
              const RecentTabTransition& recentTransition,
              const ItemBitmapMap& images) const;
    [[nodiscard]] static float RecentMaxScroll(float height, std::size_t count) noexcept;
    [[nodiscard]] static std::optional<RecentScrollbar> RecentScrollGeometry(
        float width, float height, const UiTheme& theme, std::size_t count,
        float scroll) noexcept;
    [[nodiscard]] static RecentScrollbarPose AnimateRecentScrollbar(
        const std::optional<RecentScrollbar>& outgoing,
        const std::optional<RecentScrollbar>& incoming, float progress) noexcept;
};

} // namespace noven::ui
