#pragma once
#include "ui/Dropdown.h"
#include "ui/MapLayout.h"
#include <optional>

namespace noven::ui {
// 有界地图列表，绘制、滚动与命中使用同一采样；不拥有地图/模式数据。
// Bounded map list shares geometry for drawing, scrolling and hits without owning map/mode data.
struct MapPicker final {
    D2D1_RECT_F header,panel;
    float scroll{};
    static MapPicker Sample(const MapLayout& layout,float scroll,std::size_t count){
        const auto left=layout.search.left,right=layout.search.right;
        const auto bottom=std::min(layout.content.bottom-8,182.0F+static_cast<float>(count)*32+8);
        return {{left,130,right-150,166},{left,174,std::min(right-150,left+420),std::max(182.0F,bottom)},scroll};
    }
    D2D1_RECT_F Row(std::size_t i) const noexcept {
        const auto y=panel.top+4+static_cast<float>(i)*32-scroll;return {panel.left+4,y,panel.right-4,y+32};
    }
    float Maximum(std::size_t count) const noexcept{return std::max(0.0F,static_cast<float>(count)*32-(panel.bottom-panel.top-8));}
    std::optional<std::size_t> Hit(float x,float y,std::size_t count) const noexcept {
        if(!MapContains(panel,{x,y}))return {};
        const float row=(y-panel.top-4+scroll)/32;
        if(row<0||row>=static_cast<float>(count))return {};
        return static_cast<std::size_t>(row);
    }
};
}
