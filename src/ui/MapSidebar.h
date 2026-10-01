#pragma once
#include "ui/MapLayout.h"
#include "ui/MapClock.h"
#include "ui/localization/LocalizationService.h"

namespace noven::ui {
// 与页面展开共用采样，无独立计时器；出现/退出使用相同透明度与轻微下移。
// Share the page expansion sample, without another timer; entrance/exit use the same fade and short slide.
struct MapSidebarMotion final {
    float opacity,offset;
    static MapSidebarMotion Sample(float progress){const float alpha=MapLayout::Ease(progress);return {alpha,(1-alpha)*10};}
};

// 只绘制传入的元数据和参考时钟，不拥有目录或窗口，DIP 高度供布局/刷新共同判断。
// Render supplied metadata/reference clocks only; own no catalog/window, sharing DIP height with layout/refresh.
struct MapInformationCard final {
    static constexpr float Height=164;
    D2D1_RECT_F bounds;
    static std::optional<D2D1_RECT_F> Bounds(const MapLayout& layout){
        if(layout.content.bottom-layout.filters.back().bottom<Height+12)return std::nullopt;
        return D2D1_RECT_F{layout.filters.back().left,layout.content.bottom-Height-4,layout.filters.back().right,layout.content.bottom-4};
    }
    void Draw(const UiCanvas& canvas,const UiTheme& theme,std::wstring_view players,int duration,std::int64_t utc) const {
        const auto r=bounds;canvas.Round(r,8,theme.surface);
        const auto text=[&](std::wstring_view value,float y,D2D1_COLOR_F color){canvas.Text(value,canvas.smallFormat,{r.left+8,r.top+y,r.right-4,r.top+y+19},color);};
        text(Tr("map.info"),4,theme.accent);
        text(Tr("map.players")+L" · "+std::wstring(players),23,theme.primaryText);
        text(Tr("map.duration")+L" · "+std::to_wstring(duration)+L" "+Tr("map.minutes"),42,theme.primaryText);
        text(Tr("map.game_time"),64,theme.secondaryText);
        for(int slot=0;slot<2;++slot){const float y=r.top+87+static_cast<float>(slot)*34;
            canvas.Round({r.left+6,y,r.right-6,y+29},5,theme.selected);
            canvas.brush.SetColor(theme.accent);const D2D1_POINT_2F center{r.left+16,y+14};
            canvas.target.DrawEllipse(D2D1::Ellipse(center,5,5),&canvas.brush,1);
            canvas.target.DrawLine(center,{center.x,center.y-3},&canvas.brush,1);
            canvas.target.DrawLine(center,{center.x+3,center.y+1},&canvas.brush,1);
            canvas.Text(MapClockText(utc,slot==1),canvas.label,{r.left+26,y,r.right-7,y+29},theme.primaryText);
        }
    }
};
}
