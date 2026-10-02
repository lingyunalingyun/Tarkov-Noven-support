#include "ui/MapPage.h"
#include "data/TaskMapLinks.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace {
using namespace noven;
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
bool Near(double a,double b){return std::abs(a-b)<.005;}
void Write(const std::filesystem::path& path,const std::string& text){
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary);file<<text;Require(file.good(),"fixture write");
}
void Png(const std::filesystem::path& path){
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))
        &&SUCCEEDED(factory->CreateStream(&stream))
        &&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"PNG fixture stream");
    Require(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))
        &&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))
        &&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))
        &&SUCCEEDED(frame->SetSize(1,1)),"PNG fixture encoder");
    auto format=GUID_WICPixelFormat24bppBGR;BYTE pixel[3]{30,60,90};
    Require(SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat24bppBGR
        &&SUCCEEDED(frame->WritePixels(1,3,3,pixel))&&SUCCEEDED(frame->Commit())
        &&SUCCEEDED(encoder->Commit()),"PNG fixture encoding");
}
void Icons(const std::filesystem::path& assets){
    const auto image=assets/"fixture.png";Png(image);
    const auto icons=assets/"maps"/"icons";std::filesystem::create_directories(icons);
    for(const auto& icon:ui::MapDetailIcons)
        std::filesystem::copy_file(image,icons/("detail_"+std::string(icon.id)+".png"));
    for(const auto id:ui::MapIconImages::Categories){
        const auto path=icons/("category_"+std::string(id)+".png");
        if(!std::filesystem::exists(path))std::filesystem::copy_file(image,path);
    }
}
void CatalogFixture(const std::filesystem::path& directory,bool pve){
    std::string maps="id\tnormalizedName\tnameZh\tnameEn\trotation\traidDuration\tplayers\n";
    std::string refs="mapId\tslug\tbaseFloor\twidth\theight\trotation\tminX\tmaxX\tminZ\tmaxZ\tauthor\n";
    std::string floors="mapId\tfloorId\tnameZh\tnameEn\torder\tabstractPath\tsatellitePath\n";
    std::string extents="mapId\tfloorId\tbottom\ttop\tminX\tmaxX\tminZ\tmaxZ\n";
    std::string points="id\tmapId\tkind\tsubtype\tsourceId\tnameZh\tnameEn\tx\ty\tz\n";
    for(int row=0;row<17;++row){
        const int i=pve&&row==15?17:row;
        const auto id="map"+std::to_string(i),prefix=(pve?"p":"r")+std::to_string(i);
        maps+=id+"\t"+id+"\t地图 "+std::to_string(i)+"\tMap "+std::to_string(i)+"\t90\t30\t1-8\n";
        points+=prefix+"_task\t"+id+"\ttask\tobjective\ttask"+std::to_string(i)+"_obj0_zone0\t目标\tTarget\t"
            +(pve?"2\t15\t-4":"-2\t15\t4")+"\n";
        // 一图有来源坐标但无引用；不能伪造底图，picker 必须能恢复到其他地图。
        // One map has source points without references; never invent its background, and keep picker recovery available.
        if(i==16)continue;
        refs+=id+"\t"+id+"\tbase\t200\t100\t"+std::to_string((i%4)*90)+"\t-10\t10\t-20\t20\tFixture author\n";
        floors+=id+"\tbase\t主层\tBase\t1\tfixture.png\t\n";
        floors+=id+"\tupper\t上层\tUpper\t0\tfixture.png\t\n";
        extents+=id+"\tupper\t10\t20\t-5\t5\t-10\t10\n";
        points+=id+"_common\t"+id+"\tspawn\tpmc\tspawn"+std::to_string(i)+"\t区域\tArea\t"
            +(pve?"8\t15\t-12":"-8\t15\t12")+"\n";
    }
    if(pve)points+="pve_extra\tmap0\tboss\tboss\tboss0\t首领\tBoss\t0\t0\t0\n";
    Write(directory/"map_maps.tsv",maps);Write(directory/"map_points.tsv",points);
    Write(directory/"map_references.tsv",refs);Write(directory/"map_floors.tsv",floors);Write(directory/"map_extents.tsv",extents);
}
void Click(ui::MapPage& page,D2D1_RECT_F rect){
    const auto x=(rect.left+rect.right)*.5F,y=(rect.top+rect.bottom)*.5F;page.MouseDown(x,y);page.MouseUp(x,y);
}
void Settle(ui::MapPage& page){for(int i=0;i<60;++i)page.Tick(.016F);}
void VerifyPageMap(ui::MapPage& page,const data::MapRecord& map){
    page.SelectMap(map.id);page.Prepare(1100,700,{});
    const auto expected=std::count_if(page.Catalog().Points().begin(),page.Catalog().Points().end(),
        [&](const auto& point){return point.mapId==map.id;});
    const auto& points=page.Points();
    Require(points.size()==static_cast<std::size_t>(map.floors.empty()?0:expected),"per-map visible count matches all supported source points");
    std::set<std::string> ids;
    for(const auto& point:points){
        Require(ids.emplace(point.id).second,"per-map point IDs are unique");
        const auto* source=page.Catalog().Point(point.id);
        Require(source&&source->mapId==map.id&&!source->sourceId.empty(),"every displayed point resolves the same map/source identity");
        const auto projected=map.projection.Project(source->position);
        Require(Near(point.coordinate.x,projected[0])&&Near(point.coordinate.y,projected[1]),"page uses each map generic projection");
        Require(point.floorId==data::MapFloorFor(source->position,map.extents,map.baseFloor),"page uses source floor extents and base fallback");
        Require(!point.title.empty()&&!point.title.starts_with(L"[missing:"),"generic point label resolves");
    }
    Require(page.Information()&&page.Information()->id==map.id&&page.Layout().stack.count==map.floors.size(),"selected map information and floor count stay local");
}
std::vector<std::string> Fields(std::string line){
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    std::vector<std::string> result;std::size_t start=0;
    while(true){const auto end=line.find('\t',start);result.push_back(line.substr(start,end-start));
        if(end==std::string::npos)return result;start=end+1;}
}
void FullCatalog(const std::filesystem::path& directory,std::string_view mode){
    data::MapCatalog catalog;std::wstring error;Require(catalog.Load(directory,error),"full generated catalog loads");
    std::ifstream metadata(directory/"map_catalog.meta.json",std::ios::binary);
    Require(metadata.good(),"full catalog metadata exists");
    const std::string meta((std::istreambuf_iterator<char>(metadata)),{});
    const auto number=[&](const char* key){std::smatch match;
        Require(std::regex_search(meta,match,std::regex(std::string("\"")+key+"\"\\s*:\\s*([0-9]+)")),"metadata count exists");
        return std::stoull(match[1].str());};
    Require(std::regex_search(meta,std::regex("\"map\"\\s*:\\s*\"all\""))
        &&std::regex_search(meta,std::regex("\"structureMode\"\\s*:\\s*\""+std::string(mode)+"\"")),"full catalog scope and mode match provenance");
    Require(catalog.Maps().size()==number("mapCount")&&catalog.Maps().size()>3
        &&catalog.Points().size()==number("pointCount"),"full catalog total counts match generator metadata");
    std::ifstream input(directory/"map_points.tsv",std::ios::binary);std::string line;
    Require(static_cast<bool>(std::getline(input,line)),"full source TSV header exists");
    std::map<std::string,std::size_t> sourceCounts,loadedCounts;std::set<std::string> sourceIds;
    std::map<std::string_view,const data::MapPointRecord*,std::less<>> loadedPoints;
    for(const auto& point:catalog.Points())loadedPoints.emplace(point.id,&point);
    while(std::getline(input,line)){
        const auto fields=Fields(line);Require(fields.size()==10,"full source TSV column count");
        const auto found=loadedPoints.find(fields[0]);const auto* point=found==loadedPoints.end()?nullptr:found->second;
        Require(point&&point->mapId==fields[1]&&point->kind==fields[2]&&point->sourceId==fields[4]
            &&sourceIds.insert(fields[0]).second,"full source point IDs, kinds and map ownership retained");
        Require(Near(point->position.x,std::stod(fields[7]))&&Near(point->position.y,std::stod(fields[8]))
            &&Near(point->position.z,std::stod(fields[9])),"full source XYZ retained");
        ++sourceCounts[fields[1]];
    }
    Require(input.eof(),"full source TSV read completes");
    for(const auto& point:catalog.Points())++loadedCounts[point.mapId];
    Require(sourceCounts==loadedCounts&&sourceIds.size()==catalog.Points().size(),"full per-map source counts match without dropped or invented points");
    Require(sourceCounts.size()==catalog.Maps().size(),"every full catalog map has its own source points");
    std::cout<<mode<<": "<<catalog.Maps().size()<<" maps, "<<catalog.Points().size()<<" source points verified\n";
}
}

