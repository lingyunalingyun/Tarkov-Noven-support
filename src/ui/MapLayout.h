#pragma once
#include "ui/FloorStack.h"
#include <algorithm>

namespace noven::ui {
// 单一 DIP 布局采样，绘制与鼠标命中共享；不依赖物理分辨率。
// One DIP layout sample shared by rendering and input, independent of physical resolution.
struct MapLayout final {
    D2D1_RECT_F content,search,strip,viewport;
    FloorStack stack;
    static float Ease(float progress) noexcept {
        const float t=1-std::clamp(progress,0.0F,1.0F);return 1-t*t*t;
    }
    static MapLayout Sample(float width,float height,const UiTheme& theme,float progress){
        const float left=theme.sidebarWidth+theme.contentPadding,right=width-theme.contentPadding;
        const float available=std::max(120.0F,right-left),bottom=std::max(300.0F,height-24);
        const bool narrow=available<640;
        const float rail=std::clamp(available*.23F,90.0F,200.0F);
        const float compact=std::clamp(rail*.58F,55.0F,120.0F);
        const float large=std::min({320.0F,available*.55F,(bottom-150)*.75F});
        const D2D1_POINT_2F overview{left+available*.5F-large*.3F,150+(bottom-150-large*.92F)*.5F};
        const D2D1_POINT_2F selected{left+rail*.43F,160};const float t=Ease(progress);
        const float viewLeft=left+rail+12,viewTop=narrow?272.0F:250.0F;
        return {{left,140,right,bottom},{left,84,right,122},
            {viewLeft,146,right,viewTop-16},{viewLeft,viewTop,right,bottom},
            {{overview.x+(selected.x-overview.x)*t,overview.y+(selected.y-overview.y)*t},
                large+(compact-large)*t,4}};
    }
};
}
