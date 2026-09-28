#include "data/HideoutBrowser.h"
#include "ui/HideoutPage.h"
#include "ui/DurationFormat.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <cstdlib>

namespace {
void Check(bool ok,const char* message) { if (!ok) { std::cerr<<message<<'\n'; std::exit(1); } }
void Write(const std::filesystem::path& dir,const char* name,const std::string& text) {
    std::ofstream(dir/(std::string("hideout_")+name+".tsv"),std::ios::binary)<<text;
}
const std::string bolt="57347c5b245977448d35f6e1";
void Fixture(const std::filesystem::path& dir) {
    Write(dir,"station_images","mode\tstationId\timageKey\nregular\ts\tstation-workbench\n");
    Write(dir,"crafts","mode\tid\tstationId\tlevel\tseconds\titemId\tcount\trestrictions\nregular\tc1\ts\t2\t3600\t"+bolt+"\t2\tquest\n");
    Write(dir,"craft_materials","mode\tcraftId\titemId\tcount\ttool\tfunctional\nregular\tc1\t"+bolt+"\t0.66\t1\t0\n");
    Write(dir,"stations","mode\tid\tnameZh\tnameEn\nregular\ts\t工作台\tWorkbench\nregular\tt\t照明\tIllumination\n");
    Write(dir,"levels","mode\tid\tstation\tlevel\tseconds\nregular\ts-2\ts\t2\t10800\nregular\tt-1\tt\t1\t\nregular\ts-1\ts\t1\t0\n");
    Write(dir,"item_requirements","mode\tlevelId\titemId\tcount\nregular\ts-2\t"+bolt+"\t4\n");
    Write(dir,"station_requirements","mode\tlevelId\tstationId\tlevel\nregular\ts-2\tt\t1\n");
    Write(dir,"skill_requirements","mode\tlevelId\tskillId\tnameZh\tnameEn\tlevel\nregular\ts-2\tAttention\t专注\tAttention\t2\n");
    Write(dir,"trader_requirements","mode\tlevelId\ttraderId\tnameZh\tnameEn\tlevel\nregular\ts-2\tmechanic\t机械师\tMechanic\t2\n");
}
}
int wmain(int argc,wchar_t** argv) {
    using namespace noven::data; using namespace noven::ui;
    Check(argc==2,"asset directory"); const std::filesystem::path assets=argv[1];
    std::wstring error; HideoutCatalog production;
    Check(production.Load(assets/"data",error),"generated catalog loads");
    Check(!production.Levels().empty(),"generated source levels");
    const auto temp=std::filesystem::temp_directory_path()/("NovenHideout-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directory(temp); Fixture(temp);
    HideoutCatalog catalog; Check(catalog.Load(temp,error),"fixture loads");
    Check(catalog.Levels()[0].level==1 && catalog.Levels()[1].items[0].count==4,"level ordering and quantities");
    Check(catalog.Levels()[1].stations[0].stationId=="t" && catalog.Levels()[1].skills[0].level==2
        && catalog.Levels()[1].traders[0].id=="mechanic","prerequisite identities");
    Check(catalog.Station("regular","s")->nameZh=="工作台","Unicode station name");
    ItemCatalog items; Check(items.Load(assets/"data/items_catalog.tsv",error),"item catalog");
    ItemEconomyStore economy;
    for (auto mode:AllGameModes()) {
        const auto price=18000+static_cast<int>(mode)*100;
        const auto payload="{\"data\":{\"fleaMarket\":{\"enabled\":true},\"items\":{\""+bolt+"\":{\"id\":\""+bolt+"\",\"lastLowPrice\":"+std::to_string(price)+",\"width\":1,\"height\":1}}}}";
        Check(economy.ReplaceFromUpstreamJson(mode,payload,error),"economy fixture");
    }
    HideoutBrowser browser(catalog,items,economy);
    Check(catalog.Station("regular","s")->imageKey=="station-workbench","station image key from generated data");
    Check(browser.Crafts(catalog.Levels()[0],"en-US").empty(),"level one excludes level two crafts");
    const auto crafts=browser.Crafts(catalog.Levels()[1],"en-US");
    Check(crafts.size()==1 && crafts[0].source->count==2 && crafts[0].source->seconds==3600
        && crafts[0].materials[0].tool && crafts[0].materials[0].count==0.66
        && !crafts[0].source->restrictions.empty(),"craft quantities, fractional resource, tools, duration and restrictions");
    Check(browser.Crafts(catalog.Levels()[1],"zh-CN")[0].name!=crafts[0].name,"craft item names follow ItemCatalog locale");
    const auto zh=browser.Query("工作台",GameMode::Pvp,"zh-CN"), en=browser.Query("Workbench",GameMode::Pvp,"en-US");
    Check(zh.size()==2 && en.size()==2 && zh[0].source==en[0].source && zh[0].name!=en[0].name,"bilingual search preserves identity");
    for (const auto& q : {std::string("Bolts"),std::string("螺栓"),bolt,items.FindById(bolt)->shortNameEn})
    {
        Check(browser.Query(q,GameMode::Pvp,"en-US").size()==1,"product name/short/ID search");
        Check(browser.Query("#"+q,GameMode::Pvp,"en-US").size()==1,"requirement name/short/ID search");
    }
    Check(browser.Query("  # bOlTs ",GameMode::Pvp,"en-US").size()==1,"requirement whitespace and case normalization");
    Check(browser.Query("#Workbench",GameMode::Pvp,"en-US").empty(),"requirement search excludes station names");
    const auto productRows=browser.Query("Bolts",GameMode::Pvp,"en-US");
    Check(productRows[0].filteredDetails && productRows[0].materials.empty(),"product search hides unrelated upgrade materials");
    Check(!en[0].filteredDetails,"station name search retains complete details");
    Check(browser.Crafts(*productRows[0].source,"en-US","Bolts").size()==1
        && browser.Crafts(*productRows[0].source,"en-US","no such product").empty(),"details filter craft outputs");
    Check(browser.Query("#",GameMode::Pvp,"en-US").size()==browser.Query("",GameMode::Pvp,"en-US").size(),"bare hash keeps list while typing");
    Check(browser.Query("no such thing zzz",GameMode::Pvp,"en-US").empty(),"no results");
    const auto ordered=browser.Query("",GameMode::Pvp,"en-US");
    Check(ordered[0].name=="Illumination" && ordered[1].source->level==1,"neutral ordering");
    for (auto mode:AllGameModes()) {
        const auto row=browser.Query("#"+bolt,mode,"en-US")[0];
        Check(row.completeEstimate && row.knownSubtotal==(18000+static_cast<int>(mode)*100)*4
            && row.materials[0].unitPrice==(18000+static_cast<int>(mode)*100),"mode-specific complete flea estimate");
        Check(row.source->mode=="regular","canonical/seasonal structural fallback");
    }
    ItemEconomyStore missing; HideoutBrowser noPrices(catalog,items,missing);
    const auto unknown=noPrices.Query("#"+bolt,GameMode::Pvp,"en-US")[0];
    Check(!unknown.completeEstimate && unknown.unknownRequirementCount==1 && !unknown.materials[0].subtotal,"missing is incomplete not zero price");
    HideoutMaterial material; material.count=4; material.unitPrice=18000; material.status=FleaStatus::Banned;
    std::int64_t total=0;
    Check(!AddFleaEstimate(material,total) && !material.subtotal,"banned no fake zero");
    material.status=FleaStatus::LockedOrUnavailable; Check(!AddFleaEstimate(material,total),"locked unknown");
    material.status=FleaStatus::Unknown; Check(!AddFleaEstimate(material,total),"unknown status unknown");
    material.status=FleaStatus::Allowed; material.unitPrice=(std::numeric_limits<std::int64_t>::max)();
    Check(!AddFleaEstimate(material,total) && total==0,"multiply overflow guarded");
    material.count=1; total=1; Check(!AddFleaEstimate(material,total) && total==1,"sum overflow guarded");
    Check(UiLocalization().DiscoverLocales(assets/"i18n",error),"localization");
    Check(FormatDuration(108000)==L"1 天 6 小时" && FormatDuration({})==Tr(TextKey::Unknown),"duration units and missing");
    HideoutPage page; UiTheme theme; page.Initialize(temp,items,economy); page.Prepare(1100,850,theme);
    const float left=theme.sidebarWidth+theme.contentPadding;
    page.MouseDown(left+20,100); page.MouseUp(left+20,100);
    for (wchar_t c:std::wstring(L"Workbench")) Check(page.Char(c),"search committed text");
    Check(page.Rows().size()==2 && page.Scroll()==0,"query resets scroll");
    Check(page.StationCount()==1,"search groups station levels into one tab");
    for(int i=0;i<60;++i) page.Tick(.016F);
    page.MouseDown(left+125,385); page.MouseUp(left+125,385);
    for (int i=0;i<60;++i) page.Tick(0.016F);
    const auto expanded=page.ExpandedId(); Check(expanded=="s-2" && page.VisibleImages().size()<=32,"level selection and bounded images");
    Check(page.SelectedStation()=="s","level selection preserves station identity");
    page.MouseDown(left+40,520);
    Check(page.MouseUp(left+40,520)==bolt,"upgrade item click returns stable ID");
    page.MouseDown(left+40,520);
    Check(!page.MouseUp(left+40,490),"release outside item does not navigate");
    page.MouseDown(left+35,385);page.MouseUp(left+35,385);
    Check(page.ExpandedId()=="s-1","level navigation stays interactive during content fade");
    page.MouseDown(left+125,385);page.MouseUp(left+125,385);
    Check(page.ExpandedId()=="s-2","rapid level selection retargets without hiding navigation");
    page.MouseDown(left+150,150); page.MouseUp(left+150,150);
    Check(page.Mode()==GameMode::Pve && page.ExpandedId()==expanded,"mode preserves expansion");
    UiLocalization().SetLocale("en-US"); page.Prepare(1100,850,theme);
    Check(page.Mode()==GameMode::Pve && page.QueryText()==L"Workbench" && page.ExpandedId()==expanded,"locale preserves UI state");
    HideoutPage other; other.Initialize(temp,items,economy); Check(other.Mode()==GameMode::Pvp,"independent page state");
    page.MouseDown(left+20,100); page.MouseUp(left+20,100);
    Check(page.Key(VK_ESCAPE,false),"focused search clears"); page.Wheel(-120); for(int i=0;i<60;++i) page.Tick(.016F);
    page.Char(L'x'); Check(page.Scroll()==0,"edit resets scroll after movement");
    Check(FormatDuration(2700)==L"45 min","English duration");
    Fixture(temp);
    Write(temp,"stations","mode\tid\tnameZh\tnameEn\nregular\ts\t螺栓\tBolts\nregular\tt\t照明\tIllumination\n");
    std::ofstream(temp/"hideout_item_requirements.tsv",std::ios::app)<<"regular\tt-1\t"+bolt+"\t1\n";
    Check(catalog.Load(temp,error),"rank fixture");
    Check(browser.Query("Bolts",GameMode::Pvp,"en-US")[0].source->level==1,"exact station outranks product match");
    Check(browser.Query("Bolts",GameMode::Pvp,"en-US").size()==2,"plain search excludes upgrade-only matches");
    Check(browser.Query("#Bolts",GameMode::Pvp,"en-US").size()==2,"hash search includes upgrade-only matches");
    Fixture(temp);
    Write(temp,"craft_materials","mode\tcraftId\titemId\tcount\ttool\tfunctional\nregular\tc1\tcraft-only-input\t1\t0\t0\n");
    Check(catalog.Load(temp,error),"craft input search fixture");
    Check(browser.Query("craft-only-input",GameMode::Pvp,"en-US").empty(),"plain search excludes recipe inputs");
    Check(browser.Query("#craft-only-input",GameMode::Pvp,"en-US").empty(),"hash search excludes recipe-only inputs");
    const auto inputRows=browser.Query("#Bolts",GameMode::Pvp,"en-US");
    Check(inputRows.size()==1 && inputRows[0].materials.size()==1
        && browser.Crafts(*inputRows[0].source,"en-US","#Bolts").empty(),
        "hash details show upgrade materials without recipes");
    Fixture(temp);
    std::ofstream(temp/"hideout_item_requirements.tsv",std::ios::app)<<"regular\ts-2\tmissing-item\t1\n";
    Check(catalog.Load(temp,error),"unknown catalog join fixture");
    const auto partial=browser.Query("#"+bolt,GameMode::Pvp,"en-US")[0];
    Check(partial.knownSubtotal==72000 && partial.unknownRequirementCount==1 && !partial.completeEstimate,"partial sum never claims complete total");
    Fixture(temp);
    std::ofstream(temp/"hideout_stations.tsv",std::ios::app)<<"pve\ts\t工作台\tWorkbench\n";
    std::ofstream(temp/"hideout_levels.tsv",std::ios::app)<<"pve\ts-1\ts\t1\t60\n";
    Check(catalog.Load(temp,error) && catalog.StructureMode(GameMode::Pve)=="pve"
        && catalog.StructureMode(GameMode::Seasonal)=="regular","explicit variants preserved, Seasonal maps regular");
    Check(browser.Query("",GameMode::Pve,"en-US").size()==1,"pve variant is not flattened");
    Fixture(temp);
    { std::ofstream more(temp/"hideout_item_requirements.tsv",std::ios::app);
      for(int i=0;i<40;++i) more<<"regular\ts-2\tmaterial"<<i<<"\t1\n"; }
    HideoutPage many; many.Initialize(temp,items,economy); many.Prepare(1100,5000,theme);
    Check(many.VisibleImages().size()<=32,"initial image requests bounded");
    many.MouseDown(left+26+136+20,230); many.MouseUp(left+26+136+20,230);
    for(int i=0;i<60;++i) many.Tick(.016F);
    many.MouseDown(left+125,385); many.MouseUp(left+125,385);
    for(int i=0;i<60;++i) many.Tick(.016F);
    Check(many.ExpandedId()=="s-2" && many.VisibleImages().size()==32,"large detail list still caps image requests at 32");
    HorizontalCardStrip rail; rail.Layout(D2D1::RectF(0,0,300,142),26);
    Check(!rail.CanMove(-1) && rail.CanMove(1),"rail disables left arrow at start");
    TabSelectionAnimation selection;selection.Select(0,4);selection.Select(3,4);selection.Tick(.05F);
    const float visual=selection.Position(),color=selection.Weight(0);
    Check(visual>0 && visual<3 && color<1,"tab position and color animate");
    selection.Select(1,4);Check(selection.Position()==visual && selection.Weight(0)==color,"retarget preserves current pose");
    for(int i=0;i<60;++i) selection.Tick(.016F);
    Check(!selection.Active() && selection.Position()==1 && selection.Weight(1)==1,"tab animation settles");
    const auto longBar=MakeScrollbar(D2D1::RectF(0,0,12,400),2000,0);
    const auto shortBar=MakeScrollbar(D2D1::RectF(0,0,12,400),800,0);
    const auto morph=SampleScrollbarTransition(longBar,shortBar,.5F);
    Check(morph.opacity==1 && morph.bar->thumb.bottom!=longBar->thumb.bottom,"populated scrollbar morph never fades");
    Check(SampleScrollbarTransition(longBar,{},.2F).opacity<1 && !SampleScrollbarTransition(longBar,{},1).bar,"absent scrollbar fades then disappears");
    Check(rail.Hit(20,20)==0 && !rail.Hit(130,20) && !rail.Hit(20,138),"rail hit testing excludes gaps/track");
    rail.Move(272); for(int i=0;i<60;++i) rail.Tick(.016F);
    Check(rail.Offset()>271 && rail.Hit(20,20)==2,"rail smooth horizontal scrolling");
    Check(rail.Press(299,139),"horizontal scrollbar drag capture");rail.Drag(300);rail.Release();
    Check(rail.Offset()==rail.Maximum(),"horizontal scrollbar reaches last station");
    Check(rail.CanMove(-1) && !rail.CanMove(1),"rail disables right arrow at end");
    rail.Layout(D2D1::RectF(0,0,5000,142),26);Check(rail.Offset()==0,"resize clamps rail");
    HideoutPage live; live.Initialize(assets/"data",items,economy);live.Prepare(1100,850,theme);
    Check(live.StationCount()==26,"production groups 68 levels into 26 station tabs");
    for(int i=0;i<60;++i) live.Tick(.016F);
    Check(live.StationScroll()==0,"initial layout keeps first station fully visible");
    live.Wheel(-120,left+100,230);for(int i=0;i<60;++i) live.Tick(.016F);
    Check(live.StationScroll()>0 && live.Scroll()==0,"rail wheel is independent from detail scroll");
    const auto selectedStation=live.SelectedStation();
    live.Wheel(-1200,left+100,230);
    for(int i=0;i<60;++i) { live.Tick(.016F); live.Prepare(1100,850,theme); }
    const float browsedOffset=live.StationScroll();
    Check(browsedOffset>1000,"rail can browse beyond selected station");
    Check(economy.ReplaceFromUpstreamJson(GameMode::Pvp,
        "{\"data\":{\"fleaMarket\":{\"enabled\":true},\"items\":{\""+bolt+"\":{\"id\":\""+bolt+"\",\"lastLowPrice\":19000}}}}",error),"refresh economy while browsing rail");
    live.Prepare(1100,850,theme);
    for(int i=0;i<60;++i) live.Tick(.016F);
    Check(live.StationScroll()==browsedOffset && live.SelectedStation()==selectedStation,
        "economy refresh must not pull rail back to selected station");
    Fixture(temp);Write(temp,"craft_materials","mode\tcraftId\titemId\tcount\ttool\tfunctional\nregular\tc1\titem\tnan\t0\t0\n");
    Check(!catalog.Load(temp,error),"invalid craft quantity rejected");
    Fixture(temp);Write(temp,"station_images","mode\tstationId\timageKey\nregular\ts\tstation-../bad\n");
    Check(!catalog.Load(temp,error),"unsafe station image rejected");
    Fixture(temp); Write(temp,"item_requirements","mode\tlevelId\titemId\tcount\nregular\ts-2\titem\t0\n");
    Check(!catalog.Load(temp,error),"zero quantity rejected");
    Fixture(temp); Write(temp,"levels","bad\n"); Check(!catalog.Load(temp,error) && !catalog.Ready(),"malformed atomic failure");
    Fixture(temp); std::ofstream(temp/"hideout_levels.tsv",std::ios::app)<<"regular\ts-1\ts\t1\t0\n";
    Check(!catalog.Load(temp,error),"duplicate rejected");
    Fixture(temp); std::ofstream(temp/"hideout_stations.tsv",std::ios::app)<<"regular\tx\t\xff\tBad\n";
    Check(!catalog.Load(temp,error),"invalid UTF-8 rejected");
    std::filesystem::remove(temp/"hideout_stations.tsv"); Check(!catalog.Load(temp,error),"missing no crash");
    for (const auto& e:std::filesystem::directory_iterator(temp)) std::filesystem::remove(e.path());
    std::filesystem::remove(temp);
    std::cout<<"Hideout catalog/browser/economy/UI tests passed\n";
}
