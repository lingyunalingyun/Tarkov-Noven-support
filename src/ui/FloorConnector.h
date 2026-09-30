#pragma once
#include "ui/UiCanvas.h"
#include <algorithm>
#include <cmath>

namespace noven::ui {
struct FloorConnector final {
    D2D1_POINT_2F start{},end{};
    void Draw(const UiCanvas& canvas,D2D1_COLOR_F color,float progress) const {
        const float vertical=std::abs(end.y-start.y),horizontal=std::abs(end.x-start.x);
        const float length=(vertical+horizontal)*std::clamp(progress,0.0F,1.0F);
        const D2D1_POINT_2F elbow{start.x,end.y};canvas.brush.SetColor(color);
        const float part=vertical>0?std::min(length/vertical,1.0F):1;
        canvas.target.DrawLine(start,{start.x,start.y+(end.y-start.y)*part},&canvas.brush,1.5F);
        if(length>vertical&&horizontal>0)
            canvas.target.DrawLine(elbow,{start.x+(end.x-start.x)*(length-vertical)/horizontal,end.y},
                &canvas.brush,1.5F);
    }
};
}
