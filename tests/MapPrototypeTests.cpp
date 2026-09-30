#include "ui/FloorStack.h"
#include "ui/MapPrototypeData.h"
#include "ui/MapLayout.h"
#include "ui/MapViewport.h"
#include "ui/MapPage.h"
#include "ui/MapFilters.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<.002F;}
}
int main(){
    using namespace noven::ui;
    MapFilters filters;
    Require(filters.Allows(MapPointCategory::Task,"task-a"),"filters default to visible");
    filters.ToggleTask("task-a");Require(!filters.Allows(MapPointCategory::Task,"task-a"),"task identity filter hides target");
    Require(filters.Allows(MapPointCategory::Task,"task-b"),"task identity filter does not affect other tasks");
    filters.categories[static_cast<std::size_t>(MapPointCategory::Task)]=false;
    filters.categories[static_cast<std::size_t>(MapPointCategory::Task)]=true;
    Require(!filters.Allows(MapPointCategory::Task,"task-a"),"category changes preserve task exclusions");
    filters.ToggleTask("task-a");Require(filters.Allows(MapPointCategory::Task,"task-a"),"task toggle restores identity");
    MapFilterList list{{450,350,750,550},0};
    Require(list.Hit({470,400},14)==0,"filter row uses shared geometry");
    Require(!list.Hit({470,560},14),"clipped rows cannot be clicked");
    const auto bar=list.Bar(14);Require(bar&&bar->maximum>0,"long filters get shared scrollbar");
    list.scroll=bar->maximum;Require(list.Hit({470,520},14).has_value(),"scrolled filters remain selectable");
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
    const auto count=MapPrototype::Floors.size();
    const auto overview=MapLayout::Sample(1400,800,{},0,count),selected=MapLayout::Sample(1400,800,{},1,count);
    Require(overview.stack.width>selected.stack.width,"selection shrinks stack");
    Require(overview.stack.origin.x>selected.stack.origin.x,"selection moves stack left");
    Require(Near(selected.stack.Plate(0).anchor.x,selected.stack.Plate(3).anchor.x),"expanded floors align vertically");
    Require(MapLayout::Ease(0)==0&&MapLayout::Ease(1)==1,"transition endpoints exact");
    for(float width:{850.0F,1100.0F,1600.0F}){
        const auto layout=MapLayout::Sample(width,700,{},1,count);
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
    MapPage page;page.Prepare(1280,800,{});Require(!page.Selected(),"page starts in overview");
    Require(page.FocusInteraction(demo.id)&&page.FloorId()==demo.floorId&&page.InteractionId()==demo.id,
        "cross-floor focus uses shared ID and expands layout");
    for(int tick=0;tick<30;++tick)page.Tick(.016F);
    page.Prepare(1280,800,{});Require(!page.Animating(),"layout transition settles");
    const auto destination=page.Viewport().ToScreen(demo.coordinate);
    const auto bounds=page.Viewport().Bounds();
    Require(Near(destination.x,(bounds.left+bounds.right)/2)&&Near(destination.y,(bounds.top+bounds.bottom)/2),
        "cross-floor interaction centers viewport");
    int active=0;for(const auto& p:page.Points())if(p.floorId==page.FloorId())++active;
    Require(active==1,"only current floor marker is active");
    page.SelectFloor(MapPrototype::Floors[2].id);Require(page.Selected(),"floor switching never collapses layout");
    Require(!page.FocusInteraction("invalid"),"unknown interaction ID cannot change selection");
    page.MouseDown(page.Layout().search.left+20,100);page.MouseUp(page.Layout().search.left+20,100);
    for(const auto c:std::wstring(L"demo exit"))Require(page.Char(c),"persistent search accepts input");
    Require(page.Points().size()==1,"search filters isolated preview points");
    Require(page.Key(VK_LEFT,false)&&page.Search().Caret()==8,"search retains shared caret behavior");
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
    settle();const auto category=MapCategoryStrip{filtered.Layout().strip}.Cell(0);
    click(category);Require(!has("demo-container")&&has("demo-mine"),"category hides only matching point types");
    click(category);Require(has("demo-container"),"category restores matching point types");
    click(filtered.Layout().filters[1]);settle();Require(filtered.Panel()==MapFilterPanel::Layers,"layer flyout opens");
    click(filtered.FilterList().Row(0));Require(!filtered.Filters().grid&&filtered.Filters().geometry,"grid layer toggle independent");
    click(filtered.FilterList().Row(1));Require(!filtered.Filters().geometry,"geometry layer toggles");
    click(filtered.Layout().filters[2]);settle();Require(filtered.Panel()==MapFilterPanel::Tasks,"task flyout switches");
    click(filtered.FilterList().Row(0));Require(!has("demo-task")&&has("demo-task-two"),"task checkbox filters stable identity");
    click(filtered.FilterList().Row(1),50);Require(filtered.FloorId()=="demo-b3"&&filtered.InteractionId()=="demo-task-two",
        "task label focuses another floor without collapsing");
    Require(!filtered.Panel(),"focused task closes flyout");
    click(filtered.Layout().filters[0]);settle();const auto filterBar=filtered.FilterList().Bar(MapCategoryCount);
    Require(filterBar.has_value(),"point flyout has draggable overflow scrollbar");
    const auto scaleBefore=filtered.Viewport().Scale();
    Require(filtered.Wheel(-120,filtered.Layout().flyout.left+20,filtered.Layout().flyout.top+60),"flyout consumes wheel");
    Require(filtered.FilterList().scroll>0&&filtered.Viewport().Scale()==scaleBefore,"filter scrolling does not zoom map");
    filtered.Wheel(120,filtered.Layout().flyout.left+20,filtered.Layout().flyout.top+60);
    const auto thumb=filtered.FilterList().Bar(MapCategoryCount)->thumb;
    filtered.MouseDown((thumb.left+thumb.right)/2,thumb.top+5);filtered.MouseMove((thumb.left+thumb.right)/2,thumb.top+50);
    filtered.MouseUp((thumb.left+thumb.right)/2,thumb.top+50);
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
        Require(layout.strip.bottom<layout.viewport.top&&layout.viewport.bottom-layout.viewport.top>200,"categories preserve map priority");
        for(std::size_t i=0;i<MapCategoryCount;++i){const auto cell=MapCategoryStrip{layout.strip}.Cell(i);
            Require(cell.right<=layout.strip.right&&cell.bottom<=layout.strip.bottom,"category wrapping stays inside strip");}
        Require(layout.filters.back().bottom<layout.content.bottom,"left filter buttons fit content");}
    std::cout<<"Map prototype geometry and transform checks passed\n";
}
