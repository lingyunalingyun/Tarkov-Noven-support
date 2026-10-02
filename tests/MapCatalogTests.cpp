#include "data/MapCatalog.h"
#include "data/TaskMapLinks.h"
#include "data/InterchangeReference.h"
#include "data/MapReference.h"
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
    Require(catalog.Load(std::filesystem::path(argv[1])/"data",error)&&error.empty(),"generated full map catalog loads");
    Require(catalog.Maps().size()==17&&catalog.Points().size()==16755,"all-map snapshot counts match");
    const auto* map=catalog.Map("5714dbc024597771384a510d");
    Require(map&&map->nameZh=="立交桥"&&map->nameEn=="Interchange"&&map->cardinalRotation==180,"localized map identity loads");
    Require(!catalog.Map("missing")&&!catalog.Point("missing"),"unknown identities return null");
    int containers=0,extracts=0,loose=0,locks=0,switches=0,stationary=0,tasks=0;
    for(const auto& point:catalog.Points()){
        Require(catalog.Point(point.id)==&point&&catalog.Map(point.mapId),"stable identity resolves owned point and map");
        Require(std::isfinite(point.position.x)&&std::isfinite(point.position.y)&&std::isfinite(point.position.z),"raw coordinates finite");
        if(point.mapId!=map->id)continue;
        containers+=point.kind=="container";extracts+=point.kind=="extract";loose+=point.kind=="loose";
        locks+=point.kind=="lock";switches+=point.kind=="switch";stationary+=point.kind=="stationary";
        tasks+=point.kind=="task";
    }
    Require(containers==795&&extracts==9,"deduplicated containers and faction extract records retained");
    Require(loose==483&&locks==28&&switches==6&&stationary==2&&tasks==45,
        "all positioned Interchange API groups are retained");
    noven::data::TaskMapLinks links;links.Bind(catalog);
    const std::string pathfinder="5ae449c386f7744bde357697";
    std::string previousPoint;
    for(const auto objective:{"5bb60cbc88a45011a8235cc5","6a60968c58aab7961885e537","6a6096d81284478fd859003a"}){
        const auto targets=links.Targets(pathfinder,pathfinder+"_"+objective);
        Require(targets.size()==1&&catalog.Point(targets.front())->kind=="task"&&targets.front()!=previousPoint,"Pathfinder objectives resolve three distinct exact DEV positions");
        previousPoint=targets.front();
    }
    Require(links.Targets("wrong",pathfinder+"_5bb60cbc88a45011a8235cc5").empty()
        &&links.Targets(pathfinder,pathfinder+"_missing").empty(),"task links never use name or prefix approximations");
    namespace reference=noven::data::InterchangeReference;
    const auto top=reference::Project({598,20,-442}),bottom=reference::Project({-433,20,426});
    Require(top.x==0&&top.y==0&&bottom.x==reference::Width&&bottom.y==reference::Height,"dev rotated bounds project to SVG corners");
    const auto center=reference::Project({82.5,99,-8});
    Require(std::abs(center.x-reference::Width/2)<1e-9&&std::abs(center.y-reference::Height/2)<1e-9,"world center projects to image center independent of height");
    Require(reference::FloorFor({0,24.99,0})=="Ground_Level"&&reference::FloorFor({0,25,0})=="First_Floor"
        &&reference::FloorFor({0,34,0})=="Second_Floor","floor height boundaries are deterministic");
    Require(reference::FloorFor({121,40,0})=="Ground_Level"&&reference::FloorFor({0,40,219})=="Ground_Level","outdoor height is not a mall floor");
    Require(reference::Floors[0].id=="Second_Floor"&&reference::Floors[2].label==L"B1","real floors have stable top-down order");
    const noven::data::MapProjection generic{reference::Width,reference::Height,180,-433,598,-442,426};
    for(const auto position:std::array<noven::data::MapWorldPosition,3>{{{598,20,-442},{-433,34,426},{82.5,99,-8}}}){
        const auto expected=reference::Project(position);const auto actual=generic.Project(position);
        Require(std::abs(actual[0]-expected.x)<1e-9&&std::abs(actual[1]-expected.y)<1e-9,"generic rotation agrees with verified Interchange projection");
    }
    const noven::data::MapProjection quarterTurn{200,100,90,-10,10,-20,20};
    const auto quarterOrigin=quarterTurn.Project({-10,300,20}),quarterFar=quarterTurn.Project({10,300,-20});
    Require(std::abs(quarterOrigin[0])<1e-9&&std::abs(quarterOrigin[1]-100)<1e-9
        &&std::abs(quarterFar[0]-200)<1e-9&&std::abs(quarterFar[1])<1e-9,"quarter-turn factory-style map uses rotated bounds and inverted screen y");
    const std::vector<noven::data::MapFloorExtent> extents{{"upper",25,34,-222,120,-327,218}};
    Require(noven::data::MapFloorFor({0,25,0},extents,"base")=="upper"
        &&noven::data::MapFloorFor({121,25,0},extents,"base")=="base"
        &&noven::data::MapFloorFor({0,34,0},extents,"base")=="base","generic floor ranges honor spatial and half-open height boundaries");
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
    for(const auto kind:{"loose","lock","switch","stationary","artillery","task"}){
        Write(dir/"map_points.tsv",header+"p2\tmap1\t"+kind+"\t\titem1\tName\tName\t1\t2\t3\n");
        Require(fixture.Load(dir,error)&&fixture.Point("p2"),"supported positioned map data kind loads");
    }
    Write(dir/"map_points.tsv",header+row);Require(fixture.Load(dir,error)&&fixture.Point("p1"),"fixture restores baseline kind");
    rejects("p1\tmap1\tcontainer\t\titem1\tName\tName\tnan\t2\t3\n");
    rejects("p1\tmap1\tcontainer\t\titem1\tName\tName\t1\t2\n");
    rejects("p1\tmap1\tcontainer\t\titem1\tBad\\q\tName\t1\t2\t3\n");
    rejects(std::string("p1\tmap1\tcontainer\t\titem1\t")+"\xFF"+"\tName\t1\t2\t3\n");
    Write(dir/"map_maps.tsv",maps+"map1\tother\tName\tName\t180\t40\t11-15\n");
    Write(dir/"map_points.tsv",header+row);Require(!fixture.Load(dir,error),"duplicate maps reject");
    Write(dir/"map_maps.tsv",maps);Write(dir/"map_points.tsv",header);
    Require(!fixture.Load(dir,error),"empty points reject");
    Write(dir/"map_points.tsv",header+row);
    Write(dir/"map_point_icons.tsv","id\ticons\np1\ttoolbox,duffle\n");
    Require(fixture.Load(dir,error)&&fixture.Point("p1")->icons.size()==2,"optional detailed icons bind to stable point identity");
    Write(dir/"map_point_icons.tsv","id\ticons\nmissing\ttoolbox\n");
    Require(!fixture.Load(dir,error)&&fixture.Point("p1")->icons.size()==2,"unknown icon reference rejects atomically");
    Write(dir/"map_point_icons.tsv","id\ticons\np1\ttoolbox,\n");
    Require(!fixture.Load(dir,error),"empty icon token rejects");
    Write(dir/"map_point_icons.tsv","id\ticons\np1\ttoolbox\np1\tduffle\n");
    Require(!fixture.Load(dir,error),"duplicate point icon assignment rejects");
    std::filesystem::remove(dir/"map_point_icons.tsv");
    Write(dir/"map_outlines.tsv","pointId\tvertex\tx\ty\tz\np1\t0\t1\t2\t3\np1\t1\t4\t5\t6\n");
    Require(fixture.Load(dir,error)&&fixture.Point("p1")->outline.size()==2&&fixture.Point("p1")->outline[1].z==6,"source outlines preserve ordered world vertices");
    Write(dir/"map_outlines.tsv","pointId\tvertex\tx\ty\tz\np1\t1\t1\t2\t3\n");
    Require(!fixture.Load(dir,error)&&fixture.Point("p1")->outline.size()==2,"non-contiguous outline rejects atomically");
    std::filesystem::remove(dir/"map_outlines.tsv");
    Write(dir/"map_points.tsv",header+"p1\tmap1\textract\tpmc\texit1\tName\tName\t1\t2\t3\n");
    Write(dir/"map_conditions.tsv","pointId\tfield\tvalue\np1\tswitch\ttrue\np1\ttransferItem\t{\"count\":5000,\"item\":\"rub\"}\n");
    Require(fixture.Load(dir,error)&&fixture.Point("p1")->conditions.size()==2,"static source extract conditions load without inventing eligibility");
    Write(dir/"map_conditions.tsv","pointId\tfield\tvalue\nmissing\tswitch\ttrue\n");
    Require(!fixture.Load(dir,error)&&fixture.Point("p1")->conditions.size()==2,"unknown condition identity rejects atomically");
    Write(dir/"map_conditions.tsv","pointId\tfield\tvalue\np1\tswitch\ttrue\np1\tswitch\tfalse\n");
    Require(!fixture.Load(dir,error),"duplicate condition fields reject");
    std::filesystem::remove(dir/"map_conditions.tsv");
    Write(dir/"map_references.tsv","mapId\tslug\tbaseFloor\twidth\theight\trotation\tminX\tmaxX\tminZ\tmaxZ\tauthor\nmap1\tinterchange\tbase\t1000\t700\t180\t-433\t598\t-442\t426\tSource Author\n");
    const std::string floorHeader="mapId\tfloorId\tnameZh\tnameEn\torder\tabstractPath\tsatellitePath\n";
    Write(dir/"map_floors.tsv",floorHeader+"map1\tbase\t1F\t1F\t0\tmaps/interchange/base.png\t\n");
    Write(dir/"map_extents.tsv","mapId\tfloorId\tbottom\ttop\tminX\tmaxX\tminZ\tmaxZ\nmap1\tbase\t-10\t10\t-20\t20\t-30\t30\n");
    Require(fixture.Load(dir,error)&&fixture.Maps()[0].floors.size()==1&&fixture.Maps()[0].extents.size()==1
        &&fixture.Maps()[0].projection.width==1000,"optional verified map references and local floor image paths load");
    Write(dir/"map_floors.tsv",floorHeader+"map1\tbase\t1F\t1F\t0\t../unsafe.png\t\n");
    Require(!fixture.Load(dir,error)&&fixture.Maps()[0].floors[0].abstractPath=="maps/interchange/base.png","unsafe image path rejects without losing verified reference");
    std::filesystem::remove(dir/"map_references.tsv");std::filesystem::remove(dir/"map_floors.tsv");std::filesystem::remove(dir/"map_extents.tsv");
    std::filesystem::remove(dir/"map_points.tsv");Require(!fixture.Load(dir,error),"missing table rejects");
    std::filesystem::remove(dir/"map_maps.tsv");Require(std::filesystem::remove(dir),"fixture cleanup removes only owned empty directory");
    std::cout<<"Map catalog snapshot and validation checks passed\n";
}
