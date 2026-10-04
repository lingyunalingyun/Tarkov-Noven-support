#pragma once

#include "ui/MainPage.h"
#include "ui/UiCanvas.h"
#include "ui/Theme.h"

#include <optional>
#include <algorithm>

namespace noven::ui {

class Sidebar final {
public:
    void StartSelection(MainPage from,MainPage to,float height,const UiTheme& theme) {
        if(top_<0)top_=ItemRect(from,height,theme).top;
        from_=top_;target_=ItemRect(to,height,theme).top;progress_=0;
    }
    void Tick(float seconds) noexcept {
        progress_=std::clamp(progress_+seconds/0.24F,0.0F,1.0F);
        const float t=progress_*progress_*(3-2*progress_);top_=from_+(target_-from_)*t;
    }
    bool Animating() const noexcept {return progress_<1;}
    [[nodiscard]] D2D1_RECT_F ItemRect(MainPage page, float height,
                                      const UiTheme& theme) const noexcept;
    [[nodiscard]] std::optional<MainPage> HitTest(float x, float y,
                                                  float height,
                                                  const UiTheme& theme) const noexcept;
    void Draw(const UiCanvas& canvas, const UiTheme& theme, float height,
              MainPage active, std::optional<MainPage> hovered,
              std::optional<MainPage> pressed) const;
private:
    float top_{-1},from_{},target_{},progress_{1};
};

} // namespace noven::ui
