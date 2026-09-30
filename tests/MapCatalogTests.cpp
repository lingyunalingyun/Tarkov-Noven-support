#include "data/MapCatalog.h"
#include "data/InterchangeReference.h"
#include <windows.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <cstdlib>

namespace {
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
void Write(const std::filesystem::path& path,const std::string& value){
    std::ofstream file(path,std::ios::binary);file<<value;Require(file.good(),"fixture writes");
}
}
int main(int argc,char** argv){
    Require(argc==2,"assets directory required");std::wstring error;
    noven::data::MapCatalog catalog;
    Require(catalog.Load(std::filesystem::path(argv[1])/"data",error)&&error.empty(),"generated Interchange catalog loads");
    Require(catalog.Maps().size()==1&&catalog.Points().size()==1070,"snapshot counts match");
    const auto* map=catalog.Map("5714dbc024597771384a510d");
    Require(map&&map->nameZh=="立交桥"&&map->nameEn=="Interchange"&&map->cardinalRotation==180,"localized map identity loads");
    Require(!catalog.Map("missing")&&!catalog.Point("missing"),"unknown identities return null");
    int containers=0,extracts=0;
    for(const auto& point:catalog.Points()){
        Require(catalog.Point(point.id)==&point&&point.mapId==map->id,"stable identity resolves owned point");
        Require(std::isfinite(point.position.x)&&std::isfinite(point.position.y)&&std::isfinite(point.position.z),"raw coordinates finite");
        containers+=point.kind=="container";extracts+=point.kind=="extract";
    }
    Require(containers==795&&extracts==9,"deduplicated containers and faction extract records retained");
    namespace reference=noven::data::InterchangeReference;
    const auto top=reference::Project({598,20,-442}),bottom=reference::Project({-433,20,426});
    Require(top.x==0&&top.y==0&&bottom.x==reference::Width&&bottom.y==reference::Height,"dev rotated bounds project to SVG corners");
    const auto center=reference::Project({82.5,99,-8});
    Require(std::abs(center.x-reference::Width/2)<1e-9&&std::abs(center.y-reference::Height/2)<1e-9,"world center projects to image center independent of height");
    Require(reference::FloorFor({0,24.99,0})=="Ground_Level"&&reference::FloorFor({0,25,0})=="First_Floor"
        &&reference::FloorFor({0,34,0})=="Second_Floor","floor height boundaries are deterministic");
    Require(reference::FloorFor({121,40,0})=="Ground_Level"&&reference::FloorFor({0,40,219})=="Ground_Level","outdoor height is not a mall floor");
    Require(reference::Floors[0].id=="Second_Floor"&&reference::Floors[2].label==L"B1","real floors have stable top-down order");
    const auto dir=std::filesystem::temp_directory_path()/("NovenMapCatalogTests-"+std::to_string(GetCurrentProcessId()));
    Require(std::filesystem::create_directory(dir),"isolated fixture directory created");
    const std::string maps="id\tnormalizedName\tnameZh\tnameEn\trotation\traidDuration\tplayers\nmap1\tinterchange\t立交桥\tInterchange\t180\t40\t11-15\n";
    const std::string header="id\tmapId\tkind\tsubtype\tsourceId\tnameZh\tnameEn\tx\ty\tz\n";
    const std::string row="p1\tmap1\tcontainer\t\titem1\t铁\\t路\tRail\\nway\t-1.5\t27\t4\n";
    Write(dir/"map_maps.tsv",maps);Write(dir/"map_points.tsv",header+row);
    noven::data::MapCatalog fixture;
    Require(fixture.Load(dir,error)&&fixture.Point("p1")->nameZh=="铁\t路"&&fixture.Point("p1")->nameEn=="Rail\nway",
        "TSV escapes decode without altering coordinates");
    Require(fixture.Point("p1")->position.x==-1.5&&fixture.Point("p1")->position.y==27,"negative x and world height preserved");
    const auto rejects=[&](const std::string& bad){Write(dir/"map_points.tsv",header+bad);
        Require(!fixture.Load(dir,error)&&!error.empty()&&fixture.Points().size()==1&&fixture.Point("p1"),"bad reload rejects without losing valid state");};
    rejects(row+row);
    rejects("p1\tmissing\tcontainer\t\titem1\tName\tName\t1\t2\t3\n");
    rejects("p1\tmap1\tunknown\t\titem1\tName\tName\t1\t2\t3\n");
    rejects("p1\tmap1\tcontainer\t\titem1\tName\tName\tnan\t2\t3\n");
    rejects("p1\tmap1\tcontainer\t\titem1\tName\tName\t1\t2\n");
    rejects("p1\tmap1\tcontainer\t\titem1\tBad\\q\tName\t1\t2\t3\n");
    rejects(std::string("p1\tmap1\tcontainer\t\titem1\t")+"\xFF"+"\tName\t1\t2\t3\n");
    Write(dir/"map_maps.tsv",maps+"map1\tother\tName\tName\t180\t40\t11-15\n");
    Write(dir/"map_points.tsv",header+row);Require(!fixture.Load(dir,error),"duplicate maps reject");
    Write(dir/"map_maps.tsv",maps);Write(dir/"map_points.tsv",header);
    Require(!fixture.Load(dir,error),"empty points reject");
    std::filesystem::remove(dir/"map_points.tsv");Require(!fixture.Load(dir,error),"missing table rejects");
    std::filesystem::remove(dir/"map_maps.tsv");Require(std::filesystem::remove(dir),"fixture cleanup removes only owned empty directory");
    std::cout<<"Map catalog snapshot and validation checks passed\n";
}
