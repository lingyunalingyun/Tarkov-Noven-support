#pragma once
#include "ui/FloorStack.h"
#include "ui/TabBar.h"
#include <algorithm>

namespace noven::ui {
// 单一 DIP 布局采样，绘制与鼠标命中共享；不依赖物理分辨率。
// One DIP layout sample shared by rendering and input, independent of physical resolution.
struct MapLayout final {
    D2D1_RECT_F content,search,strip,viewport;
    FloorStack stack;
    D2D1_RECT_F back;
    TabBarLayout maps;
    static float Ease(float progress) noexcept {
        const float t=1-std::clamp(progress,0.0F,1.0F);return 1-t*t*t;
    }
    static MapLayout Sample(float width,float height,const UiTheme& theme,float progress,std::size_t floorCount){
        const float left=theme.sidebarWidth+theme.contentPadding,right=width-theme.contentPadding;
        const float available=std::max(120.0F,right-left),bottom=std::max(300.0F,height-24);
        const bool narrow=available<640;
        const float rail=148;
        const float last=static_cast<float>(floorCount?floorCount-1:0),stackHeight=.32F+last*.2F;
        const float compact=rail-60;
        const float large=std::min({320.0F,available*.55F,(bottom-190)*.7F/stackHeight});
        const D2D1_POINT_2F overview{left+available*.5F-large*(1-last*.13F)*.5F,190+(bottom-190-large*stackHeight)*.5F};
        const D2D1_POINT_2F selected{left+52,244};const float t=Ease(progress);
        const float viewLeft=left+rail+12,viewTop=narrow?312.0F:290.0F;
        return {{left,180,right,bottom},{left,84,right,122},
            {viewLeft,186,right,viewTop-16},{viewLeft,viewTop,right,bottom},
            {{overview.x+(selected.x-overview.x)*t,overview.y+(selected.y-overview.y)*t},
                large+(compact-large)*t,floorCount,.13F*(1-t)},
            {left+8,188,left+44,224},{left,130,166,available/3,std::min(36.0F,available/9)}};
    }
};
}
