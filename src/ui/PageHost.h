#pragma once

#include "data/GameMode.h"
#include "ui/MainPage.h"
#include "ui/UiCanvas.h"
#include "ui/Theme.h"

#include <cstddef>
#include <optional>

namespace noven::ui {

struct ScannerPageState final {
    data::GameMode mode{data::GameMode::Pvp};
    bool ocrReady{};
    std::size_t catalogItems{};
};

// 仅绘制当前页；未接入的模块使用同一占位模板，不持有扫描服务。
// Render only the active page; future modules share a placeholder template
// and do not own scanner services.
class PageHost final {
public:
    [[nodiscard]] D2D1_RECT_F ModeSelectorRect(const UiTheme& theme) const noexcept;
    [[nodiscard]] std::optional<data::GameMode> ModeOptionAt(
        float x, float y, const UiTheme& theme) const noexcept;
    void Draw(const UiCanvas& canvas, const UiTheme& theme, float width,
              float height, MainPage active, const ScannerPageState& scanner,
              bool modeMenuOpen, bool modeHovered,
              std::optional<data::GameMode> hoveredMode) const;
};

} // namespace noven::ui
