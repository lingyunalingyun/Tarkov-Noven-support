#pragma once

#include "ui/MainPage.h"
#include "ui/UiCanvas.h"
#include "ui/Theme.h"

#include <optional>

namespace noven::ui {

class Sidebar final {
public:
    [[nodiscard]] D2D1_RECT_F ItemRect(MainPage page, float height,
                                      const UiTheme& theme) const noexcept;
    [[nodiscard]] std::optional<MainPage> HitTest(float x, float y,
                                                  float height,
                                                  const UiTheme& theme) const noexcept;
    void Draw(const UiCanvas& canvas, const UiTheme& theme, float height,
              MainPage active, std::optional<MainPage> hovered,
              std::optional<MainPage> pressed) const;
};

} // namespace noven::ui
