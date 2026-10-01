#pragma once
#include "ui/UiCanvas.h"
#include "ui/Theme.h"
#include "ui/MapMarkerIcon.h"
#include <span>
#include <string_view>
#include <optional>

namespace noven::ui {
enum class MapMarkerType { Task, Extract, Point };
struct MapInteractionPoint final {
    std::string_view id,floorId;
    MapMarkerType type;
    D2D1_POINT_2F coordinate;
    std::wstring_view title;
    MapPointCategory category{MapPointCategory::EasterEgg};
};
inline bool MapContains(D2D1_RECT_F r,D2D1_POINT_2F p) noexcept {
    return p.x>=r.left&&p.x<r.right&&p.y>=r.top&&p.y<r.bottom;
}
// 条目与地图标识共享稳定 ID；组件不保存独立的索引选择状态。
// Strip entries share marker IDs; the component retains no independent index selection.
struct MapInteractionStrip final {
    D2D1_RECT_F bounds{};
    D2D1_RECT_F Entry(std::size_t index,std::size_t count) const noexcept {
        const bool narrow=bounds.right-bounds.left<440;
        const float top=bounds.top+34;
        if(narrow){const float h=(bounds.bottom-top)/static_cast<float>(count);
            return {bounds.left+8,top+static_cast<float>(index)*h,bounds.right-8,top+static_cast<float>(index+1)*h-3};}
        const float w=(bounds.right-bounds.left-16)/static_cast<float>(count);
        return {bounds.left+8+static_cast<float>(index)*w,top,bounds.left+8+static_cast<float>(index+1)*w-4,bounds.bottom-8};
    }
    std::optional<std::string_view> Hit(D2D1_POINT_2F p,std::span<const MapInteractionPoint> points) const {
        for(std::size_t i=0;i<points.size();++i)if(MapContains(Entry(i,points.size()),p))return points[i].id;
        return std::nullopt;
    }
    void Draw(const UiCanvas& canvas,const UiTheme& theme,std::wstring_view heading,
        std::span<const MapInteractionPoint> points,std::string_view selected) const {
        canvas.Round(bounds,theme.cornerRadius,theme.surface);
        canvas.Text(heading,canvas.smallFormat,{bounds.left+14,bounds.top+8,bounds.right-12,bounds.top+32},theme.secondaryText);
        for(std::size_t i=0;i<points.size();++i){const auto r=Entry(i,points.size());
            canvas.Round(r,5,points[i].id==selected?theme.selected:theme.background);
            DrawMapMarkerIcon(canvas,theme,points[i].category,{r.left+18,(r.top+r.bottom)*.5F});
            const auto wrapping=canvas.smallFormat.GetWordWrapping();
            canvas.smallFormat.SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            canvas.Text(points[i].title,canvas.smallFormat,{r.left+36,r.top+3,r.right-5,r.bottom},theme.primaryText);
            canvas.smallFormat.SetWordWrapping(wrapping);
        }
    }
};
}
