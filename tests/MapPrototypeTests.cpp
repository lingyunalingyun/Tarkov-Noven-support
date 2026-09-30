#include "ui/FloorStack.h"
#include "ui/MapPrototypeData.h"
#include "ui/MapLayout.h"
#include "ui/MapViewport.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<.002F;}
}
int main(){
    using namespace noven::ui;
    FloorStack stack{{150,100},200,4};
    Require(MapPrototype::Floors.front().label==L"1F"&&MapPrototype::Floors.back().label==L"-3F",
        "demo floor order is deterministic");
    for(std::size_t i=0;i<4;++i){
        const auto plate=stack.Plate(i);
        const auto point=D2D1::Point2F(plate.anchor.x,plate.anchor.y-2);
        Require(stack.Hit(point)==i,"each exposed floor can be selected");
        Require(plate.Contains(point),"selected geometry contains hit point");
    }
    Require(!stack.Hit({0,0}),"outside stack does not select floor");
    const auto overview=MapLayout::Sample(1400,800,{},0),selected=MapLayout::Sample(1400,800,{},1);
    Require(overview.stack.width>selected.stack.width,"selection shrinks stack");
    Require(overview.stack.origin.x>selected.stack.origin.x,"selection moves stack left");
    Require(MapLayout::Ease(0)==0&&MapLayout::Ease(1)==1,"transition endpoints exact");
    for(float width:{850.0F,1100.0F,1600.0F}){
        const auto layout=MapLayout::Sample(width,700,{},1);
        Require(layout.viewport.right-layout.viewport.left>250,"responsive map keeps useful width");
        Require(layout.stack.Plate(0).vertices[1].x<layout.viewport.left,"stack stays left of map");
    }
    MapViewport view(MapPrototype::World);view.SetBounds({400,250,1200,750});
    const D2D1_POINT_2F map{420,310};const auto screen=view.ToScreen(map),roundTrip=view.ToMap(screen);
    Require(Near(map.x,roundTrip.x)&&Near(map.y,roundTrip.y),"map/screen round trip");
    const D2D1_POINT_2F cursor{790,480};const auto anchor=view.ToMap(cursor);view.ZoomAt(cursor,2);
    const auto after=view.ToMap(cursor);
    Require(Near(anchor.x,after.x)&&Near(anchor.y,after.y),"cursor zoom preserves map anchor");
    view.ZoomAt(cursor,1000);Require(Near(view.Scale(),view.MaximumScale()),"maximum scale clamps");
    view.ZoomAt(cursor,-1000);Require(Near(view.Scale(),view.MinimumScale()),"minimum scale clamps");
    view.Fit();const auto center=view.ToScreen({500,350});
    Require(Near(center.x,800)&&Near(center.y,500),"fit centers map");
    view.Pan({100000,100000});const auto corner=view.ToScreen({0,0});
    Require(corner.x<1200&&corner.y<750,"positive pan cannot lose map");
    view.Pan({-100000,-100000});const auto farCorner=view.ToScreen({1000,700});
    Require(farCorner.x>400&&farCorner.y>250,"negative pan cannot lose map");
    view.Fit();view.Focus({400,300});const auto focused=view.ToScreen({400,300});
    Require(Near(focused.x,800)&&Near(focused.y,500),"focus centers requested coordinate");
    const auto oldScale=view.Scale();view.SetBounds({400,250,1200,750});
    Require(view.Scale()==oldScale,"unchanged bounds preserve view");
    const auto& demo=MapPrototype::Points[1];
    const std::array points{MapInteractionPoint{demo.id,demo.floorId,demo.type,demo.coordinate,demo.english}};
    MapInteractionStrip strip{{400,140,1000,235}};
    Require(strip.Hit({450,190},points)==demo.id,"strip hit returns stable marker ID");
    Require(!strip.Hit({0,0},points),"outside strip has no marker identity");
    std::cout<<"Map prototype geometry and transform checks passed\n";
}
