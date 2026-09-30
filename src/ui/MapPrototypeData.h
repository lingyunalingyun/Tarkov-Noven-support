#pragma once
#include <array>
#include <string_view>
#include <d2d1.h>
#include "ui/MapInteractionStrip.h"

namespace noven::ui::MapPrototype {
// 仅用于布局验收；不是游戏地图数据，未来由 MapCatalog 整体替换。
// Layout demo only, not game data; replace this entire namespace with MapCatalog later.
struct Floor { std::string_view id; std::wstring_view label; };
inline constexpr std::array Floors{Floor{"demo-1",L"1F"},Floor{"demo-b1",L"-1F"},
    Floor{"demo-b2",L"-2F"},Floor{"demo-b3",L"-3F"}};
inline constexpr D2D1_SIZE_F World{1000,700};
inline constexpr std::array Buildings{D2D1_RECT_F{140,130,340,280},D2D1_RECT_F{600,100,820,340},
    D2D1_RECT_F{350,440,640,590}};
struct DemoPoint {
    std::string_view id,floorId;
    MapMarkerType type;
    D2D1_POINT_2F coordinate;
    std::wstring_view chinese,english;
};
inline constexpr std::array Points{
    DemoPoint{"demo-task","demo-1",MapMarkerType::Task,{250,200},L"演示任务 · 1F",L"Demo task · 1F"},
    DemoPoint{"demo-extract","demo-b1",MapMarkerType::Extract,{700,250},L"演示出口 · -1F",L"Demo exit · -1F"},
    DemoPoint{"demo-point","demo-b3",MapMarkerType::Point,{500,520},L"演示地点 · -3F",L"Demo point · -3F"}};
}
