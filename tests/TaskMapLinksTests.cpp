#include "ui/TasksPage.h"
#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
void Write(const std::filesystem::path& path,const std::string& value){
    std::ofstream file(path,std::ios::binary);file<<value;Require(file.good(),"fixture writes");
}
std::optional<noven::ui::TasksPage::Action> Click(noven::ui::TasksPage& page,D2D1_RECT_F bounds){
    const float x=(bounds.left+bounds.right)/2,y=(bounds.top+bounds.bottom)/2;
    page.MouseDown(x,y);return page.MouseUp(x,y);
}
}

int main(int argc,char** argv){
    Require(argc==2,"assets directory required");
    using namespace noven;
    const std::string task="5ae449c386f7744bde357697";
    const auto objective=task+"_5bb60cbc88a45011a8235cc5";
    const auto single=task+"_6a60968c58aab7961885e537";
    const auto dir=std::filesystem::temp_directory_path()/("NovenTaskMapLinksTests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    Require(std::filesystem::create_directory(dir),"isolated fixture directory");
    Write(dir/"map_maps.tsv","id\tnormalizedName\tnameZh\tnameEn\trotation\traidDuration\tplayers\nmap1\tinterchange\t立交桥\tInterchange\t180\t40\t11-15\n");
    std::string points="id\tmapId\tkind\tsubtype\tsourceId\tnameZh\tnameEn\tx\ty\tz\n";
    for(int i=0;i<12;++i)points+="point"+std::to_string(i)+"\tmap1\ttask\tvisit\t"+objective+"_"+std::to_string(i)+"\t未知\tUnknown\t1\t25\t-3\n";
    points+="single\tmap1\ttask\tvisit\t"+single+"_0\t未知\tUnknown\t4\t5\t6\n";
    points+="ignored\tmap1\tloose\titem\t"+objective+"_99\t未知\tUnknown\t0\t0\t0\n";
    points+="malformed\tmap1\ttask\tvisit\tbad_\t未知\tUnknown\t0\t0\t0\n";
    Write(dir/"map_points.tsv",points);
    std::wstring error;data::MapCatalog map;Require(map.Load(dir,error),"fixture catalog loads");
    data::TaskMapLinks links;links.Bind(map);
    Require(links.Targets(task,objective).size()==12&&links.Targets(task,single).size()==1,"exact multiple and single objectives");
    Require(links.Targets("wrong",objective).empty()&&links.Targets(task,task+"_missing").empty()
        &&links.Targets("bad","bad_").empty(),"wrong task and absent/malformed identities excluded");
    const auto metadata=*links.Metadata("point1");map=data::MapCatalog{};
    Require(links.Metadata("point1")->mapEn=="Interchange"&&metadata.mapZh=="立交桥"
        &&metadata.position.x==1&&metadata.position.y==25&&metadata.position.z==-3,"metadata owns catalog names and raw coordinates");
    Require(!links.Metadata("ignored")&&!links.Metadata("missing"),"non-task metadata absent");
    Require(map.Load(dir,error),"fixture reloads");
    data::ItemCatalog items;const auto assets=std::filesystem::path(argv[1]);
    Require(items.Load(assets/"data"/"items_catalog.tsv",error),"item catalog loads");
    Require(ui::UiLocalization().DiscoverLocales(assets/"i18n",error),"localization loads");
    ui::TasksPage page;ui::UiTheme theme;
    page.Initialize(assets/"data",items);page.SetMapLinks(map);
    page.Prepare(1600,1200,theme);
    page.MouseDown(theme.sidebarWidth+theme.contentPadding+20,100);page.MouseUp(theme.sidebarWidth+theme.contentPadding+20,100);
    for(const wchar_t c:std::wstring(L"Pathfinder"))Require(page.Char(c),"task query accepts text");
    page.Prepare(1600,1200,theme);
    Require(page.SelectedTask()==task,"fixture task selected by search");
    const auto row=page.ObjectiveBounds(objective),singleRow=page.ObjectiveBounds(single);
    Require(row&&singleRow,"objective bounds available");
    const auto singleAction=Click(page,*singleRow);
    Require(singleAction&&singleAction->destination==ui::TasksPage::Action::Destination::Map&&singleAction->id=="single"
        &&singleAction->mode==data::GameMode::Pvp,"single target emits existing map action with PvP mode");
    const auto query=page.QueryText();const auto scroll=page.Scroll();
    Require(!Click(page,*row)&&page.MapTargetsLayout(),"multiple target opens list without action");
    auto layout=*page.MapTargetsLayout();
    Require(layout.rows[0].id=="point0"&&layout.rows[1].id=="point1"
        &&layout.rows[1].title.ends_with(L" 2")&&layout.rows[1].source.find(L"X 1, Y 25, Z -3")!=std::wstring::npos,"safe ordinal title and source coordinates");
    const auto selected=Click(page,layout.rows[1].bounds);
    Require(selected&&selected->destination==ui::TasksPage::Action::Destination::Map&&selected->id=="point1"
        &&!page.MapTargetsLayout(),"second stable point selected and list closes");
    Require(page.QueryText()==query&&page.Scroll()==scroll&&page.SelectedTask()==task,"map action preserves source context");
    Click(page,*row);layout=*page.MapTargetsLayout();
    page.MouseDown(layout.rows[0].bounds.left+10,layout.rows[0].bounds.top+10);
    Require(!page.MouseUp(layout.rows[1].bounds.left+10,layout.rows[1].bounds.top+10)&&page.MapTargetsLayout(),"release on different row does not navigate");
    Require(page.Key(VK_ESCAPE,false)&&!page.MapTargetsLayout()&&page.QueryText()==query,"ESC closes before changing search");
    Click(page,*row);layout=*page.MapTargetsLayout();Require(!Click(page,layout.close)&&!page.MapTargetsLayout(),"close glyph dismisses");
    Click(page,*row);page.MouseDown(0,0);Require(!page.MouseUp(0,0)&&!page.MapTargetsLayout(),"outside click dismisses without navigation");
    page.Prepare(700,600,theme);
    auto clipped=*page.ObjectiveBounds(objective);
    Require(!Click(page,clipped)&&!page.MapTargetsLayout(),"offscreen objective cannot open list");
    page.Prepare(700,900,theme);
    page.Wheel(-WHEEL_DELTA*7,600,850);for(int i=0;i<60;++i)page.Tick(.016F);
    clipped=*page.ObjectiveBounds(objective);Require(clipped.top>=328&&clipped.bottom<878,"narrow objective revealed by detail scrolling");
    const auto narrowScroll=page.Scroll();Click(page,clipped);layout=*page.MapTargetsLayout();
    Require(layout.bounds.left>=theme.sidebarWidth+theme.contentPadding&&layout.bounds.right<=700-theme.contentPadding
        &&layout.bounds.top>=328&&layout.bounds.bottom<=878&&layout.rows.size()<12,"narrow popup fits detail clip and limits visible rows");
    for(int i=0;i<20;++i)Require(page.Wheel(-WHEEL_DELTA,600,800),"list consumes wheel");
    layout=*page.MapTargetsLayout();Require(layout.rows.back().id=="point11"&&page.Scroll()==narrowScroll,"list scroll reaches last target without scrolling task");
    const auto last=Click(page,layout.rows.back().bounds);Require(last&&last->id=="point11","scrolled list selects exact last ID");
    Click(page,clipped);page.Prepare(700,400,theme);Require(!page.MapTargetsLayout(),"insufficient height suppresses unclickable list");
    Require(page.Key(VK_ESCAPE,false),"hidden popup still dismisses with ESC");
    page.Prepare(1600,1200,theme);
    data::MapCatalog empty;page.SetMapLinks(empty,data::GameMode::Pve);
    Require(!Click(page,*page.ObjectiveBounds(objective))&&page.MapTargetsLayout(),"empty PvE bind leaves regular index intact");
    const auto modeScroll=page.Scroll();
    page.SetMode(data::GameMode::Pve);
    Require(page.Mode()==data::GameMode::Pve&&!page.MapTargetsLayout()&&page.QueryText()==query
        &&page.SelectedTask()==task&&page.Scroll()==modeScroll,"mode switch closes chooser and preserves available task, search and scroll");
    Require(!Click(page,*page.ObjectiveBounds(objective))&&!page.MapTargetsLayout(),"missing PvE index never falls back to regular targets");
    const auto pveDir=dir/"pve";Require(std::filesystem::create_directory(pveDir),"PvE fixture directory");
    std::filesystem::copy_file(dir/"map_maps.tsv",pveDir/"map_maps.tsv");
    Write(pveDir/"map_points.tsv","id\tmapId\tkind\tsubtype\tsourceId\tnameZh\tnameEn\tx\ty\tz\n"
        "pve0\tmap1\ttask\tvisit\t"+objective+"_0\t未知\tUnknown\t101\t125\t-103\n"
        "pve1\tmap1\ttask\tvisit\t"+objective+"_1\t未知\tUnknown\t102\t126\t-104\n"
        "pveSingle\tmap1\ttask\tvisit\t"+single+"_0\t未知\tUnknown\t104\t105\t106\n");
    data::MapCatalog pveMap;Require(pveMap.Load(pveDir,error),"distinct PvE fixture loads");
    page.SetMapLinks(pveMap,data::GameMode::Pve);
    Require(!Click(page,*page.ObjectiveBounds(objective))&&page.MapTargetsLayout(),"PvE chooser opens after PvE catalog bind");
    layout=*page.MapTargetsLayout();
    Require(layout.rows.size()==2&&layout.rows[0].id=="pve0"&&layout.rows[1].id=="pve1"
        &&layout.rows[1].source.find(L"X 102, Y 126, Z -104")!=std::wstring::npos,"PvE chooser reads true PvE IDs and coordinates");
    const auto pveAction=Click(page,layout.rows[1].bounds);
    Require(pveAction&&pveAction->id=="pve1"&&pveAction->mode==data::GameMode::Pve,"PvE multiple target action carries PvE mode");
    const auto pveSingle=Click(page,*page.ObjectiveBounds(single));
    Require(pveSingle&&pveSingle->id=="pveSingle"&&pveSingle->mode==data::GameMode::Pve,"PvE direct action carries PvE mode");
    Click(page,*page.ObjectiveBounds(objective));layout=*page.MapTargetsLayout();
    page.MouseDown(layout.rows[0].bounds.left+10,layout.rows[0].bounds.top+10);
    page.SetMode(data::GameMode::Pvp);
    Require(!page.MouseUp(layout.rows[0].bounds.left+10,layout.rows[0].bounds.top+10)&&!page.MapTargetsLayout(),"mode change cancels pending chooser press");
    Require(page.QueryText()==query&&page.SelectedTask()==task&&page.Scroll()==modeScroll,"regular return preserves source context");
    Click(page,*page.ObjectiveBounds(objective));layout=*page.MapTargetsLayout();
    Require(layout.rows.size()==12&&layout.rows[1].id=="point1","regular return restores regular targets");
    const auto regularAction=Click(page,layout.rows[1].bounds);
    Require(regularAction&&regularAction->id=="point1"&&regularAction->mode==data::GameMode::Pvp,"regular return emits PvP action");
    Click(page,*page.ObjectiveBounds(objective));page.SetMode(data::GameMode::Pvp);
    Require(!page.MapTargetsLayout()&&page.QueryText()==query,"same-mode call also closes chooser");
    ui::TasksPage::Action defaultAction{ui::TasksPage::Action::Destination::Prices,"item"};
    Require(defaultAction.mode==data::GameMode::Pvp,"two-member action aggregate stays compatible");
    page.SetMode(data::GameMode::Seasonal);
    const auto seasonalAction=Click(page,*page.ObjectiveBounds(single));
    Require(seasonalAction&&seasonalAction->id=="single"&&seasonalAction->mode==data::GameMode::Seasonal,"Seasonal reads regular index and retains action mode");
    page.SetMode(data::GameMode::Pvp);
    page.SetMapLinks(empty,data::GameMode::Seasonal);
    Require(!Click(page,*page.ObjectiveBounds(objective))&&!page.MapTargetsLayout(),"Seasonal shares regular index and absent index cannot link");
    Click(page,{theme.sidebarWidth+theme.contentPadding,84,1600-theme.contentPadding,122});
    Require(page.Key(VK_ESCAPE,false),"clear search for mode-specific task query");
    for(const wchar_t c:std::wstring(L"6834145ebc1f443d7603c8a7"))Require(page.Char(c),"PvE-only task ID query");
    Require(page.SelectedTask().empty(),"PvE-only task absent from regular query");
    const auto pveQuery=page.QueryText();page.SetMode(data::GameMode::Pve);
    Require(page.SelectedTask()=="6834145ebc1f443d7603c8a7"&&page.QueryText()==pveQuery,"mode change re-queries PvE task structure with retained search");
    page.SetMode(data::GameMode::Pvp);
    Require(page.SelectedTask().empty()&&page.QueryText()==pveQuery&&page.Scroll()==0,"missing task clears selection without dropping query");
    Require(page.Key(VK_ESCAPE,false),"clear search for regular-only task");
    for(const wchar_t c:std::wstring(L"66058cb22cee99303f1ba067"))Require(page.Char(c),"regular-only task ID query");
    Require(page.SelectedTask()=="66058cb22cee99303f1ba067","regular-only task selected");
    page.SetMode(data::GameMode::Pve);Require(page.SelectedTask().empty(),"regular-only task removed on PvE re-query");
    page.SetMode(data::GameMode::Pvp);Require(page.SelectedTask()=="66058cb22cee99303f1ba067","regular-only task restored by preserved query");
    ui::TasksPage beforeInitialize;beforeInitialize.SetMode(data::GameMode::Pve);
    beforeInitialize.Initialize(assets/"data",items);beforeInitialize.Prepare(1600,1200,theme);
    Require(beforeInitialize.Mode()==data::GameMode::Pve&&!beforeInitialize.SelectedTask().empty(),"mode can be supplied before Initialize");
    links.Bind(empty);Require(links.Targets(task,objective).empty()&&!links.Metadata("point1"),"rebind clears owned links and metadata");
    std::filesystem::remove_all(dir);
    std::cout<<"TaskMapLinksTests PASS\n";
}
