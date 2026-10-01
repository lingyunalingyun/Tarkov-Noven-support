#pragma once
#include "ui/MapMarkerIcon.h"
#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

namespace noven::ui {
enum class MapDetailIcon { Toolbox,Duffle,AmmoBox,GrenadeBox,WeaponBox,WoodCrate,Suitcase,DeadScav,
    GroundCache,BarrelCache,Pc,Jacket,Drawer,CashRegister,Medbag,Medcase,TechnicalCrate,RationCrate,
    MedicalCrate,Safe,Stash,PmcBody,CivilianBody,LabBody,Keycard,Key,Battlepass,Drink,Other,
    Electronics,Valuable,Injector,Flammable,Intel,Medical,Energy,Food,Building,Tools,Ammo,Weapon,Special,
    TaskItem,TaskObjective,Count };
struct MapDetailDescriptor {std::string_view id,key;MapPointCategory category;};
// 图标身份由生成数据引用；筛选、点位、信息条共享同一目录。
// Generated data references these icon identities; filters, points and details share this registry.
inline constexpr std::array<MapDetailDescriptor,static_cast<std::size_t>(MapDetailIcon::Count)> MapDetailIcons{{
    {"toolbox","map.icon.toolbox",MapPointCategory::Container},
    {"duffle","map.icon.duffle",MapPointCategory::Container},
    {"ammo_box","map.icon.ammo_box",MapPointCategory::Container},
    {"grenade_box","map.icon.grenade_box",MapPointCategory::Container},
    {"weapon_box","map.icon.weapon_box",MapPointCategory::Container},
    {"wood_crate","map.icon.wood_crate",MapPointCategory::Container},
    {"suitcase","map.icon.suitcase",MapPointCategory::Container},
    {"dead_scav","map.icon.dead_scav",MapPointCategory::Container},
    {"ground_cache","map.icon.ground_cache",MapPointCategory::Container},
    {"barrel_cache","map.icon.barrel_cache",MapPointCategory::Container},
    {"pc","map.icon.pc",MapPointCategory::Container},
    {"jacket","map.icon.jacket",MapPointCategory::Container},
    {"drawer","map.icon.drawer",MapPointCategory::Container},
    {"cash_register","map.icon.cash_register",MapPointCategory::Container},
    {"medbag","map.icon.medbag",MapPointCategory::Container},
    {"medcase","map.icon.medcase",MapPointCategory::Container},
    {"technical_crate","map.icon.technical_crate",MapPointCategory::Container},
    {"ration_crate","map.icon.ration_crate",MapPointCategory::Container},
    {"medical_crate","map.icon.medical_crate",MapPointCategory::Container},
    {"safe","map.icon.safe",MapPointCategory::Container},
    {"stash","map.icon.stash",MapPointCategory::Container},
    {"pmc_body","map.icon.pmc_body",MapPointCategory::Container},
    {"civilian_body","map.icon.civilian_body",MapPointCategory::Container},
    {"lab_body","map.icon.lab_body",MapPointCategory::Container},
    {"keycard","map.icon.keycard",MapPointCategory::LooseLoot},
    {"key","map.icon.key",MapPointCategory::LooseLoot},
    {"battlepass","map.icon.battlepass",MapPointCategory::LooseLoot},
    {"drink","map.icon.drink",MapPointCategory::LooseLoot},
    {"other","map.icon.other",MapPointCategory::LooseLoot},
    {"electronics","map.icon.electronics",MapPointCategory::LooseLoot},
    {"valuable","map.icon.valuable",MapPointCategory::LooseLoot},
    {"injector","map.icon.injector",MapPointCategory::LooseLoot},
    {"flammable","map.icon.flammable",MapPointCategory::LooseLoot},
    {"intel","map.icon.intel",MapPointCategory::LooseLoot},
    {"medical","map.icon.medical",MapPointCategory::LooseLoot},
    {"energy","map.icon.energy",MapPointCategory::LooseLoot},
    {"food","map.icon.food",MapPointCategory::LooseLoot},
    {"building","map.icon.building",MapPointCategory::LooseLoot},
    {"tools","map.icon.tools",MapPointCategory::LooseLoot},
    {"ammo","map.icon.ammo",MapPointCategory::LooseLoot},
    {"weapon","map.icon.weapon",MapPointCategory::LooseLoot},
    {"special","map.icon.special",MapPointCategory::LooseLoot},
    {"task_item","map.icon.task_item",MapPointCategory::Task},
    {"task_objective","map.icon.task_objective",MapPointCategory::Task},
}};
using MapIconMask=std::uint64_t;
static_assert(MapDetailIcons.size()<64);
inline MapIconMask MapIconBit(MapDetailIcon icon){return MapIconMask{1}<<static_cast<unsigned>(icon);}
inline MapIconMask MapIconFor(std::string_view id){
    for(std::size_t i=0;i<MapDetailIcons.size();++i)if(MapDetailIcons[i].id==id)return MapIconMask{1}<<i;
    return 0;
}
inline void DrawMapDetailIcon(const UiCanvas& canvas,const UiTheme& theme,MapPointCategory category,
    MapIconMask icons,D2D1_POINT_2F p,bool selected=false,float size=10){
    if(!icons){DrawMapMarkerIcon(canvas,theme,category,p,selected,size);return;}
    const auto icon=static_cast<MapDetailIcon>(std::countr_zero(icons));
    const float s=size/10;
    D2D1_COLOR_F color=theme.accent;
    if(category==MapPointCategory::Container)color=D2D1::ColorF(.62F,.73F,.79F);
    if(icon==MapDetailIcon::Medbag||icon==MapDetailIcon::Medcase||icon==MapDetailIcon::MedicalCrate||icon==MapDetailIcon::Medical)
        color=D2D1::ColorF(.95F,.43F,.37F);
    if(icon==MapDetailIcon::GroundCache||icon==MapDetailIcon::BarrelCache||icon==MapDetailIcon::Food||icon==MapDetailIcon::TaskItem)
        color=D2D1::ColorF(.48F,.76F,.48F);
    if(icon==MapDetailIcon::Pc||icon==MapDetailIcon::Electronics||icon==MapDetailIcon::Keycard||icon==MapDetailIcon::Jacket)
        color=D2D1::ColorF(.40F,.69F,.94F);
    canvas.Circle(p,size,theme.background);canvas.brush.SetColor(color);
    const auto line=[&](float x,float y,float a,float b){canvas.target.DrawLine({p.x+x*s,p.y+y*s},{p.x+a*s,p.y+b*s},&canvas.brush,1.5F*s);};
    const auto box=[&](float x,float y,float a,float b){canvas.target.DrawRectangle({p.x+x*s,p.y+y*s,p.x+a*s,p.y+b*s},&canvas.brush,1.4F*s);};
    const auto circle=[&](float x,float y,float r){canvas.target.DrawEllipse(D2D1::Ellipse({p.x+x*s,p.y+y*s},r*s,r*s),&canvas.brush,1.4F*s);};
    const auto cross=[&](){line(-3,0,3,0);line(0,-3,0,3);};
    const auto crate=[&](){box(-7,-5,7,5);line(-7,-2,7,-2);line(-4,-5,-4,5);line(4,-5,4,5);};
    switch(icon){
    case MapDetailIcon::Toolbox:box(-7,-3,7,5);box(-3,-6,3,-3);line(-7,0,7,0);line(0,-1,0,2);break;
    case MapDetailIcon::Duffle:canvas.target.DrawRoundedRectangle(D2D1::RoundedRect({p.x-7*s,p.y-3*s,p.x+7*s,p.y+5*s},3*s,3*s),&canvas.brush,s);box(-3,-6,3,-3);line(-4,-2,-4,4);line(4,-2,4,4);break;
    case MapDetailIcon::AmmoBox:crate();line(-2,1,2,1);line(0,0,0,4);break;
    case MapDetailIcon::GrenadeBox:box(-7,-5,7,5);circle(0,1,3);line(-1,-3,2,-3);break;
    case MapDetailIcon::WeaponBox:box(-8,-4,8,4);line(-5,0,5,0);line(-2,0,-2,2);line(3,-2,3,0);break;
    case MapDetailIcon::WoodCrate:crate();line(-4,-2,4,5);line(4,-2,-4,5);break;
    case MapDetailIcon::Suitcase:box(-5,-6,5,5);line(-2,-8,2,-8);line(-2,-8,-2,-6);line(2,-8,2,-6);line(-2,-5,-2,4);line(2,-5,2,4);circle(-3,7,1);circle(3,7,1);break;
    case MapDetailIcon::DeadScav:circle(0,-3,3);line(-5,2,5,2);line(-4,6,4,-1);line(-4,-1,4,6);break;
    case MapDetailIcon::GroundCache:box(-7,-4,7,4);line(-5,-2,5,2);line(-5,2,5,-2);line(-8,6,8,6);break;
    case MapDetailIcon::BarrelCache:box(-5,-6,5,6);line(-5,-3,5,-3);line(-5,3,5,3);line(-7,7,7,7);break;
    case MapDetailIcon::Pc:box(-5,-7,5,7);box(-3,-5,3,-2);circle(0,3,1);line(-3,6,3,6);break;
    case MapDetailIcon::Jacket:line(-3,-6,-7,-2);line(-7,-2,-5,2);line(-5,2,-3,0);box(-3,-5,3,6);line(3,-6,7,-2);line(7,-2,5,2);line(5,2,3,0);line(0,-4,0,6);break;
    case MapDetailIcon::Drawer:box(-5,-7,5,7);line(-5,-2,5,-2);line(-5,2,5,2);line(-1,-4,1,-4);line(-1,0,1,0);line(-1,5,1,5);break;
    case MapDetailIcon::CashRegister:box(-7,1,7,6);box(-4,-6,5,0);line(-2,-3,3,-3);line(-3,3,3,3);break;
    case MapDetailIcon::Medbag:box(-7,-3,7,5);box(-3,-6,3,-3);cross();break;
    case MapDetailIcon::Medcase:box(-6,-6,6,6);cross();line(-5,4,5,4);break;
    case MapDetailIcon::TechnicalCrate:crate();circle(0,1,2);line(-2,4,2,-1);break;
    case MapDetailIcon::RationCrate:crate();line(0,-1,0,4);line(-2,2,0,4);line(0,4,2,2);break;
    case MapDetailIcon::MedicalCrate:crate();cross();break;
    case MapDetailIcon::Safe:box(-6,-7,6,7);box(-4,-5,4,5);circle(1,0,2);line(-3,-3,-3,3);break;
    case MapDetailIcon::Stash:crate();circle(0,1,2);line(0,3,0,5);break;
    case MapDetailIcon::PmcBody:case MapDetailIcon::CivilianBody:case MapDetailIcon::LabBody:
        circle(0,-5,2);line(0,-3,0,3);line(-5,0,5,0);line(0,3,-4,7);line(0,3,4,7);
        if(icon==MapDetailIcon::PmcBody)line(-3,-7,3,-7);
        if(icon==MapDetailIcon::LabBody)box(-3,-2,3,3);
        break;
    case MapDetailIcon::Keycard:box(-7,-5,7,5);box(-4,-2,-1,1);line(2,-2,5,-2);line(2,1,5,1);break;
    case MapDetailIcon::Key:circle(-3,-3,3);line(-1,-1,6,6);line(3,3,5,1);line(5,5,7,3);break;
    case MapDetailIcon::Battlepass:box(-5,-7,5,7);line(-3,-4,3,-4);line(-3,-1,3,-1);circle(0,4,2);break;
    case MapDetailIcon::Drink:box(-3,-4,3,7);box(-2,-7,2,-4);line(-3,2,3,2);break;
    case MapDetailIcon::Other:line(-6,0,0,-6);line(0,-6,6,0);line(6,0,0,6);line(0,6,-6,0);circle(0,0,1);break;
    case MapDetailIcon::Electronics:box(-4,-4,4,4);box(-2,-2,2,2);for(float a=-3;a<=3;a+=3){line(a,-7,a,-4);line(a,4,a,7);line(-7,a,-4,a);line(4,a,7,a);}break;
    case MapDetailIcon::Valuable:line(-6,-2,-3,-5);line(-3,-5,3,-5);line(3,-5,6,-2);line(6,-2,0,6);line(0,6,-6,-2);line(-6,-2,6,-2);line(-3,-5,0,6);line(3,-5,0,6);break;
    case MapDetailIcon::Injector:line(-5,6,4,-3);line(-3,7,6,-2);line(4,-3,6,-2);line(-5,6,-3,7);line(5,-2,7,-5);line(5,-7,8,-4);line(-5,7,-7,9);break;
    case MapDetailIcon::Flammable:line(0,-8,-5,1);line(-5,1,-3,6);line(-3,6,3,6);line(3,6,5,1);line(5,1,2,-3);line(2,-3,0,0);line(0,0,0,-8);break;
    case MapDetailIcon::Intel:box(-5,-7,5,7);line(-3,-4,3,-4);line(-3,0,3,0);line(-3,3,1,3);break;
    case MapDetailIcon::Medical:box(-5,-6,5,6);cross();break;
    case MapDetailIcon::Energy:box(-4,-6,4,6);line(-2,-8,2,-8);line(1,-4,-2,0);line(-2,0,2,0);line(2,0,-1,4);break;
    case MapDetailIcon::Food:circle(0,0,6);line(-3,-7,-3,7);line(3,-7,3,7);line(-5,-7,-5,-3);line(-1,-7,-1,-3);break;
    case MapDetailIcon::Building:box(-7,-4,7,4);line(-7,0,7,0);line(0,-4,0,0);line(-4,0,-4,4);line(4,0,4,4);break;
    case MapDetailIcon::Tools:line(-5,6,3,-2);line(-3,7,5,-1);line(3,-2,1,-6);line(1,-6,4,-4);line(4,-4,7,-7);line(7,-7,7,-3);line(7,-3,5,-1);line(-5,6,-3,7);break;
    case MapDetailIcon::Ammo:box(-2,-3,2,6);line(-2,-3,0,-7);line(0,-7,2,-3);line(-3,6,3,6);break;
    case MapDetailIcon::Weapon:line(-8,-1,7,-1);line(-6,-1,-8,3);line(-1,-1,-1,4);line(2,-1,2,2);line(5,-4,5,-1);break;
    case MapDetailIcon::Special:circle(0,0,6);line(-4,0,0,-4);line(0,-4,4,0);line(4,0,0,4);line(0,4,-4,0);break;
    case MapDetailIcon::TaskItem:box(-5,-5,5,5);box(-7,-2,-5,2);box(-2,-7,2,-5);line(0,-2,0,2);line(-2,0,2,0);break;
    case MapDetailIcon::TaskObjective:circle(0,0,7);circle(0,0,3);line(0,-9,0,-5);line(5,0,9,0);break;
    case MapDetailIcon::Count:break;
    }
    if(selected)canvas.target.DrawEllipse(D2D1::Ellipse(p,size+5*s,size+5*s),&canvas.brush,1.5F*s);
}
}
