#pragma once

#include "ui/MapPointCategory.h"
#include "ui/Theme.h"
#include "ui/UiCanvas.h"

namespace noven::ui {

// 所有地图表面共用这一组原生矢量图标，避免同一分类在不同区域出现不同符号。
// Every map surface shares these native vector icons so one category never changes symbols.
inline void DrawMapMarkerIcon(const UiCanvas& canvas,const UiTheme& theme,MapPointCategory category,
    D2D1_POINT_2F p,bool selected=false,float size=10.0F){
    const float s=size/10.0F;
    const auto line=[&](float x1,float y1,float x2,float y2,float width=1.6F){
        canvas.target.DrawLine({p.x+x1*s,p.y+y1*s},{p.x+x2*s,p.y+y2*s},&canvas.brush,width*s);
    };
    canvas.Circle(p,size,theme.background);canvas.brush.SetColor(theme.accent);
    switch(category){
    case MapPointCategory::Container:
        canvas.target.DrawRectangle({p.x-5*s,p.y-3*s,p.x+5*s,p.y+5*s},&canvas.brush,1.5F*s);
        line(-6,-4,6,-4);line(-3,-1,3,-1,1);
        break;
    case MapPointCategory::LooseLoot:
        line(0,-6,4,0);line(4,0,0,6);line(0,6,-4,0);line(-4,0,0,-6);
        line(5,-5,5,-1,1);line(3,-3,7,-3,1);
        break;
    case MapPointCategory::Lock:
        canvas.target.DrawEllipse(D2D1::Ellipse({p.x,p.y-3*s},4*s,4*s),&canvas.brush,1.5F*s);
        canvas.Fill({p.x-5*s,p.y-2*s,p.x+5*s,p.y+6*s},theme.accent);
        canvas.Circle({p.x,p.y+1*s},1.3F*s,theme.background);canvas.brush.SetColor(theme.accent);line(0,2,0,4,1.2F);
        break;
    case MapPointCategory::Switch:
        canvas.target.DrawRoundedRectangle(D2D1::RoundedRect({p.x-7*s,p.y-4*s,p.x+7*s,p.y+4*s},4*s,4*s),&canvas.brush,1.5F*s);
        canvas.Circle({p.x-3*s,p.y},2.5F*s,theme.accent);line(2,-2,5,-2,1);line(2,0,5,0,1);line(2,2,5,2,1);
        break;
    case MapPointCategory::StationaryWeapon:
        line(-7,-2,4,-5,2);line(4,-5,7,-5,2);line(-1,-3,0,2);line(0,2,-5,6);line(0,2,5,6);line(-4,-1,-2,2,2);
        break;
    case MapPointCategory::Mine:
        canvas.target.DrawEllipse(D2D1::Ellipse(p,4*s,4*s),&canvas.brush,1.5F*s);
        line(0,-7,0,-4);line(0,4,0,7);line(-7,0,-4,0);line(4,0,7,0);line(-2,-2,2,2);line(2,-2,-2,2);
        break;
    case MapPointCategory::Artillery:
        line(0,6,0,-5,2);line(-5,5,-2,-3,1.5F);line(5,5,2,-3,1.5F);
        line(-5,-5,-2,-7);line(0,-5,0,-8);line(5,-5,2,-7);
        break;
    case MapPointCategory::Boss:
        line(-6,4,-5,-5);line(-5,-5,-1,-1);line(-1,-1,0,-6);line(0,-6,3,-1);line(3,-1,6,-5);line(6,-5,5,4);line(5,4,-6,4);
        break;
    case MapPointCategory::Task:
        canvas.target.DrawEllipse(D2D1::Ellipse(p,6*s,6*s),&canvas.brush,1.2F*s);line(-4,0,4,0,2);line(0,-4,0,4,2);
        break;
    case MapPointCategory::PmcExtract:
        canvas.target.DrawEllipse(D2D1::Ellipse(p,6*s,6*s),&canvas.brush,1.2F*s);line(-3,3,4,-4,2);line(0,-4,4,-4,2);line(4,-4,4,0,2);
        break;
    case MapPointCategory::ScavExtract:
        canvas.target.DrawEllipse(D2D1::Ellipse(p,6*s,6*s),&canvas.brush,1.2F*s);line(3,-3,-4,4,2);line(0,4,-4,4,2);line(-4,4,-4,0,2);
        break;
    case MapPointCategory::CoopExtract:
        canvas.target.DrawEllipse(D2D1::Ellipse({p.x-3*s,p.y},3.5F*s,4.5F*s),&canvas.brush,1.4F*s);
        canvas.target.DrawEllipse(D2D1::Ellipse({p.x+3*s,p.y},3.5F*s,4.5F*s),&canvas.brush,1.4F*s);line(-1,0,1,0,2);
        break;
    case MapPointCategory::Transit:
        line(-7,-3,6,-3,1.7F);line(3,-6,6,-3,1.7F);line(3,0,6,-3,1.7F);
        line(7,3,-6,3,1.7F);line(-3,0,-6,3,1.7F);line(-3,6,-6,3,1.7F);
        break;
    case MapPointCategory::HiddenExtract:
        canvas.target.DrawEllipse(D2D1::Ellipse(p,7*s,4.5F*s),&canvas.brush,1.3F*s);canvas.Circle(p,2*s,theme.accent);line(-7,7,7,-7,2);
        break;
    case MapPointCategory::Sniper:
        canvas.target.DrawEllipse(D2D1::Ellipse(p,5*s,5*s),&canvas.brush,1.4F*s);
        line(-8,0,-3,0);line(3,0,8,0);line(0,-8,0,-3);line(0,3,0,8);canvas.Circle(p,1.5F*s,theme.accent);
        break;
    case MapPointCategory::Spawn:
        line(-6,5,0,-6,2);line(0,-6,6,5,2);line(6,5,-6,5,2);line(0,-2,0,4,1.2F);
        break;
    case MapPointCategory::ScavSpawn:
        line(-6,-5,0,6,2);line(0,6,6,-5,2);line(6,-5,-6,-5,2);line(0,2,0,-4,1.2F);
        break;
    case MapPointCategory::Btr:
        canvas.target.DrawRectangle({p.x-7*s,p.y-3*s,p.x+7*s,p.y+4*s},&canvas.brush,1.5F*s);
        canvas.target.DrawRectangle({p.x-2*s,p.y-6*s,p.x+4*s,p.y-3*s},&canvas.brush,1.5F*s);line(4,-5,8,-5,1.5F);
        canvas.target.DrawEllipse(D2D1::Ellipse({p.x-4*s,p.y+5*s},2*s,2*s),&canvas.brush,1.5F*s);
        canvas.target.DrawEllipse(D2D1::Ellipse({p.x+4*s,p.y+5*s},2*s,2*s),&canvas.brush,1.5F*s);
        break;
    case MapPointCategory::EasterEgg:
        canvas.target.DrawEllipse(D2D1::Ellipse(p,5*s,7*s),&canvas.brush,1.5F*s);line(-4,-1,-1,1);line(-1,1,2,-1);line(2,-1,4,1,1.3F);
        break;
    case MapPointCategory::Count:
        break;
    }
    if(selected)canvas.target.DrawEllipse(D2D1::Ellipse(p,size+5*s,size+5*s),&canvas.brush,1.5F*s);
}

} // namespace noven::ui
