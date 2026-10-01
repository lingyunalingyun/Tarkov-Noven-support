#pragma once
#include "ui/MapMarkerIcon.h"
#include "ui/MapInteractionStrip.h"
#include "ui/Scrollbar.h"
#include <array>
#include <set>
#include <string>

namespace noven::ui {
enum class MapFilterPanel { Points, Layers, Tasks };
// 分类开关与任务身份独立；任务被隐藏后不能因分类重新打开而出现。
// Category switches and task identities are independent; enabling a category does not unhide a task.
struct MapFilters final {
    std::array<bool,MapCategoryCount> categories=[] {std::array<bool,MapCategoryCount> a{};a.fill(true);return a;}();
    bool grid{true},geometry{true};
    std::set<std::string,std::less<>> hiddenTasks;
    MapIconMask hiddenIcons{};
    // 多种可能物资共享一个点位；只在全部所属类型隐藏时隐藏点位。
    // Possible loot types share one point; hide it only when all its types are hidden.
    bool AllowsIcons(MapIconMask icons) const {return !icons||(icons&~hiddenIcons)!=0;}
    bool Allows(MapPointCategory category,std::string_view id) const {
        return categories[static_cast<std::size_t>(category)]
            &&(category!=MapPointCategory::Task||!hiddenTasks.contains(id));
    }
    void ToggleTask(std::string_view id){
        if(hiddenTasks.contains(id))hiddenTasks.erase(std::string(id));else hiddenTasks.emplace(id);
    }
};
// 侧展列表覆盖画布，不改变地图视口；裁剪、行命中和滚条共用 DIP 布局。
// Flyouts overlay the canvas without resizing it; clipping, row hits and scrollbar share DIP geometry.
struct MapFilterList final {
    D2D1_RECT_F bounds;
    float scroll{};
    D2D1_RECT_F Body() const {return {bounds.left+8,bounds.top+38,bounds.right-8,bounds.bottom-8};}
    D2D1_RECT_F Row(std::size_t index) const {
        const auto r=Body();const float y=r.top+static_cast<float>(index)*30-scroll;
        return {r.left,y,r.right-14,y+28};
    }
    std::optional<std::size_t> Hit(D2D1_POINT_2F p,std::size_t count) const {
        if(!MapContains(Body(),p))return std::nullopt;
        for(std::size_t i=0;i<count;++i)if(MapContains(Row(i),p))return i;
        return std::nullopt;
    }
    std::optional<ScrollbarGeometry> Bar(std::size_t count) const {
        const auto r=Body();return MakeScrollbar({r.right-12,r.top,r.right,r.bottom},static_cast<float>(count)*30,scroll);
    }
};
inline void DrawMapCheck(const UiCanvas& canvas,const UiTheme& theme,D2D1_RECT_F row,
    std::wstring_view label,bool checked,std::optional<MapPointCategory> category=std::nullopt,MapIconMask icons=0,
    const MapIconImages* images=nullptr){
    canvas.Round(row,4,checked?theme.selected:theme.background);
    const float x=row.left+12,y=(row.top+row.bottom)*.5F;
    canvas.brush.SetColor(checked?theme.accent:theme.secondaryText);
    canvas.target.DrawRectangle({x-5,y-5,x+5,y+5},&canvas.brush,1);
    if(checked){canvas.target.DrawLine({x-3,y},{x-1,y+3},&canvas.brush,1.5F);
        canvas.target.DrawLine({x-1,y+3},{x+4,y-3},&canvas.brush,1.5F);}
    if(category){
        if(images)images->Draw(canvas,theme,*category,icons,{row.left+34,y},false,9);
        else DrawMapDetailIcon(canvas,theme,*category,icons,{row.left+34,y},false,7);
    }
    const auto wrapping=canvas.smallFormat.GetWordWrapping();canvas.smallFormat.SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    canvas.Text(label,canvas.smallFormat,{row.left+(category?46.0F:24.0F),row.top,row.right-4,row.bottom},
        checked?theme.primaryText:theme.secondaryText);
    canvas.smallFormat.SetWordWrapping(wrapping);
}
}
