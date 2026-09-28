#pragma once
#include "ui/UiCanvas.h"
#include "ui/Theme.h"

namespace noven::ui {
enum class NavigationGlyph { Back,Left,Right };
// 绘制和命中使用同一 DIP 边界；调用方负责焦点、按压配对与导航动作。
// Drawing and hit testing share DIP bounds; callers own focus, paired presses and navigation actions.
inline bool HitNavigationButton(D2D1_RECT_F bounds,float x,float y,bool enabled=true) {
    return enabled && x>=bounds.left && x<bounds.right && y>=bounds.top && y<bounds.bottom;
}
inline void DrawNavigationButton(const UiCanvas& canvas,const UiTheme& theme,D2D1_RECT_F bounds,
    NavigationGlyph glyph,bool enabled,bool hovered,bool pressed) {
    const float x=(bounds.left+bounds.right)*.5F,y=(bounds.top+bounds.bottom)*.5F;
    const bool back=glyph==NavigationGlyph::Back;
    hovered=enabled && hovered; pressed=enabled && pressed && (back || hovered);
    auto background=back?(pressed?theme.selected:hovered?theme.hover:theme.surface):theme.surface;
    background.a=enabled?1.0F:.35F;
    canvas.Round(bounds,8,background);
    auto color=hovered?theme.accent:back?theme.primaryText:theme.secondaryText;
    color.a=enabled?1.0F:.25F;canvas.brush.SetColor(color);
    if(back) {
        canvas.target.DrawLine(D2D1::Point2F(x+7,y),D2D1::Point2F(x-7,y),&canvas.brush,1.6F);
        canvas.target.DrawLine(D2D1::Point2F(x-1,y-6),D2D1::Point2F(x-7,y),&canvas.brush,1.6F);
        canvas.target.DrawLine(D2D1::Point2F(x-7,y),D2D1::Point2F(x-1,y+6),&canvas.brush,1.6F);
    } else {
        const int direction=glyph==NavigationGlyph::Left?-1:1;
        const float radius=pressed?3.0F:4.0F;
        const auto tip=D2D1::Point2F(x+direction*radius*.5F,y);
        canvas.target.DrawLine(D2D1::Point2F(x-direction*radius*.5F,y-radius),tip,&canvas.brush,1.6F);
        canvas.target.DrawLine(tip,D2D1::Point2F(x-direction*radius*.5F,y+radius),&canvas.brush,1.6F);
    }
}
}
