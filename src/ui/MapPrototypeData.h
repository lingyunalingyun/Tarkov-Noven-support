#pragma once
#include <array>
#include <string_view>
#include <d2d1.h>

namespace noven::ui::MapPrototype {
// 仅用于布局验收；不是游戏地图数据，未来由 MapCatalog 整体替换。
// Layout demo only, not game data; replace this entire namespace with MapCatalog later.
struct Floor { std::string_view id; std::wstring_view label; };
inline constexpr std::array Floors{Floor{"demo-1",L"1F"},Floor{"demo-b1",L"-1F"},
    Floor{"demo-b2",L"-2F"},Floor{"demo-b3",L"-3F"}};
inline constexpr D2D1_SIZE_F World{1000,700};
inline constexpr std::array Buildings{D2D1_RECT_F{140,130,340,280},D2D1_RECT_F{600,100,820,340},
    D2D1_RECT_F{350,440,640,590}};
}
