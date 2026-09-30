#pragma once
#include "data/MapCatalog.h"
#include <array>

namespace noven::data::InterchangeReference {
// 仅适配已核对的 dev 立交桥配置；生成器遇到配置变化会拒绝生成。
// Adapter for the verified dev Interchange configuration only; generation rejects contract changes.
inline constexpr std::string_view MapId="5714dbc024597771384a510d";
inline constexpr double Width=1127.6852,Height=947.02582;
struct Floor {std::string_view id;std::wstring_view label;};
inline constexpr std::array Floors{Floor{"Second_Floor",L"2F"},Floor{"First_Floor",L"1F"},Floor{"Ground_Level",L"B1"}};
struct ImagePosition {double x,y;};
inline ImagePosition Project(MapWorldPosition world) noexcept {
    // dev SVG 覆盖旋转后的世界 bounds；高度不参与平面投影。
    // dev SVG overlays rotated world bounds; height does not enter the planar projection.
    return {(598-world.x)/1031*Width,(world.z+442)/868*Height};
}
inline std::string_view FloorFor(MapWorldPosition world) noexcept {
    // 商场范围外保持地面层，不能仅按高度把室外点分配到商场楼层。
    // Outside the mall retain ground, never assign outdoor points to mall floors by height alone.
    if(world.x>=-222&&world.x<=120&&world.z>=-327&&world.z<=218){
        if(world.y>=34&&world.y<1000)return "Second_Floor";
        if(world.y>=25&&world.y<34)return "First_Floor";
    }
    return "Ground_Level";
}
}
