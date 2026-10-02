#include "ui/FloorStack.h"
#include "ui/MapPrototypeData.h"
#include "ui/MapLayout.h"
#include "ui/MapViewport.h"
#include "ui/MapPage.h"
#include "ui/MapFilters.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <chrono>

namespace {
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<.002F;}
}
int main(int argc,char** argv){
    Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"WIC COM initializes");
    struct ComScope {~ComScope(){CoUninitialize();}} comScope;
    using namespace noven::ui;
    const auto picker=MapPicker::Sample(MapLayout::Sample(1400,500,{},0,3,17),64,17);
    const auto pickerRow=picker.Row(2);
    Require(picker.Hit(pickerRow.left+8,pickerRow.top+8,17)==2,"map picker hit uses scrolled stable row");
    Require(!picker.Hit(picker.panel.left-1,picker.panel.top+8,17)&&picker.Maximum(17)>0,"map picker clips and scrolls large catalogs");
    const MapSearchQuery markerQuery(L"  # 保险箱  "),ordinaryQuery(L" TASK ");
    Require(markerQuery.markers&&markerQuery.term==L"保险箱"&&!ordinaryQuery.markers&&ordinaryQuery.term==L"task","hash marker mode trims and folds independently from content search");
    MapFilters filters;
    Require(MapClockText(0)==L"03:00:00"&&MapClockText(0,true)==L"15:00:00","DEV clock epoch and twelve-hour pair");
    Require(MapGameSeconds(1000)-MapGameSeconds(0)==7,"DEV clock runs seven times real time");
    Require(MapClockText(86400000)==MapClockText(0)&&MapClockText(-1000)==L"02:59:53","clock wraps daily and handles negative epoch");
    const auto lootIcons=MapIconFor("drink")|MapIconFor("food");
    Require(MapDetailIcons.size()==44&&MapIconFor("toolbox")!=MapIconFor("duffle")&&MapIconFor("unknown")==0,
        "detail icons have distinct stable identities and unknown coarse fallback");
    filters.hiddenIcons=MapIconFor("drink");Require(filters.AllowsIcons(lootIcons),"one possible visible loot type retains source point");
    filters.hiddenIcons|=MapIconFor("food");Require(!filters.AllowsIcons(lootIcons)&&filters.AllowsIcons(0),"all loot types hide point but preserve coarse-only points");
    filters.hiddenIcons=0;
    Require(filters.Allows(MapPointCategory::Task,"task-a"),"filters default to visible");
    filters.ToggleTask("task-a");Require(!filters.Allows(MapPointCategory::Task,"task-a"),"task identity filter hides target");
    Require(filters.Allows(MapPointCategory::Task,"task-b"),"task identity filter does not affect other tasks");
    filters.categories[static_cast<std::size_t>(MapPointCategory::Task)]=false;
    filters.categories[static_cast<std::size_t>(MapPointCategory::Task)]=true;
    Require(!filters.Allows(MapPointCategory::Task,"task-a"),"category changes preserve task exclusions");
    filters.ToggleTask("task-a");Require(filters.Allows(MapPointCategory::Task,"task-a"),"task toggle restores identity");
    Require(Near(MapFloorOpacity(filters,"one","one"),1)&&Near(MapFloorOpacity(filters,"one","two"),.3F),"current floor opaque and others ghosted by default");
    filters.otherFloors=false;Require(MapFloorOpacity(filters,"one","two")==0&&MapFloorOpacity(filters,"one","one")==1,"other-floor switch never hides current layer");
    filters.otherFloors=true;filters.otherFloorOpacity=0;Require(MapFloorOpacity(filters,"one","two")==0,"zero opacity is invisible");
    MapOpacitySlider slider{{0,0,200,28}};
    Require(slider.Value(-100)==0&&slider.Value(500)==1&&Near(slider.Value(100),.5F),"opacity slider clamps endpoints and samples midpoint");
    MapFilterList list{{450,350,750,550},0};
    Require(list.Hit({470,400},14)==0,"filter row uses shared geometry");
    Require(!list.Hit({470,560},14),"clipped rows cannot be clicked");
    const auto bar=list.Bar(14);Require(bar&&bar->maximum>0,"long filters get shared scrollbar");
    list.scroll=bar->maximum;Require(list.Hit({470,520},14).has_value(),"scrolled filters remain selectable");
    for(float height:{600.0F,800.0F,1100.0F}) {
        const auto many=MapLayout::Sample(1400,height,{},1,16,17);
        Require(many.stack.Plate(15).anchor.y+8<=many.filters[0].top,
            "sixteen-floor selector never overlaps filter hit regions");
    }
    FloorStack stack{{150,100},200,4};
    Require(MapPrototype::Floors.front().label==L"1F"&&MapPrototype::Floors.back().label==L"-3F",
        "demo floor order is deterministic");
    for(std::size_t i=0;i<4;++i){
        const auto plate=stack.Plate(i);
        const auto point=D2D1::Point2F(plate.anchor.x,plate.anchor.y-2);
        Require(stack.Hit(point)==i,"each exposed floor can be selected");
        Require(plate.Contains(point),"selected geometry contains hit point");
        const auto previewTransform=plate.PreviewTransform();
        const auto previewCorner=previewTransform.TransformPoint({0,0}),previewEnd=previewTransform.TransformPoint({1,1});
        Require(Near(previewCorner.x,plate.vertices[3].x)&&Near(previewCorner.y,plate.vertices[3].y)
            &&Near(previewEnd.x,plate.vertices[1].x)&&Near(previewEnd.y,plate.vertices[1].y),"preview rectangle maps exactly onto floor plate");
    }
    Require(!stack.Hit({0,0}),"outside stack does not select floor");
    const auto count=MapPrototype::Floors.size();
    const auto overview=MapLayout::Sample(1400,800,{},0,count),selected=MapLayout::Sample(1400,800,{},1,count);
    Require(overview.stack.width>selected.stack.width,"selection shrinks stack");
    Require(overview.stack.origin.x>selected.stack.origin.x,"selection moves stack left");
    Require(Near(selected.stack.Plate(0).anchor.x,selected.stack.Plate(3).anchor.x),"expanded floors align vertically");
    for(std::size_t activeFloor=0;activeFloor<count;++activeFloor)
        for(std::size_t i=0;i<count;++i){
            Require(selected.stack.LabelVisible(i,activeFloor)==(i==activeFloor),"expanded stack labels only selected floor");
            Require(overview.stack.LabelVisible(i,activeFloor)==(i==activeFloor),"selection transition hides other floor labels");
        }
    Require(selected.stack.LabelVisible(0,std::nullopt)&&selected.stack.LabelVisible(count-1,std::nullopt)
        &&!selected.stack.LabelVisible(1,std::nullopt),"compact overview retains endpoint labels");
    Require(overview.stack.LabelVisible(1,std::nullopt),"large overview retains intermediate floor labels");
    Require(MapLayout::Ease(0)==0&&MapLayout::Ease(1)==1,"transition endpoints exact");
    const auto hiddenSidebar=MapSidebarMotion::Sample(0),partialSidebar=MapSidebarMotion::Sample(.5F),visibleSidebar=MapSidebarMotion::Sample(1);
    Require(hiddenSidebar.opacity==0&&hiddenSidebar.offset==10&&partialSidebar.opacity>0&&partialSidebar.opacity<1
        &&partialSidebar.offset>0&&visibleSidebar.opacity==1&&visibleSidebar.offset==0,"sidebar fade/slide has exact reversible endpoints");
    Require(MapInformationCard::Bounds(MapLayout::Sample(1400,800,{},1,count)).has_value()
        &&!MapInformationCard::Bounds(MapLayout::Sample(1400,600,{},1,count)),"information card never overlaps filters in short layouts");
    for(float width:{850.0F,1100.0F,1600.0F}){
        const auto layout=MapLayout::Sample(width,700,{},1,count);
        Require(layout.viewport.right-layout.viewport.left>250,"responsive map keeps useful width");
        Require(layout.stack.Plate(0).vertices[1].x<layout.viewport.left,"stack stays left of map");
    }
    MapViewport view(MapPrototype::World);view.SetBounds({400,250,1200,750});
    MapResetButton resetButton{view.Bounds()};Require(resetButton.Hit({1170,708})&&!resetButton.Hit({1152,690}),"reset hit matches circular bottom-right control");
    const D2D1_POINT_2F map{420,310};const auto screen=view.ToScreen(map),roundTrip=view.ToMap(screen);
    Require(Near(map.x,roundTrip.x)&&Near(map.y,roundTrip.y),"map/screen round trip");
    const D2D1_POINT_2F cursor{790,480};const auto anchor=view.ToMap(cursor);view.ZoomAt(cursor,2);
    const auto after=view.ToMap(cursor);
    Require(Near(anchor.x,after.x)&&Near(anchor.y,after.y),"cursor zoom preserves map anchor");
    view.ZoomAt(cursor,1000);Require(Near(view.Scale(),view.MaximumScale()),"maximum scale clamps");
    Require(view.MaximumScale()/view.MinimumScale()>40,"detail zoom remains useful beyond overview scale");
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
    view.FocusSmooth({650,400});const auto focusStart=view.ToScreen({650,400});
    Require(view.Focusing()&&!Near(focusStart.x,800),"smooth focus does not teleport");
    view.Tick(.16F);const auto focusHalf=view.ToScreen({650,400});
    Require(std::abs(focusHalf.x-800)<std::abs(focusStart.x-800)&&view.Focusing(),"smooth focus advances toward visual center");
    view.SetBounds({420,270,1220,770});Require(view.Focusing(),"resize retains focus animation");
    view.Tick(.16F);const auto focusEnd=view.ToScreen({650,400});
    Require(!view.Focusing()&&Near(focusEnd.x,820)&&Near(focusEnd.y,520)&&Near(view.Scale(),oldScale),"focus settles at resized center without zoom");
    view.FocusSmooth({300,250});view.Tick(.08F);const auto retargetStart=view.ToScreen({300,250});
    view.FocusSmooth({500,300});Require(Near(view.ToScreen({300,250}).x,retargetStart.x),"focus retarget has no discontinuity");
    view.Pan({10,0});Require(!view.Focusing(),"manual pan cancels focus");
    view.FocusSmooth({300,250});view.ZoomAt({800,500},1);Require(!view.Focusing(),"manual zoom cancels focus");
    view.FocusSmooth({300,250});view.Fit();Require(!view.Focusing(),"reset cancels focus");
    const float resetFitScale=view.Scale();view.ZoomAt({800,500},5);view.Pan({30,20});
    const float resetFromScale=view.Scale();const auto resetFrom=view.ToScreen({400,300});view.FitSmooth();
    Require(view.Focusing()&&view.Scale()==resetFromScale&&Near(view.ToScreen({400,300}).x,resetFrom.x),"smooth reset starts without zoom or focus jump");
    view.Tick(.16F);Require(view.Scale()<resetFromScale&&view.Scale()>resetFitScale,"reset interpolates fit zoom");
    view.SetBounds({430,280,1230,780});view.Tick(.16F);
    Require(!view.Focusing()&&Near(view.Scale(),resetFitScale)&&Near(view.ToScreen({500,350}).x,830)&&Near(view.ToScreen({500,350}).y,530),"reset centers whole map and fit zoom after resize");
    view.FitSmooth();view.Pan({1,0});Require(!view.Focusing(),"manual input cancels smooth reset");
    MapPage shortcuts;shortcuts.Prepare(1280,800,{});
    Require(shortcuts.FocusInteraction(MapPrototype::Points[0].id),"shortcut test selects off-center task marker");
    for(int tick=0;tick<30;++tick)shortcuts.Tick(.016F);
    shortcuts.Prepare(1280,800,{});
    const auto shortcutBounds=shortcuts.Viewport().Bounds();
    const D2D1_POINT_2F shortcutCenter{(shortcutBounds.left+shortcutBounds.right)*.5F,(shortcutBounds.top+shortcutBounds.bottom)*.5F};
    Require(shortcuts.Wheel(600,shortcutCenter.x,shortcutCenter.y),"ordinary wheel zooms viewport");
    const float shortcutZoom=shortcuts.Viewport().Scale();
    const auto resetCenter=MapResetButton{shortcutBounds}.Center();
    shortcuts.MouseDown(resetCenter.x,resetCenter.y);shortcuts.MouseUp(resetCenter.x,resetCenter.y);
    Require(shortcuts.Viewport().Focusing()&&shortcuts.Viewport().Scale()==shortcutZoom,"reset animates without instant zoom jump");
    for(int tick=0;tick<30;++tick)shortcuts.Tick(.016F);
    MapViewport defaultView{MapPrototype::World};defaultView.SetBounds(shortcutBounds);
    const auto resetWorldCenter=shortcuts.Viewport().ToScreen({500,350});
    Require(Near(shortcuts.Viewport().Scale(),defaultView.Scale())&&Near(resetWorldCenter.x,shortcutCenter.x)&&Near(resetWorldCenter.y,shortcutCenter.y),"selected task cannot override whole-map reset center or fit scale");
    Require(shortcuts.SelectedPoint().has_value(),"reset view does not clear marker selection");
    Require(shortcuts.Key(VK_ESCAPE,false)&&!shortcuts.SelectedPoint()&&shortcuts.Selected(),"Escape deselects marker without collapsing map or requiring search focus");
    Require(!shortcuts.Key(VK_ESCAPE,false),"unfocused Escape without marker has no map action");
    Require(shortcuts.FocusInteraction(MapPrototype::Points[0].id)&&shortcuts.Key(VK_ESCAPE,false)&&!shortcuts.Viewport().Focusing(),"Escape cancels pending marker focus");
    const float floorZoom=shortcuts.Viewport().Scale();
    Require(shortcuts.Wheel(-120,shortcutCenter.x,shortcutCenter.y,true)&&shortcuts.FloorId()=="demo-b1","Ctrl-wheel down selects next floor");
    Require(shortcuts.Wheel(-240,shortcutCenter.x,shortcutCenter.y,true)&&shortcuts.FloorId()=="demo-b3","multi-notch Ctrl-wheel works during floor transition");
    Require(shortcuts.Wheel(-120,shortcutCenter.x,shortcutCenter.y,true)&&shortcuts.FloorId()=="demo-b3"&&shortcuts.Viewport().Scale()==floorZoom,"bottom boundary clamps without leaking into zoom");
    Require(shortcuts.Wheel(1200,shortcutCenter.x,shortcutCenter.y,true)&&shortcuts.FloorId()=="demo-1"&&shortcuts.Viewport().Scale()==floorZoom,"Ctrl-wheel up clamps at top and retains zoom");
    Require(!shortcuts.Wheel(-120,0,0,true)&&shortcuts.FloorId()=="demo-1","Ctrl-wheel outside map content cannot switch floors");
    const auto& demo=MapPrototype::Points[1];
    const std::array points{MapInteractionPoint{demo.id,demo.floorId,demo.type,demo.coordinate,demo.english,demo.category}};
    MapInteractionStrip strip{{400,140,1000,235}};
    Require(strip.Hit({450,190},points)==demo.id,"strip hit returns stable marker ID");
    Require(!strip.Hit({0,0},points),"outside strip has no marker identity");
    MapPage page;page.Prepare(1280,800,{});Require(!page.Selected(),"page starts in overview");
    Require(page.FocusInteraction(demo.id)&&page.FloorId()==demo.floorId&&page.InteractionId()==demo.id,
        "cross-floor focus uses shared ID and expands layout");
    Require(page.SelectedPoint()&&page.SelectedPoint()->id==demo.id&&page.SelectedPoint()->category==demo.category,
        "information uses selected marker identity and category icon");
    for(int tick=0;tick<30;++tick)page.Tick(.016F);
    page.Prepare(1280,800,{});Require(!page.Animating(),"layout transition settles");
    Require(!page.MouseMove(1100,650)&&!page.MouseMove(1101,651),"pointer movement without hover/drag does not request redraw");
    const auto destination=page.Viewport().ToScreen(demo.coordinate);
    const auto bounds=page.Viewport().Bounds();
    Require(Near(destination.x,(bounds.left+bounds.right)/2)&&Near(destination.y,(bounds.top+bounds.bottom)/2),
        "cross-floor interaction centers viewport");
    int active=0;for(const auto& p:page.Points())if(p.floorId==page.FloorId())++active;
    Require(active==1,"only current floor marker is active");
    MapPage ghost;ghost.Prepare(1280,800,{});ghost.SelectFloor("demo-1");
    const auto settleGhost=[&]{for(int i=0;i<30;++i){ghost.Tick(.016F);ghost.Prepare(1280,800,{});}};
    const auto clickGhost=[&](D2D1_RECT_F r){const float x=(r.left+r.right)*.5F,y=(r.top+r.bottom)*.5F;ghost.MouseDown(x,y);ghost.MouseUp(x,y);};
    settleGhost();const auto ghostPoints=ghost.Points();
    const auto ghostExtract=std::find_if(ghostPoints.begin(),ghostPoints.end(),[](const auto& p){return p.id=="demo-extract";});
    Require(ghostExtract!=ghostPoints.end()&&Near(ghost.MarkerOpacity(*ghostExtract),.3F),"another-floor marker stays rendered at configured opacity");
    auto ghostPosition=ghost.Viewport().ToScreen(ghostExtract->coordinate);
    ghost.MouseDown(ghostPosition.x,ghostPosition.y);ghost.MouseUp(ghostPosition.x,ghostPosition.y);
    Require(ghost.FloorId()=="demo-b1"&&ghost.InteractionId()=="demo-extract"&&ghost.Viewport().Focusing(),"ghost click switches to its floor and animates focus");
    Require(!Near(ghost.Viewport().ToScreen(ghostExtract->coordinate).x,(ghost.Viewport().Bounds().left+ghost.Viewport().Bounds().right)*.5F),"marker selection has no immediate center jump");
    settleGhost();const auto ghostCentered=ghost.Viewport().ToScreen(ghostExtract->coordinate);const auto ghostBounds=ghost.Viewport().Bounds();
    Require(Near(ghostCentered.x,(ghostBounds.left+ghostBounds.right)*.5F)&&Near(ghostCentered.y,(ghostBounds.top+ghostBounds.bottom)*.5F),"selected other-floor marker settles at visual center");
    clickGhost(ghost.Layout().filters[1]);settleGhost();
    clickGhost(ghost.FilterList().Row(2));Require(!ghost.Filters().otherFloors&&ghost.MarkerOpacity(ghostPoints.front())==0,"left layer switch hides other-floor markers");
    clickGhost(ghost.FilterList().Row(2));
    const auto opacityRow=ghost.FilterList().Row(4);const MapOpacitySlider opacitySlider{opacityRow};
    const float opacityY=(opacityRow.top+opacityRow.bottom)*.5F;
    ghost.MouseDown(opacitySlider.Left(),opacityY);ghost.MouseMove(opacitySlider.Right()+200,opacityY);ghost.MouseUp(opacitySlider.Right()+200,opacityY);
    Require(ghost.Filters().otherFloorOpacity==1,"opacity drag reaches and clamps full opacity");
    ghost.MouseDown(opacitySlider.Left(),opacityY);ghost.MouseUp(opacitySlider.Left(),opacityY);
    Require(ghost.Filters().otherFloorOpacity==0&&ghost.MarkerOpacity(ghostPoints.front())==0,"opacity zero hides ghost markers");
    clickGhost(ghost.Layout().filters[1]);settleGhost();Require(!ghost.Panel(),"close flyout before testing map hits");
    clickGhost(ghost.Layout().filters[1]);settleGhost();clickGhost(ghost.FilterList().Row(5));
    Require(ghost.Filters().satellite,"satellite style selects exclusively");clickGhost(ghost.FilterList().Row(6));
    Require(!ghost.Filters().satellite,"abstract style restores without toggling geometry");
    clickGhost(ghost.Layout().filters[1]);settleGhost();
    ghostPosition=ghost.Viewport().ToScreen(ghostPoints.front().coordinate);
    ghost.MouseDown(ghostPosition.x,ghostPosition.y);ghost.MouseUp(ghostPosition.x,ghostPosition.y);
    Require(ghost.FloorId()=="demo-b1","invisible ghost cannot switch floors");
    ghost.Prepare(1100,700,{});ghost.Prepare(1280,800,{});
    Require(ghost.Filters().otherFloors&&ghost.Filters().otherFloorOpacity==0&&ghost.FloorId()=="demo-b1","resize/navigation preparation preserves ghost settings and selected floor");
    page.SelectFloor(MapPrototype::Floors[2].id);Require(page.Selected(),"floor switching never collapses layout");
    Require(!page.SelectedPoint(),"floor switching clears marker information");
    Require(!page.FocusInteraction("invalid"),"unknown interaction ID cannot change selection");
    page.MouseDown(page.Layout().search.left+20,100);page.MouseUp(page.Layout().search.left+20,100);
    for(const auto c:std::wstring(L"#demo exit"))Require(page.Char(c),"persistent search accepts input");
    Require(page.Points().size()==1,"search filters isolated preview points");
    Require(page.Key(VK_LEFT,false)&&page.Search().Caret()==9,"search retains shared caret behavior");
    Require(page.Key(VK_RETURN,false)&&page.FloorId()==demo.floorId,"Enter focuses filtered cross-floor marker");
    Require(page.Key(VK_ESCAPE,false)&&page.Search().Text().empty(),"Escape clears prototype query");
    page.MouseDown(0,0);page.MouseUp(0,0);
    const auto markerPosition=page.Viewport().ToScreen(demo.coordinate);
    page.MouseDown(markerPosition.x,markerPosition.y);page.MouseMove(markerPosition.x+3,markerPosition.y);
    page.MouseUp(markerPosition.x+3,markerPosition.y);
    const auto unpanned=page.Viewport().ToScreen(demo.coordinate);
    Require(Near(unpanned.x,markerPosition.x),"marker click never starts a map pan");
    const auto back=page.Layout().back;
    page.MouseDown(back.left+10,back.top+10);page.MouseUp(back.left+10,back.top+10);
    Require(!page.Selected()&&page.Animating(),"back starts reverse overview transition");
    for(int tick=0;tick<30;++tick)page.Tick(.016F);
    page.Prepare(1280,800,{});
    Require(!page.Animating(),"overview transition settles");
    page.SelectFloor(demo.floorId);Require(page.Selected(),"same floor can reopen from overview");
    const auto maps=page.Layout().maps;
    const float mapX=maps.left+maps.itemWidth*1.5F;
    page.MouseDown(mapX,maps.top+10);page.MouseUp(mapX,maps.top+10);
    Require(page.MapId()==MapPrototype::Maps[1].id&&!page.Selected(),"map selector returns to overview");
    Require(page.Points().size()==1&&page.Points().front().id=="demo-b-task","map identity isolates markers");
    page.SelectMap("unknown");Require(page.MapId()==MapPrototype::Maps[1].id,"invalid map ID ignored");
    Require(page.FocusInteraction(demo.id)&&page.MapId()==demo.mapId,"focus selects matching map identity");
    MapPage filtered;filtered.Prepare(1280,800,{});filtered.SelectFloor("demo-b2");
    const auto settle=[&]{for(int tick=0;tick<30;++tick)filtered.Tick(.016F);filtered.Prepare(1280,800,{});};
    const auto click=[&](D2D1_RECT_F r,float inset=12){filtered.MouseDown(r.left+inset,r.top+12);filtered.MouseUp(r.left+inset,r.top+12);};
    const auto has=[&](std::string_view id){for(const auto& p:filtered.Points())if(p.id==id)return true;return false;};
    settle();const auto categoriesBefore=filtered.Filters().categories;
    click(filtered.Layout().strip);
    Require(filtered.Filters().categories==categoriesBefore&&!filtered.Panel(),"information strip is not a second filter control");
    Require(filtered.FocusInteraction("demo-container")&&filtered.SelectedPoint(),"visible marker has information");
    click(filtered.Layout().filters[0]);settle();const auto category=filtered.FilterList().Row(0);
    click(category);Require(!has("demo-container")&&has("demo-mine"),"category hides only matching point types");
    Require(!filtered.SelectedPoint(),"hidden selected marker does not retain stale information");
    click(category);Require(has("demo-container"),"category restores matching point types");
    Require(filtered.SelectedPoint()&&filtered.SelectedPoint()->id=="demo-container","restored visible identity restores information");
    click(filtered.Layout().filters[1]);settle();Require(filtered.Panel()==MapFilterPanel::Layers,"layer flyout opens");
    click(filtered.FilterList().Row(0));Require(!filtered.Filters().grid&&filtered.Filters().geometry,"grid layer toggle independent");
    click(filtered.FilterList().Row(1));Require(!filtered.Filters().geometry,"geometry layer toggles");
    click(filtered.Layout().filters[2]);settle();Require(filtered.Panel()==MapFilterPanel::Tasks,"task flyout switches");
    click(filtered.FilterList().Row(0));Require(!has("demo-task")&&has("demo-task-two"),"task checkbox filters stable identity");
    click(filtered.FilterList().Row(1),50);Require(filtered.FloorId()=="demo-b3"&&filtered.InteractionId()=="demo-task-two",
        "task label focuses another floor without collapsing");
    Require(!filtered.Panel(),"focused task closes flyout");
    click(filtered.Layout().filters[0]);settle();const auto filterCount=MapCategoryCount+MapDetailIcons.size();
    const auto filterBar=filtered.FilterList().Bar(filterCount);
    Require(filterBar.has_value(),"point flyout has draggable overflow scrollbar");
    const auto scaleBefore=filtered.Viewport().Scale();
    Require(filtered.Wheel(-120,filtered.Layout().flyout.left+20,filtered.Layout().flyout.top+60),"flyout consumes wheel");
    Require(filtered.FilterList().scroll>0&&filtered.Viewport().Scale()==scaleBefore,"filter scrolling does not zoom map");
    filtered.Wheel(120,filtered.Layout().flyout.left+20,filtered.Layout().flyout.top+60);
    const auto thumb=filtered.FilterList().Bar(filterCount)->thumb;
    filtered.MouseDown((thumb.left+thumb.right)/2,thumb.top+5);filtered.MouseMove((thumb.left+thumb.right)/2,thumb.top+500);
    filtered.MouseUp((thumb.left+thumb.right)/2,thumb.top+500);
    Require(Near(filtered.FilterList().scroll,filterBar->maximum),"filter scrollbar drag reaches calculated maximum");
    filtered.MouseDown(30,200);filtered.MouseUp(30,200);
    Require(filtered.Panel()==MapFilterPanel::Points,"sidebar navigation press preserves filter flyout");
    filtered.Overview();settle();Require(!filtered.Panel()&&!filtered.Selected(),"overview closes filters");
    filtered.SelectFloor("demo-b2");settle();Require(!filtered.Filters().grid&&filtered.Filters().hiddenTasks.contains("demo-task"),
        "overview preserves independent filters");
    MapPage taskSearch;taskSearch.Prepare(1280,800,{});taskSearch.SelectFloor("demo-1");
    for(int tick=0;tick<30;++tick)taskSearch.Tick(.016F);taskSearch.Prepare(1280,800,{});
    taskSearch.MouseDown(taskSearch.Layout().search.left+10,100);
    for(const wchar_t c:std::wstring(L"task two"))taskSearch.Char(c);
    const auto tasksButton=taskSearch.Layout().filters[2];
    taskSearch.MouseDown(tasksButton.left+10,tasksButton.top+10);taskSearch.MouseUp(tasksButton.left+10,tasksButton.top+10);
    for(int tick=0;tick<30;++tick)taskSearch.Tick(.016F);
    const auto taskRow=taskSearch.FilterList().Row(0);
    taskSearch.MouseDown(taskRow.left+50,taskRow.top+12);taskSearch.MouseUp(taskRow.left+50,taskRow.top+12);
    Require(taskSearch.InteractionId()=="demo-task-two"&&taskSearch.Points().size()==1,
        "task flyout follows search and focuses a visible matching identity");
    for(float width:{850.0F,1100.0F,1600.0F}){const auto layout=MapLayout::Sample(width,700,{},1,count);
        Require(layout.strip.bottom<layout.viewport.top&&layout.viewport.bottom-layout.viewport.top>200,"information preserves map priority");
        Require(Near(layout.strip.bottom-layout.strip.top,100)&&Near(layout.strip.right,layout.viewport.right),
            "information strip remains compact and aligned across widths");
        Require(layout.filters.back().bottom<layout.content.bottom,"left filter buttons fit content");}
    Require(argc==2,"assets directory required");
    MapPage actual;std::wstring error;
    Require(UiLocalization().DiscoverLocales(std::filesystem::path(argv[1])/"i18n",error),"real point labels load bilingual resources");
    Require(actual.Initialize(std::filesystem::path(argv[1]),error)&&actual.RealData(),"real local Interchange binds with all three images");
    actual.Prepare(1400,850,{});
    Require(actual.MapId()=="5714dbc024597771384a510d"&&actual.Points().size()==1634,"production has all positioned map point identities");
    Require(actual.Information()&&actual.Information()->players=="11-15"&&actual.Information()->raidDuration==40,"map information comes from the recorded DEV catalog");
    Require(actual.Layout().stack.count==3&&!actual.Selected(),"real overview has three floors and no expanded viewport");
    const auto realPoints=actual.Points();
    noven::data::MapCatalog raw;
    Require(raw.Load(std::filesystem::path(argv[1])/"data",error),"source catalog available for identity and coordinate comparison");
    std::string cashRegister;
    for(const auto& point:realPoints){
        const auto* source=raw.Point(point.id);
        Require(source&&point.title.find(L"[missing:")==std::wstring_view::npos&&!point.title.empty(),"every point has a resolved readable title");
        if(source->kind=="container")Require(point.title!=std::wstring(source->sourceId.begin(),source->sourceId.end()),"all container titles exclude source IDs");
        if(source->kind=="spawn")Require(point.title!=std::wstring(source->sourceId.begin(),source->sourceId.end())
            &&!point.title.starts_with(L"Zone"),"all spawn titles exclude raw UUIDs and zone tokens");
        if(source->sourceId=="578f879c24597735401e6bc6"){
            Require(point.title==L"收银机","reported container ID resolves to correct Chinese type");cashRegister=point.id;
        }
    }
    Require(!cashRegister.empty()&&actual.FocusInteraction(cashRegister)&&actual.SelectedPoint()->title==L"收银机",
        "information strip uses readable container title");
    Require(UiLocalization().SetLocale("en-US")&&actual.SelectedPoint()->title==L"Cash register","locale switch updates selected title without rebinding");
    for(const auto& point:actual.Points())Require(point.title.find(L"[missing:")==std::wstring_view::npos&&!point.title.empty(),"every English point title resolves");
    Require(UiLocalization().SetLocale("zh-CN"),"Chinese locale restores");
    const auto pointSearch=actual.Layout().search;actual.MouseDown(pointSearch.left+8,pointSearch.top+8);
    for(int i=0;i<30;++i)actual.Tick(.016F);
    Require(!actual.Animating(),"focused search does not keep high-frequency map animation running");
    actual.ClockTick(0);Require(actual.ClockTick(250)&&actual.ClockTick(500),"visible reference clocks repaint when the displayed game second changes");
    MapPage quietSearch;quietSearch.Prepare(1280,800,{});quietSearch.MouseDown(quietSearch.Layout().search.left+8,100);
    quietSearch.ClockTick(0);Require(!quietSearch.ClockTick(250)&&quietSearch.ClockTick(500)&&!quietSearch.Animating(),"overview caret repaints only on phase changes without permanent animation");
    for(const auto c:std::wstring(L"#收银机"))actual.Char(c);
    const auto verifyRegisterSearch=[&]{
        const auto matches=actual.Points();
        Require(matches.size()==133,"semantic search retains 130 containers and three related task positions");
        Require(std::count_if(matches.begin(),matches.end(),[](const auto& p){return p.category==MapPointCategory::Container;})==130,
            "semantic search finds every cash register container");
        Require(std::count_if(matches.begin(),matches.end(),[](const auto& p){return p.category==MapPointCategory::Task;})==3,
            "semantic search also retains related task positions");
    };
    verifyRegisterSearch();
    Require(actual.Key(VK_ESCAPE,false),"Chinese query clears");
    for(const auto c:std::wstring(L"#Cash register"))actual.Char(c);
    verifyRegisterSearch();
    Require(actual.Key(VK_ESCAPE,false),"English query clears");
    for(const auto c:std::wstring(L"收银机"))actual.Char(c);
    Require(actual.Points().size()==3&&std::all_of(actual.Points().begin(),actual.Points().end(),[](const auto& p){return p.category==MapPointCategory::Task;}),"ordinary content search excludes icon names but includes task descriptions");
    Require(actual.Key(VK_ESCAPE,false),"content query clears");
    for(const auto c:std::wstring(L"#保险箱"))actual.Char(c);
    Require(std::none_of(realPoints.begin(),realPoints.end(),[](const auto& p){return (p.icons&MapIconFor("safe"))!=0;})
        &&actual.Points().empty(),"snapshot without safe markers returns no invented safe search results");
    Require(actual.Key(VK_ESCAPE,false),"missing type query clears");
    for(const auto c:std::wstring(L"#收银机"))actual.Char(c);verifyRegisterSearch();
    const auto warmedBuilds=actual.PointQueryBuilds();const auto queryStart=std::chrono::steady_clock::now();
    for(int i=0;i<5000;++i){actual.Points();actual.SelectedPoint();}
    Require(actual.PointQueryBuilds()==warmedBuilds,"unchanged frames and hit queries reuse one result without rescanning points");
    std::cout<<"5000 cached query/information pairs: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-queryStart).count()<<" ms\n";
    Require(actual.Key(VK_ESCAPE,false),"marker query clears");actual.Points();
    Require(actual.PointQueryBuilds()==warmedBuilds+1,"query changes invalidate visible-point cache exactly once");
    actual.MouseDown(0,0);actual.MouseUp(0,0);
    for(const auto& point:realPoints)if(point.category==MapPointCategory::Container||point.category==MapPointCategory::LooseLoot||point.category==MapPointCategory::Task)
        Require(point.icons!=0,"all production containers, loot and tasks carry detailed icon identities");
    const auto categoryCount=[&](MapPointCategory category){return static_cast<std::size_t>(std::count_if(
        realPoints.begin(),realPoints.end(),[&](const auto& point){return point.category==category;}));};
    Require(categoryCount(MapPointCategory::LooseLoot)==483&&categoryCount(MapPointCategory::Lock)==28
        &&categoryCount(MapPointCategory::Switch)==6&&categoryCount(MapPointCategory::StationaryWeapon)==2
        &&categoryCount(MapPointCategory::Task)==45,"production point kinds retain independent marker categories");
    const auto linkedPoint=std::find_if(realPoints.begin(),realPoints.end(),[](const auto& p){return p.category==MapPointCategory::Task;});
    Require(actual.OpenInteraction(linkedPoint->id)&&actual.SelectedPoint()&&actual.Search().Text().empty(),"exact external task navigation reveals target independent of stale search");
    const auto selectedLink=actual.InteractionId();Require(!actual.OpenInteraction("missing")&&actual.InteractionId()==selectedLink,"invalid external target leaves existing map state unchanged");
    for(const auto floor:{"Ground_Level","First_Floor","Second_Floor"}){
        const auto point=std::find_if(realPoints.begin(),realPoints.end(),[&](const auto& p){return p.floorId==floor;});
        Require(point!=realPoints.end()&&actual.FocusInteraction(point->id),"each real floor contains focusable source points");
        Require(actual.FloorId()==floor&&actual.SelectedPoint()->id==point->id,"real focus selects correct floor and retains source ID");
    }
    actual.Overview();actual.SelectFloor("First_Floor");
    Require(actual.Selected()&&actual.FloorId()=="First_Floor","real overview back retains floor selection contract");
    const auto query=actual.Layout().search;actual.MouseDown(query.left+8,query.top+8);
    for(const auto c:std::wstring(L"1F"))actual.Char(c);
    Require(actual.Key(VK_RETURN,false)&&actual.FloorId()=="First_Floor"&&actual.Points().size()==708,
        "real floor search retains floor markers even without floor names in API titles");
    for(const auto& point:actual.Points())Require(point.floorId=="First_Floor","floor query excludes other-floor markers");
    LocalImage image;
    MapIconImages officialIcons;
    Require(!officialIcons.Load(std::filesystem::path(argv[1])/L"missing")&&!officialIcons.Ready(),"missing local DEV icon set rejects");
    Require(officialIcons.Load(std::filesystem::path(argv[1])/L"maps"/L"icons")&&officialIcons.Ready(),
        "all 44 detail and 19 category DEV icon bindings decode offline");
    Require(!image.Load(std::filesystem::path(argv[1])/L"maps"/L"missing.png")&&!image.Ready(),"missing local image rejects without invented background");
    Require(image.Load(std::filesystem::path(argv[1])/L"maps"/L"interchange"/L"First_Floor.png")&&image.Ready(),"full-size local PNG decodes");
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    Require(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()))
        &&SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"native image test factories initialize");
    Microsoft::WRL::ComPtr<IDWriteFactory> textFactory;Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFormat;
    Require(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(textFactory.GetAddressOf())))
        &&SUCCEEDED(textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,12,L"en-US",&iconFormat)),"icon canvas text resources create");
    for(int targetIndex=0;targetIndex<3;++targetIndex){
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        Require(SUCCEEDED(wic->CreateBitmap(64,64,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap))
            &&SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target)),"independent image target creates");
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBrush;
        Require(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White),&iconBrush)),"icon brush creates");
        const UiCanvas iconCanvas{*target.Get(),*iconBrush.Get(),*iconFormat.Get(),*iconFormat.Get(),*iconFormat.Get(),*iconFormat.Get(),*iconFormat.Get()};
        target->BeginDraw();
        for(std::size_t i=0;i<MapDetailIcons.size();++i)officialIcons.Draw(iconCanvas,{},MapDetailIcons[i].category,MapIconMask{1}<<i,{32,32},true);
        for(std::size_t i=0;i<MapCategoryCount;++i)officialIcons.Draw(iconCanvas,{},static_cast<MapPointCategory>(i),0,{32,32});
        Require(SUCCEEDED(target->EndDraw()),"official marker icons survive independent render-target recreation");
        target->BeginDraw();Require(image.Draw(*target.Get(),{0,0,64,64}),"local image creates target-owned bitmap");
        Require(SUCCEEDED(target->EndDraw()),"target recreation retains valid image rendering");
        const std::array<const LocalImage*,3> thumbnails{&image,&image,&image};
        const std::array<std::wstring_view,3> floorLabels{L"2F",L"1F",L"B1"};
        const auto parentTransform=D2D1::Matrix3x2F::Translation(2,3);target->SetTransform(parentTransform);
        target->BeginDraw();FloorStack{{12,4},30,3,0}.Draw(iconCanvas,{},floorLabels,1,std::nullopt,thumbnails,{},.5F);
        MapResetButton{{0,0,64,64}}.Draw(iconCanvas,{});
        MapInformationCard{{0,0,124,164}}.Draw(iconCanvas,{},L"11-15",40,0);
        Require(SUCCEEDED(target->EndDraw()),"textured floor previews and vector reset render on recreated targets");
        D2D1_MATRIX_3X2_F restored;target->GetTransform(&restored);
        Require(restored._31==parentTransform._31&&restored._32==parentTransform._32&&restored._11==1,"thumbnail restores inherited page transform");
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->BeginDraw();Require(image.Draw(*target.Get(),{-2000,-2000,6192,4880},1,D2D1_RECT_F{0,0,64,64}),
            "zoomed local image renders visible native-resolution tiles");
        Require(SUCCEEDED(target->EndDraw()),"high-resolution tiles survive target recreation");
        Require(image.DetailTileCount()>0&&image.DetailTileCount()<=64,"detail cache is populated and bounded");
        image.ReleaseDetailCache();Require(image.DetailTileCount()==0&&image.Ready(),"detail release preserves loaded preview and metadata");
        target->BeginDraw();Require(image.Draw(*target.Get(),{-2000,-2000,6192,4880},1,D2D1_RECT_F{0,0,64,64}),"released detail tiles reload from local pack on demand");
        Require(SUCCEEDED(target->EndDraw()),"detail reload retains native target validity");
        if(targetIndex==0){
            for(int pass=0;pass<2;++pass){
                const auto start=std::chrono::steady_clock::now();
                for(int step=0;step<8;++step){
                    const float x=-static_cast<float>(step*512);
                    target->BeginDraw();
                    Require(image.Draw(*target.Get(),{x,-2000,x+8192,4880},1,D2D1_RECT_F{0,0,64,64}),"pan across detail tiles renders");
                    Require(SUCCEEDED(target->EndDraw()),"detail pan completes");
                }
                std::cout<<"Detail pan "<<(pass==0?"cold":"cached")<<" (8 views): "
                    <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
            }
        }
    }
    MapPage missing;
    Require(!missing.Initialize(std::filesystem::path(argv[1])/L"missing",error)&&!missing.RealData(),"missing production data rejects");
    std::cout<<"Map prototype and real Interchange geometry, transforms, identity and state checks passed\n";
}
