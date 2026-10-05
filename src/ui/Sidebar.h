#pragma once

#include "ui/NavigationState.h"
#include "ui/UiCanvas.h"
#include "ui/SidebarLayout.h"
#include "ui/Theme.h"
#include "ui/TabBar.h"

#include <optional>
#include <algorithm>

namespace noven::ui {

class Sidebar final {
public:
    explicit Sidebar(const PageRegistry& registry):registry_(registry){}
    const SidebarLayout& Layout(float height,const UiTheme& theme) const;
    void StartSelection(PageId from,PageId to,float height,const UiTheme& theme) {
        if(!Animating())top_=ItemRect(from,height,theme).top;
        from_=top_;target_=ItemRect(to,height,theme).top;progress_=0;
    }
    void Tick(float seconds) noexcept {
        progress_=std::clamp(progress_+seconds/0.24F,0.0F,1.0F);
        // 复用标签底线的阻尼弹性曲线；重选从当前姿态接续，落定后停止请求帧。
        // Reuse the damped tab-underline spring; retarget from the current pose and stop frames when settled.
        const float t=SampleTabTransition(progress_).underlineProgress;top_=from_+(target_-from_)*t;
    }
    bool Animating() const noexcept {return progress_<1;}
    [[nodiscard]] D2D1_RECT_F SelectionRect(PageId active,float height,const UiTheme& theme) const noexcept {
        auto rect=ItemRect(active,height,theme);
        if(Animating()){rect.top=top_;rect.bottom=top_+theme.navigationHeight;}
        return rect;
    }
    [[nodiscard]] D2D1_RECT_F ItemRect(PageId page, float height,
                                      const UiTheme& theme) const noexcept;
    [[nodiscard]] std::optional<PageId> HitTest(float x, float y,
                                                  float height,
                                                  const UiTheme& theme) const noexcept;
    void Draw(const UiCanvas& canvas, const UiTheme& theme, float height,
              PageId active, std::optional<PageId> hovered,
              std::optional<PageId> pressed) const;
private:
    const PageRegistry& registry_;
    mutable SidebarLayout layout_;
    mutable std::uint64_t revision_{static_cast<std::uint64_t>(-1)};
    mutable float height_{-1},width_{-1},rowHeight_{-1};
    float top_{-1},from_{},target_{},progress_{1};
};

} // namespace noven::ui
