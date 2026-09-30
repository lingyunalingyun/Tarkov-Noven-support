#include "ui/FloorStack.h"
#include "ui/MapPrototypeData.h"
#include <cstdlib>
#include <iostream>

namespace {
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
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
    std::cout<<"Map prototype geometry passed\n";
}