// 用法：assets [regular-all-data-directory pve-all-data-directory]；CMake 由主代理注册。
// Usage: assets [regular-all-data-directory pve-all-data-directory]; the main agent owns CMake registration.
int main(int argc,char** argv){
    Require(argc==2||argc==4,"assets plus optional full regular/PvE data directories required");
    Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"fixture WIC apartment");
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    std::wstring error;Require(ui::UiLocalization().DiscoverLocales(std::filesystem::path(argv[1])/"i18n",error),"localization loads");
    Require(ui::UiLocalization().SetLocale("en-US"),"fixture labels use English");
    const auto fixture=std::filesystem::temp_directory_path()/("NovenMapAllMapsTests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    Require(std::filesystem::create_directory(fixture),"isolated multi-map fixture directory");
    Icons(fixture);CatalogFixture(fixture/"data",false);CatalogFixture(fixture/"data"/"pve",true);
    ui::MapPage page;Require(page.Initialize(fixture,error)&&page.RealData(),"generic multi-map fixture initializes offline");
    page.Prepare(1100,700,{});
    Require(page.Mode()==data::GameMode::Pvp&&page.Catalog().Maps().size()==17&&page.Catalog().Points().size()==33
        &&page.Catalog(data::GameMode::Pve).Maps().size()==17&&page.Catalog(data::GameMode::Pve).Points().size()==34,"complete fixture catalog totals stay independent by mode");
    for(const auto& map:page.Catalog().Maps())VerifyPageMap(page,map);
    page.SelectMap("map0");page.Prepare(1100,700,{});
    Require(page.OpenInteraction("r0_task")&&page.MapId()=="map0"&&page.FloorId()=="upper"
        &&page.SelectedPoint()&&page.SelectedPoint()->id=="r0_task","external focus resolves exact regular source point and upper extent");
    Require(Near(page.SelectedPoint()->coordinate.x,80)&&Near(page.SelectedPoint()->coordinate.y,40),"zero rotation matches independently calculated fixture coordinate");
    Require(page.OpenInteraction("map0_common")&&page.FloorId()=="base","spatially outside upper extent falls back to base despite matching height");
    const auto regularCoordinate=page.SelectedPoint()->coordinate;
    const std::string previous(page.InteractionId());Require(!page.OpenInteraction("p0_task")&&page.InteractionId()==previous,"PvE-only ID cannot focus regular source");
    Require(page.OpenInteraction("r3_task")&&page.MapId()=="map3"&&page.SelectedPoint()->id=="r3_task","exact source focus crosses maps with different rotation");
    Require(Near(page.SelectedPoint()->coordinate.x,120)&&Near(page.SelectedPoint()->coordinate.y,40),"270-degree rotation matches independently calculated fixture coordinate");
    page.SelectMap("map0");page.Prepare(1100,500,{});
    auto picker=ui::MapPicker::Sample(page.Layout(),0,17);Click(page,picker.header);
    Require(picker.Maximum(17)>0&&page.Wheel(-WHEEL_DELTA*100,picker.panel.left+10,picker.panel.top+10),"large picker consumes wheel and clamps to last rows");
    picker=ui::MapPicker::Sample(page.Layout(),picker.Maximum(17),17);
    const auto missingRow=picker.Row(16);
    Require(picker.Hit(missingRow.left+10,missingRow.top+10,17)==16
        &&!picker.Hit(picker.panel.left-1,missingRow.top+10,17),"scrolled picker stable row hit is clipped");
    Click(page,missingRow);page.Prepare(1100,500,{});
    Require(page.MapId()=="map16"&&!page.Selected()&&page.Points().empty()&&page.Layout().stack.count==0
        &&page.Information()&&!page.Information()->baseFloor.size(),"missing reference remains real map identity without demo floor or invented points");
    Require(!page.OpenInteraction("r16_task"),"source point without a map reference is not focusable");
    picker=ui::MapPicker::Sample(page.Layout(),0,17);Click(page,picker.header);
    Require(page.Wheel(WHEEL_DELTA*100,picker.panel.left+10,picker.panel.top+10),"missing reference picker can scroll back");
    Click(page,picker.Row(0));page.Prepare(1100,700,{});
    Require(page.MapId()=="map0"&&page.Layout().stack.count==2&&page.OpenInteraction("r0_task"),"picker recovers supported map after missing reference");
    Settle(page);page.Prepare(1100,700,{});
    picker=ui::MapPicker::Sample(page.Layout(),0,17);Click(page,picker.header);
    Require(page.Key(VK_ESCAPE,false)&&page.InteractionId()=="r0_task","ESC dismisses picker before clearing exact point focus");
    Click(page,picker.header);page.MouseDown(picker.Row(1).left+10,picker.Row(1).top+10);
    page.MouseUp(picker.Row(2).left+10,picker.Row(2).top+10);
    Require(page.MapId()=="map0","different picker press/release rows do not navigate");
    page.Key(VK_ESCAPE,false);
    const auto pveButton=D2D1_RECT_F{page.Layout().search.right-70,130,page.Layout().search.right,166};
    Click(page,pveButton);page.Prepare(1100,700,{});
    Require(page.Mode()==data::GameMode::Pve&&page.MapId()=="map0"&&page.InteractionId().empty(),"PvE button rebinds mode, retains shared map and clears old focus");
    Require(page.Animating(),"mode selection activates the shared page animation clock");
    Settle(page);Require(!page.Animating(),"mode animation finishes without permanent redraw");
    for(const auto& map:page.Catalog().Maps())VerifyPageMap(page,map);
    picker=ui::MapPicker::Sample(page.Layout(),0,17);Click(page,picker.header);
    Require(page.Wheel(-WHEEL_DELTA*100,picker.panel.left+10,picker.panel.top+10),"PvE picker scrolls its rebound map list");
    picker=ui::MapPicker::Sample(page.Layout(),picker.Maximum(17),17);Click(page,picker.Row(15));
    Require(page.MapId()=="map17"&&page.Information()->normalizedName=="map17","PvE picker row selects PvE-only map rather than old regular row");
    Require(page.OpenInteraction("p0_task")&&page.SelectedPoint()->id=="p0_task"&&page.FloorId()=="upper","PvE exact focus uses PvE source target");
    Require(!page.OpenInteraction("r0_task")&&page.InteractionId()=="p0_task","regular-only point ID cannot focus PvE catalog");
    Require(page.OpenInteraction("map0_common")&&!Near(page.SelectedPoint()->coordinate.x,regularCoordinate.x),"same stable point ID rebinds to PvE coordinates");
    data::TaskMapLinks regularLinks,pveLinks;
    regularLinks.Bind(page.Catalog(data::GameMode::Pvp));pveLinks.Bind(page.Catalog(data::GameMode::Pve));
    const auto regularTargets=regularLinks.Targets("task0","task0_obj0"),pveTargets=pveLinks.Targets("task0","task0_obj0");
    Require(regularTargets.size()==1&&pveTargets.size()==1&&regularTargets.front()=="r0_task"
        &&pveTargets.front()=="p0_task","task source IDs resolve distinct target IDs in each mode");
    page.SelectMap("map17");Require(page.SetMode(data::GameMode::Pvp)&&page.MapId()=="map0","missing map in new mode recovers to available map");
    page.SelectMap("map15");Require(page.SetMode(data::GameMode::Pve)&&page.MapId()=="map0","regular-only map mode transition recovers to available PvE map");
    Require(page.SetMode(data::GameMode::Pvp)&&page.OpenInteraction("r0_task")&&!page.OpenInteraction("p0_task"),"regular return restores regular focus source IDs");
    Require(!page.SetMode(data::GameMode::Pvp)&&!page.SetMode(data::GameMode::Seasonal)
        &&page.Mode()==data::GameMode::Pvp&&page.InteractionId()=="r0_task","Seasonal normalizes to already active PvP without clearing regular focus");
    Require(page.SetMode(data::GameMode::Pve)&&page.OpenInteraction("p0_task"),"PvE focus established before Seasonal fallback");
    Require(page.SetMode(data::GameMode::Seasonal)&&page.Mode()==data::GameMode::Pvp
        &&page.MapId()=="map0"&&page.InteractionId().empty(),"Seasonal from PvE switches to regular and clears old focus");
    Require(page.Catalog().Points().size()==33&&page.OpenInteraction("r0_task")&&!page.OpenInteraction("p0_task"),"Seasonal fallback uses regular catalog and exact regular source IDs");
    Settle(page);page.Prepare(1100,700,{});
    const auto view=page.Viewport().Bounds();
    Require(page.Wheel(WHEEL_DELTA*3,view.left+30,view.top+40),"zoom establishes non-default view before asset reload");
    Click(page,page.Layout().search);for(const auto c:std::wstring(L"Target"))Require(page.Char(c),"query before asset reload");
    Click(page,page.Layout().filters[1]);Settle(page);Click(page,page.FilterList().Row(0));
    const std::string reloadMap(page.MapId()),reloadFloor(page.FloorId()),reloadPoint(page.InteractionId());
    const auto reloadMode=page.Mode();const auto reloadQuery=page.Search().Text();const auto reloadFilters=page.Filters();
    const auto reloadPanel=page.Panel();const auto reloadScale=page.Viewport().Scale();
    const auto reloadAnchor=page.Viewport().ToMap({view.left+17,view.top+23});
    Require(page.SelectedPoint()&&page.SelectedPoint()->id==reloadPoint,"selected source remains visible before reload");
    const auto reloadCount=page.Points().size(),reloadBuilds=page.PointQueryBuilds();
    const auto validGeneration=fixture/"generation",badGeneration=fixture/"bad-generation",missingGeneration=fixture/"missing-generation";
    std::filesystem::create_directory(validGeneration);
    std::filesystem::copy_file(fixture/"fixture.png",validGeneration/"fixture.png");
    Write(badGeneration/"fixture.png","invalid PNG fixture");
    std::filesystem::create_directory(missingGeneration);
    for(const auto& generation:std::vector<std::filesystem::path>{validGeneration,validGeneration,badGeneration,missingGeneration,{}}){
        page.SetAssetGeneration(generation);
        const auto anchor=page.Viewport().ToMap({view.left+17,view.top+23});
        Require(page.Mode()==reloadMode&&page.MapId()==reloadMap&&page.FloorId()==reloadFloor
            &&page.InteractionId()==reloadPoint&&page.Selected()&&page.SelectedPoint()->id==reloadPoint,"asset generation reload preserves mode, map, floor and selected source");
        Require(Near(page.Viewport().Scale(),reloadScale)&&Near(anchor.x,reloadAnchor.x)&&Near(anchor.y,reloadAnchor.y),"valid, unchanged, invalid and missing generations preserve zoom and pan transform");
        Require(page.Search().Text()==reloadQuery&&page.Panel()==reloadPanel
            &&page.Filters().categories==reloadFilters.categories&&page.Filters().grid==reloadFilters.grid
            &&page.Filters().geometry==reloadFilters.geometry&&page.Filters().satellite==reloadFilters.satellite
            &&page.Filters().hiddenTasks==reloadFilters.hiddenTasks&&page.Filters().hiddenIcons==reloadFilters.hiddenIcons,"asset reload preserves query, open panel and filters");
        Require(page.Points().size()==reloadCount&&page.PointQueryBuilds()==reloadBuilds,"image-only reload leaves source query cache intact");
    }
    for(float width:{700.0F,1100.0F,1600.0F})for(float height:{500.0F,700.0F,900.0F}){
        const auto layout=ui::MapLayout::Sample(width,height,{},1,16,17);
        for(std::size_t i=0;i<16;++i)for(const auto vertex:layout.stack.Plate(i).vertices)
            Require(vertex.y<layout.filters.front().top,"sixteen selected floors fit above filter hit regions");
        for(const auto filter:layout.filters){
            Require(filter.bottom<=layout.content.bottom,"sixteen-floor layout keeps filters inside content");
            Require(!layout.stack.Hit({(filter.left+filter.right)*.5F,(filter.top+filter.bottom)*.5F}),"filter centers cannot hit any of sixteen floor plates");
        }
    }
    for(float width:{700.0F,1100.0F,1600.0F}){
        page.Prepare(width,500,{});const auto list=ui::MapPicker::Sample(page.Layout(),0,17);
        Require(list.panel.left>=page.Layout().content.left&&list.panel.right<=page.Layout().search.right
            &&list.panel.bottom<=page.Layout().content.bottom,"picker bounds fit narrow and wide layouts");
        Require(!list.Hit(list.panel.right+1,list.panel.top+10,17)
            &&!list.Hit(list.panel.left+10,list.panel.bottom+1,17),"picker hit rejects outside clip bounds");
    }
    std::filesystem::remove_all(fixture);
    if(argc==4){FullCatalog(argv[2],"regular");FullCatalog(argv[3],"pve");}
    else std::cout<<"Full generated catalog checks not requested; multi-map fixtures verified\n";
    std::cout<<"MapAllMapsTests PASS\n";
}
