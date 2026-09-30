#include "ui/MapPage.h"
#include "ui/FloorConnector.h"
#include "ui/PageComponents.h"
#include "ui/NavigationButton.h"
#include <cwctype>

namespace noven::ui {
namespace {
std::wstring Fold(std::wstring_view text){std::wstring value(text);
    for(auto& c:value)c=static_cast<wchar_t>(std::towlower(c));return value;}
bool Matches(std::wstring_view value,std::wstring_view query){return Fold(value).find(Fold(query))!=std::wstring::npos;}
auto MapItems(){
    std::array<TabBarItem<std::string_view>,MapPrototype::Maps.size()> items{};
    const bool chinese=UiLocalization().ActiveLocale()=="zh-CN";
    for(std::size_t i=0;i<items.size();++i){const auto& map=MapPrototype::Maps[i];
        items[i]={map.id,chinese?map.chinese:map.english};}
    return items;
}
}
void MapPage::Prepare(float width,float height,const UiTheme& theme){
    layout_=MapLayout::Sample(width,height,theme,progress_,MapPrototype::Floors.size());viewport_.SetBounds(layout_.viewport);
}
std::size_t MapPage::FloorIndex() const {
    for(std::size_t i=0;i<MapPrototype::Floors.size();++i)if(MapPrototype::Floors[i].id==floor_id_)return i;
    return 0;
}
float MapPage::FloorPosition() const noexcept{return floor_from_+(floor_to_-floor_from_)*MapLayout::Ease(floor_progress_);}
void MapPage::SelectMap(std::string_view id){
    const auto found=std::find_if(MapPrototype::Maps.begin(),MapPrototype::Maps.end(),[&](const auto& map){return map.id==id;});
    if(found==MapPrototype::Maps.end()||map_id_==id)return;
    map_id_=found->id;Overview();interaction_id_={};viewport_.Fit();
}
void MapPage::SelectFloor(std::string_view id){
    const auto found=std::find_if(MapPrototype::Floors.begin(),MapPrototype::Floors.end(),[&](const auto& floor){return floor.id==id;});
    if(found==MapPrototype::Floors.end()||(Selected()&&floor_id_==id))return;
    const bool first=!Selected();floor_from_=FloorPosition();floor_id_=found->id;
    floor_to_=static_cast<float>(FloorIndex());floor_progress_=first?1.0F:0.0F;
    if(first)floor_from_=floor_to_;
    expanded_=true;
    interaction_id_={};CancelDrag();
}
bool MapPage::FocusInteraction(std::string_view id){
    const auto found=std::find_if(MapPrototype::Points.begin(),MapPrototype::Points.end(),[&](const auto& point){return point.id==id;});
    if(found==MapPrototype::Points.end())return false;
    SelectMap(found->mapId);
    SelectFloor(found->floorId);interaction_id_=found->id;viewport_.Focus(found->coordinate);return true;
}
std::vector<MapInteractionPoint> MapPage::Points() const {
    std::vector<MapInteractionPoint> result;
    const bool chinese=UiLocalization().ActiveLocale()=="zh-CN";
    for(const auto& p:MapPrototype::Points){
        if(p.mapId!=map_id_)continue;
        if(!Matches(p.chinese,search_.Text())&&!Matches(p.english,search_.Text()))continue;
        result.push_back({p.id,p.floorId,p.type,p.coordinate,chinese?p.chinese:p.english});
    }
    return result;
}
void MapPage::Tick(float elapsed){
    const float dt=std::clamp(elapsed,0.0F,.05F);
    progress_=std::clamp(progress_+(Selected()?dt:-dt)/.36F,0.0F,1.0F);
    floor_progress_=std::min(1.0F,floor_progress_+dt/.22F);clock_=std::fmod(clock_+dt,1.0F);
}
D2D1_RECT_F MapPage::ResetBounds() const noexcept {
    const auto r=layout_.viewport;return {std::max(r.left+8,r.right-130),r.top+10,r.right-10,r.top+42};
}
void MapPage::Draw(const UiCanvas& canvas,const UiTheme& theme) const {
    DrawPageHeader(canvas,theme,layout_.search.left,layout_.search.right,Tr(TextKey::NavMap));
    search_.Draw(canvas,theme,layout_.search,Tr(TextKey::MapSearch),clock_<.5F);
    const auto maps=MapItems();float mapIndex=0;
    for(std::size_t i=0;i<maps.size();++i)if(maps[i].id==map_id_)mapIndex=static_cast<float>(i);
    DrawTabBar(canvas,theme,canvas.smallFormat,maps,layout_.maps,map_id_,std::optional<std::string_view>{},map_id_,1,mapIndex);
    canvas.target.PushAxisAlignedClip(layout_.content,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(progress_>0){
        const float reveal=MapLayout::Ease(progress_);const auto points=Points();
        const auto opacity=canvas.brush.GetOpacity();canvas.brush.SetOpacity(opacity*reveal);
        MapInteractionStrip{layout_.strip}.Draw(canvas,theme,Tr(TextKey::MapInteractions),points,interaction_id_);
        if(points.empty())canvas.Text(Tr(TextKey::MapNoResults),canvas.smallFormat,
            {layout_.strip.left+16,layout_.strip.top+38,layout_.strip.right-16,layout_.strip.bottom},theme.secondaryText);
        const auto r=layout_.viewport;
        const D2D1_RECT_F expanded{r.left,r.top,r.right,r.top+(r.bottom-r.top)*reveal};
        canvas.target.PushAxisAlignedClip(expanded,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        canvas.Round(r,theme.cornerRadius,theme.surface);
        const auto origin=viewport_.ToScreen({0,0}),end=viewport_.ToScreen({MapPrototype::World.width,MapPrototype::World.height});
        canvas.Fill({origin.x,origin.y,end.x,end.y},theme.background);canvas.brush.SetColor(theme.divider);
        for(float x=0;x<=MapPrototype::World.width;x+=100)
            canvas.target.DrawLine(viewport_.ToScreen({x,0}),viewport_.ToScreen({x,MapPrototype::World.height}),&canvas.brush,.5F);
        for(float y=0;y<=MapPrototype::World.height;y+=100)
            canvas.target.DrawLine(viewport_.ToScreen({0,y}),viewport_.ToScreen({MapPrototype::World.width,y}),&canvas.brush,.5F);
        for(const auto block:MapPrototype::Buildings){const auto a=viewport_.ToScreen({block.left,block.top}),b=viewport_.ToScreen({block.right,block.bottom});
            canvas.Round({a.x,a.y,b.x,b.y},3,theme.selected);}
        for(const auto& p:points)if(p.floorId==floor_id_)
            DrawMapMarker(canvas,theme,p.type,viewport_.ToScreen(p.coordinate),p.id==interaction_id_);
        canvas.Round(ResetBounds(),5,theme.surface);
        canvas.Text(Tr(TextKey::MapReset),canvas.smallFormat,ResetBounds(),theme.primaryText);
        canvas.Text(MapPrototype::Floors[FloorIndex()].label,canvas.body,{r.left+14,r.top+10,r.left+70,r.top+38},theme.accent);
        canvas.Text(Tr(TextKey::MapPreview),canvas.smallFormat,{r.left+14,r.bottom-30,r.right-14,r.bottom-6},theme.secondaryText);
        canvas.target.PopAxisAlignedClip();
        auto anchor=layout_.stack.Plate(0).anchor;const float index=FloorPosition();
        anchor.x-=index*layout_.stack.width*layout_.stack.stagger;anchor.y+=index*layout_.stack.width*.20F;
        FloorConnector{anchor,{r.left,r.top+(r.bottom-r.top)*.45F}}.Draw(canvas,theme.secondaryText,reveal);
        canvas.brush.SetOpacity(opacity);
        DrawNavigationButton(canvas,theme,layout_.back,NavigationGlyph::Back,Selected(),false,back_pressed_);
    }else{
        canvas.Text(Tr(TextKey::MapPreview),canvas.smallFormat,
            {layout_.content.left,layout_.content.bottom-35,layout_.content.right,layout_.content.bottom},theme.secondaryText);
    }
    std::array<std::wstring_view,MapPrototype::Floors.size()> labels{};
    for(std::size_t i=0;i<labels.size();++i)labels[i]=MapPrototype::Floors[i].label;
    layout_.stack.Draw(canvas,theme,labels,Selected()?std::optional<std::size_t>(FloorIndex()):std::nullopt,hovered_floor_);
    canvas.target.PopAxisAlignedClip();
}
std::optional<std::string_view> MapPage::MarkerAt(D2D1_POINT_2F p) const {
    if(!Selected()||!MapContains(layout_.viewport,p))return std::nullopt;
    for(const auto& marker:Points())if(marker.floorId==floor_id_){const auto q=viewport_.ToScreen(marker.coordinate);
        if(std::hypot(p.x-q.x,p.y-q.y)<=14)return marker.id;}
    return std::nullopt;
}
void MapPage::MouseMove(float x,float y){
    const D2D1_POINT_2F p{x,y};hovered_floor_=layout_.stack.Hit(p);
    if(drag_){viewport_.Pan({p.x-drag_->x,p.y-drag_->y});drag_=p;}
}
void MapPage::MouseDown(float x,float y){
    const D2D1_POINT_2F p{x,y};CancelDrag();
    if(MapContains(layout_.search,p)){search_.Focus();return;}search_.Blur();
    pressed_map_=HitTestTabBar(MapItems(),layout_.maps,x,y);if(pressed_map_)return;
    if(Selected()&&MapContains(layout_.back,p)){back_pressed_=true;return;}
    pressed_floor_=layout_.stack.Hit(p);if(pressed_floor_)return;
    if(!Selected()||progress_<1)return;
    if(const auto id=MapInteractionStrip{layout_.strip}.Hit(p,Points())){pressed_point_=*id;return;}
    if(MapContains(ResetBounds(),p)){reset_pressed_=true;return;}
    if(const auto id=MarkerAt(p)){pressed_point_=*id;return;}
    if(MapContains(layout_.viewport,p))drag_=p;
}
void MapPage::MouseUp(float x,float y){
    const D2D1_POINT_2F p{x,y};
    if(pressed_map_&&HitTestTabBar(MapItems(),layout_.maps,x,y)==pressed_map_){SelectMap(*pressed_map_);CancelDrag();return;}
    if(back_pressed_&&MapContains(layout_.back,p)){Overview();return;}
    if(pressed_floor_&&layout_.stack.Hit(p)==pressed_floor_)SelectFloor(MapPrototype::Floors[*pressed_floor_].id);
    if(!pressed_point_.empty()){
        auto id=MapInteractionStrip{layout_.strip}.Hit(p,Points());if(!id)id=MarkerAt(p);
        if(id&&*id==pressed_point_)FocusInteraction(*id);
    }
    if(reset_pressed_&&MapContains(ResetBounds(),p))viewport_.Fit();CancelDrag();
}
bool MapPage::Wheel(int delta,float x,float y){
    if(!Selected()||progress_<1||!MapContains(layout_.viewport,{x,y}))return false;
    viewport_.ZoomAt({x,y},static_cast<float>(delta)/WHEEL_DELTA);return true;
}
bool MapPage::Key(WPARAM key,bool control){
    if(search_.Focused()&&key==VK_RETURN){
        for(const auto& floor:MapPrototype::Floors)if(Fold(floor.label)==Fold(search_.Text())){SelectFloor(floor.id);return true;}
        const auto matches=Points();if(!matches.empty())FocusInteraction(matches.front().id);return true;
    }
    return search_.HandleKeyDown(key,control);
}
}
