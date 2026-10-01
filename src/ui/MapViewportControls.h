#pragma once
#include "ui/UiCanvas.h"
#include "ui/Theme.h"
#include <cmath>

namespace noven::ui {
// 视口右下角重置控件，绘制/命中使用同一个圆，不属于地图坐标变换。
// Bottom-right reset control shares a circle for drawing/hits, outside the map-space transform.
struct MapResetButton final {
    D2D1_RECT_F viewport;
    D2D1_POINT_2F Center() const noexcept{return {viewport.right-30,viewport.bottom-42};}
    bool Hit(D2D1_POINT_2F p) const noexcept {const auto c=Center();return std::hypot(p.x-c.x,p.y-c.y)<=18;}
    void Draw(const UiCanvas& canvas,const UiTheme& theme) const {
        const auto center=Center();canvas.Circle(center,18,theme.surface);canvas.brush.SetColor(theme.primaryText);
        D2D1_POINT_2F previous{center.x+8,center.y};
        for(int step=1;step<=16;++step){const float angle=static_cast<float>(step)*4.7F/16;
            const D2D1_POINT_2F next{center.x+8*std::cos(angle),center.y+8*std::sin(angle)};
            canvas.target.DrawLine(previous,next,&canvas.brush,1.7F);previous=next;}
        canvas.target.DrawLine(previous,{previous.x-4,previous.y-3},&canvas.brush,1.7F);
        canvas.target.DrawLine(previous,{previous.x-3,previous.y+4},&canvas.brush,1.7F);
    }
};
}
