#pragma once
#include <array>
#include <string_view>
#include <d2d1.h>
#include "ui/MapInteractionStrip.h"
#include "ui/MapFilters.h"

namespace noven::ui::MapPrototype {
// 仅用于布局验收；不是游戏地图数据，未来由 MapCatalog 整体替换。
// Layout demo only, not game data; replace this entire namespace with MapCatalog later.
struct Floor { std::string_view id; std::wstring_view label; };
struct Map { std::string_view id; std::wstring_view chinese,english; };
inline constexpr std::array Maps{Map{"demo-a",L"演示地图 A",L"Demo map A"},
    Map{"demo-b",L"演示地图 B",L"Demo map B"},Map{"demo-c",L"演示地图 C",L"Demo map C"}};
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
    std::string_view mapId{"demo-a"};
    MapPointCategory category{MapPointCategory::EasterEgg};
};
inline constexpr std::array Points{
    DemoPoint{"demo-task","demo-1",MapMarkerType::Task,{250,200},L"演示任务 · 1F",L"Demo task · 1F","demo-a",MapPointCategory::Task},
    DemoPoint{"demo-extract","demo-b1",MapMarkerType::Extract,{700,250},L"演示出口 · -1F",L"Demo exit · -1F","demo-a",MapPointCategory::PmcExtract},
    DemoPoint{"demo-point","demo-b3",MapMarkerType::Point,{500,520},L"演示地点 · -3F",L"Demo point · -3F"},
    DemoPoint{"demo-container","demo-b2",MapMarkerType::Point,{140,120},L"演示容器",L"Demo container","demo-a",MapPointCategory::Container},
    DemoPoint{"demo-mine","demo-b2",MapMarkerType::Point,{300,140},L"演示地雷",L"Demo mine","demo-a",MapPointCategory::Mine},
    DemoPoint{"demo-boss","demo-b2",MapMarkerType::Point,{460,120},L"演示 BOSS",L"Demo boss","demo-a",MapPointCategory::Boss},
    DemoPoint{"demo-scav-extract","demo-b2",MapMarkerType::Extract,{620,140},L"演示 Scav 撤离",L"Demo Scav extract","demo-a",MapPointCategory::ScavExtract},
    DemoPoint{"demo-coop","demo-b2",MapMarkerType::Extract,{820,120},L"演示合作撤离",L"Demo co-op extract","demo-a",MapPointCategory::CoopExtract},
    DemoPoint{"demo-transit","demo-b2",MapMarkerType::Extract,{160,340},L"演示转移点",L"Demo transit","demo-a",MapPointCategory::Transit},
    DemoPoint{"demo-hidden","demo-b2",MapMarkerType::Extract,{360,320},L"演示隐藏撤离",L"Demo hidden extract","demo-a",MapPointCategory::HiddenExtract},
    DemoPoint{"demo-sniper","demo-b2",MapMarkerType::Point,{540,340},L"演示狙击兵",L"Demo sniper","demo-a",MapPointCategory::Sniper},
    DemoPoint{"demo-spawn","demo-b2",MapMarkerType::Point,{760,320},L"演示出生点",L"Demo spawn","demo-a",MapPointCategory::Spawn},
    DemoPoint{"demo-scav-spawn","demo-b2",MapMarkerType::Point,{220,530},L"演示 Scav 刷新",L"Demo Scav spawn","demo-a",MapPointCategory::ScavSpawn},
    DemoPoint{"demo-btr","demo-b2",MapMarkerType::Point,{440,550},L"演示 BTR 停车点",L"Demo BTR stop","demo-a",MapPointCategory::Btr},
    DemoPoint{"demo-task-two","demo-b3",MapMarkerType::Task,{750,500},L"演示任务二 · -3F",L"Demo task two · -3F","demo-a",MapPointCategory::Task},
    DemoPoint{"demo-b-task","demo-1",MapMarkerType::Task,{400,300},L"演示地图 B 任务",L"Demo map B task","demo-b",MapPointCategory::Task},
    DemoPoint{"demo-c-point","demo-b3",MapMarkerType::Point,{650,400},L"演示地图 C 地点",L"Demo map C point","demo-c",MapPointCategory::EasterEgg}};
}
