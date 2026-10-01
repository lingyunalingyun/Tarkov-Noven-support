#include "ui/MapPage.h"
#include "ui/FloorConnector.h"
#include "ui/PageComponents.h"
#include "ui/NavigationButton.h"
#include "ui/Dropdown.h"
#include "data/InterchangeReference.h"
#include <cwctype>

namespace noven::ui {
namespace {
std::wstring Fold(std::wstring_view text){std::wstring value(text);
    for(auto& c:value)c=static_cast<wchar_t>(std::towlower(c));return value;}
bool Matches(std::wstring_view value,std::wstring_view query){return Fold(value).find(Fold(query))!=std::wstring::npos;}
bool InternalName(std::wstring_view name){
    const auto hex=[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F');};
    if(name.size()==24&&std::all_of(name.begin(),name.end(),hex))return true;
    if(name.size()==36){
        for(std::size_t i=0;i<name.size();++i)
            if(i==8||i==13||i==18||i==23){if(name[i]!=L'-')return false;}
            else if(!hex(name[i]))return false;
        return true;
    }
    return name.starts_with(L"Zone")||name.starts_with(L"[missing:");
}
constexpr std::array<std::string_view,MapCategoryCount> CategoryKeys{TextKey::MapContainers,"map.category.loose_loot",
    "map.category.locks","map.category.switches","map.category.stationary_weapons",TextKey::MapMines,
    "map.category.artillery",TextKey::MapBoss,TextKey::MapTasks,
    TextKey::MapPmcExtract,TextKey::MapScavExtract,TextKey::MapCoopExtract,TextKey::MapTransit,TextKey::MapHiddenExtract,
    TextKey::MapSnipers,TextKey::MapSpawns,TextKey::MapScavSpawns,TextKey::MapBtr,TextKey::MapEasterEggs};
constexpr std::array FilterKeys{TextKey::MapFilterPoints,TextKey::MapFilterLayers,TextKey::MapFilterTasks};
static_assert(CategoryKeys.size()==MapCategoryCount);
std::wstring Wide(std::string_view text){
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count);
    return result;
}
}
MapPage::MapPage(){
    for(const auto& m:MapPrototype::Maps)maps_.push_back({std::string(m.id),std::wstring(m.chinese),std::wstring(m.english)});
    for(const auto& f:MapPrototype::Floors)floors_.push_back({std::string(f.id),std::wstring(f.label)});
    for(const auto& p:MapPrototype::Points)points_.push_back({std::string(p.id),std::string(p.floorId),std::string(p.mapId),
        p.type,p.coordinate,std::wstring(p.chinese),std::wstring(p.english),p.category});
    map_id_=maps_.front().id;
}
bool MapPage::Initialize(const std::filesystem::path& assets,std::wstring& error){
    data::MapCatalog candidate;
    if(!candidate.Load(assets/L"data",error)){unavailable_=true;return false;}
    namespace reference=data::InterchangeReference;
    const auto* map=candidate.Map(reference::MapId);
    if(!map||candidate.Maps().size()!=1||map->normalizedName!="interchange"){
        error=L"Unsupported map reference";unavailable_=true;return false;}
    std::vector<LocalImage> images(reference::Floors.size());
    MapIconImages markerImages;
    if(!markerImages.Load(assets/L"maps"/L"icons")){
        error=L"Local DEV marker icons unavailable";unavailable_=true;return false;}
    for(std::size_t i=0;i<images.size();++i)
        if(!images[i].Load(assets/L"maps"/L"interchange"/(std::string(reference::Floors[i].id)+".png"))){
            error=L"Interchange background unavailable";unavailable_=true;return false;}
    // 绑定新目录清除旧目录的查询/筛选；普通页面导航不重新绑定。
    // A new catalog clears old catalog queries/filters; ordinary navigation never rebinds.
    Overview();floor_id_={};map_id_={};interaction_id_={};progress_=0;search_.SetText(L"");filters_={};
    catalog_=std::move(candidate);maps_.clear();floors_.clear();points_.clear();images_=std::move(images);
    marker_images_=std::move(markerImages);
    map=catalog_.Map(reference::MapId);maps_.push_back({map->id,Wide(map->nameZh),Wide(map->nameEn)});
    map_id_=maps_.front().id;
    for(const auto& f:reference::Floors)floors_.push_back({std::string(f.id),std::wstring(f.label)});
    auto labels=UiLocalization();
    // 展示名称与上游身份分离；一次解析双语并由点位持有，搜索与信息区共用。
    // Resolve owned bilingual labels once, separate from source identity, for search and information.
    const auto displayName=[&](const data::MapPointRecord& point,MapPointCategory category,MapIconMask icons,std::string_view locale){
        labels.SetLocale(locale);
        const auto categoryName=labels.Get(CategoryKeys[static_cast<std::size_t>(category)]);
        if(point.kind=="container"||point.kind=="loose"){
            std::wstring result;
            for(std::size_t i=0;i<MapDetailIcons.size();++i)if(icons&(MapIconMask{1}<<i)){
                if(!result.empty())result+=L" / ";result+=labels.Get(MapDetailIcons[i].key);
            }
            return result.empty()?categoryName:result;
        }
        auto name=Wide(locale=="zh-CN"?point.nameZh:point.nameEn);
        if(name.empty()||InternalName(name))return categoryName;
        // 出生记录的 zoneName 不保证是人类可读名称；可读区域作为分类的附加信息。
        // Spawn zoneName is not guaranteed readable; retain readable locations as category context.
        return point.kind=="spawn"?categoryName+L" · "+name:name;
    };
    for(const auto& p:catalog_.Points()){
        MapPointCategory category=MapPointCategory::Container;MapMarkerType type=MapMarkerType::Point;bool shared=false;
        if(p.kind=="loose")category=MapPointCategory::LooseLoot;
        else if(p.kind=="lock")category=MapPointCategory::Lock;
        else if(p.kind=="switch")category=MapPointCategory::Switch;
        else if(p.kind=="stationary")category=MapPointCategory::StationaryWeapon;
        else if(p.kind=="task"){category=MapPointCategory::Task;type=MapMarkerType::Task;}
        else if(p.kind=="artillery")category=MapPointCategory::Artillery;
        else if(p.kind=="extract"){
            type=MapMarkerType::Extract;
            const bool cooperative=p.nameEn.find("Co-Op")!=std::string::npos;
            category=cooperative?MapPointCategory::CoopExtract:p.subtype=="scav"?MapPointCategory::ScavExtract:MapPointCategory::PmcExtract;
            shared=p.subtype=="shared"&&!cooperative;
        }else if(p.kind=="transit"){category=MapPointCategory::Transit;type=MapMarkerType::Extract;}
        else if(p.kind=="boss")category=MapPointCategory::Boss;
        else if(p.kind=="btr")category=MapPointCategory::Btr;
        else if(p.kind=="hazard")category=p.subtype=="minefield"?MapPointCategory::Mine:MapPointCategory::Sniper;
        else if(p.kind=="spawn"){
            if(p.subtype.find("\"boss\"")!=std::string::npos)category=MapPointCategory::Boss;
            else category=p.subtype.find("\"scav\"")!=std::string::npos?MapPointCategory::ScavSpawn:MapPointCategory::Spawn;
        }
        const auto xy=reference::Project(p.position);
        MapIconMask icons=0;for(const auto& icon:p.icons)icons|=MapIconFor(icon);
        points_.push_back({p.id,std::string(reference::FloorFor(p.position)),p.mapId,type,
            {static_cast<float>(xy.x),static_cast<float>(xy.y)},displayName(p,category,icons,"zh-CN"),
            displayName(p,category,icons,"en-US"),category,shared,icons});
    }
    world_={static_cast<float>(reference::Width),static_cast<float>(reference::Height)};
    viewport_=MapViewport(world_);real_=true;unavailable_=false;filters_.grid=false;error.clear();return true;
}
bool MapPage::Allows(const Point& p) const {
    return filters_.AllowsIcons(p.icons)&&(filters_.Allows(p.category,p.id)||(p.sharedExtract&&filters_.Allows(MapPointCategory::ScavExtract,p.id)));
}
std::vector<TabBarItem<std::string_view>> MapPage::MapItems() const {
    std::vector<TabBarItem<std::string_view>> items;
    const bool chinese=UiLocalization().ActiveLocale()=="zh-CN";
    for(const auto& map:maps_)items.push_back({map.id,chinese?map.chinese:map.english});
    return items;
}
void MapPage::Prepare(float width,float height,const UiTheme& theme){
    layout_=MapLayout::Sample(width,height,theme,progress_,floors_.size(),maps_.size());viewport_.SetBounds(layout_.viewport);
    if(const auto bar=FilterList().Bar(FilterEntries().size()))filter_scroll_=std::clamp(filter_scroll_,0.0F,bar->maximum);
    else filter_scroll_=0;
}
std::size_t MapPage::FloorIndex() const {
    for(std::size_t i=0;i<floors_.size();++i)if(floors_[i].id==floor_id_)return i;
    return 0;
}
float MapPage::FloorPosition() const noexcept{return floor_from_+(floor_to_-floor_from_)*MapLayout::Ease(floor_progress_);}
void MapPage::SelectMap(std::string_view id){
    const auto found=std::find_if(maps_.begin(),maps_.end(),[&](const auto& map){return map.id==id;});
    if(found==maps_.end()||map_id_==id)return;
    map_id_=found->id;Overview();interaction_id_={};viewport_.Fit();
}
void MapPage::SelectFloor(std::string_view id){
    const auto found=std::find_if(floors_.begin(),floors_.end(),[&](const auto& floor){return floor.id==id;});
    if(found==floors_.end()||(Selected()&&floor_id_==id))return;
    const bool first=!Selected();floor_from_=FloorPosition();floor_id_=found->id;
    floor_to_=static_cast<float>(FloorIndex());floor_progress_=first?1.0F:0.0F;
    if(first)floor_from_=floor_to_;
    expanded_=true;
    interaction_id_={};viewport_.StopFocus();CancelDrag();
}
bool MapPage::FocusInteraction(std::string_view id){
    const auto found=std::find_if(points_.begin(),points_.end(),[&](const auto& point){return point.id==id;});
    if(found==points_.end()||!Allows(*found))return false;
    SelectMap(found->mapId);
    SelectFloor(found->floorId);interaction_id_=found->id;viewport_.FocusSmooth(found->coordinate);return true;
}
std::vector<MapInteractionPoint> MapPage::Points() const {
    std::vector<MapInteractionPoint> result;
    const bool chinese=UiLocalization().ActiveLocale()=="zh-CN";
    const auto floor=std::find_if(floors_.begin(),floors_.end(),[&](const auto& f){return Fold(f.label)==Fold(search_.Text());});
    for(const auto& p:points_){
        if(p.mapId!=map_id_||!Allows(p))continue;
        if(floor!=floors_.end()){if(p.floorId!=floor->id)continue;}
        else if(!Matches(p.chinese,search_.Text())&&!Matches(p.english,search_.Text()))continue;
        result.push_back({p.id,p.floorId,p.type,p.coordinate,chinese?p.chinese:p.english,p.category,p.icons&~filters_.hiddenIcons});
    }
    return result;
}
std::optional<MapInteractionPoint> MapPage::SelectedPoint() const {
    // 信息区只描述当前可见楼层的选中标识，不显示已被搜索或筛选隐藏的身份。
    // Information describes the visible selected marker, never an identity hidden by search or filters.
    for(const auto& point:Points())if(point.id==interaction_id_&&point.floorId==floor_id_)return point;
    return std::nullopt;
}
std::vector<MapPage::FilterEntry> MapPage::FilterEntries() const {
    std::vector<FilterEntry> entries;if(!panel_)return entries;
    if(*panel_==MapFilterPanel::Points){
        for(std::size_t i=0;i<CategoryKeys.size();++i){
            const auto category=static_cast<MapPointCategory>(i);
            entries.push_back({Tr(CategoryKeys[i]),filters_.categories[i],{},category,{}});
            for(std::size_t j=0;j<MapDetailIcons.size();++j)if(MapDetailIcons[j].category==category){
                const auto icon=static_cast<MapDetailIcon>(j);
                entries.push_back({Tr(MapDetailIcons[j].key),(filters_.hiddenIcons&MapIconBit(icon))==0,{},category,icon});
            }
        }
    }else if(*panel_==MapFilterPanel::Layers){
        entries.push_back({Tr(TextKey::MapLayerGrid),filters_.grid,{},std::nullopt});
        entries.push_back({Tr(TextKey::MapLayerGeometry),filters_.geometry,{},std::nullopt});
        entries.push_back({Tr("map.layer.other_floors"),filters_.otherFloors,{},std::nullopt});
        entries.push_back({Tr("map.layer.other_opacity")+L" · "+std::to_wstring(static_cast<int>(std::lround(filters_.otherFloorOpacity*100)))+L"%",true,{},std::nullopt});
        entries.push_back({L"",true,{},std::nullopt});
    }else{
        const bool chinese=UiLocalization().ActiveLocale()=="zh-CN";
        for(const auto& p:points_)if(p.mapId==map_id_&&p.category==MapPointCategory::Task
            &&(Matches(p.chinese,search_.Text())||Matches(p.english,search_.Text())))
            entries.push_back({std::wstring(chinese?p.chinese:p.english),!filters_.hiddenTasks.contains(p.id),p.id,MapPointCategory::Task});
    }
    return entries;
}
void MapPage::TogglePanel(MapFilterPanel panel){
    if(panel_==panel){panel_open_=!panel_open_;return;}
    panel_=panel;panel_open_=true;panel_progress_=0;filter_scroll_=0;
}
void MapPage::DrawFilters(const UiCanvas& canvas,const UiTheme& theme) const {
    for(std::size_t i=0;i<layout_.filters.size();++i)
        DrawDropdownHeader(canvas,theme,layout_.filters[i],Tr(FilterKeys[i]),panel_open_&&panel_==static_cast<MapFilterPanel>(i),false);
    if(!panel_||panel_progress_==0)return;
    const auto list=FilterList();const auto r=list.bounds;const auto entries=FilterEntries();
    const float opacity=canvas.brush.GetOpacity();canvas.brush.SetOpacity(opacity*panel_progress_);
    canvas.target.PushAxisAlignedClip({r.left,r.top,r.left+(r.right-r.left)*MapLayout::Ease(panel_progress_),r.bottom},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    canvas.Round(r,8,theme.surface);canvas.brush.SetColor(theme.divider);
    canvas.target.DrawRoundedRectangle(D2D1::RoundedRect(r,8,8),&canvas.brush,1);
    canvas.Text(Tr(FilterKeys[static_cast<std::size_t>(*panel_)]),canvas.smallFormat,
        {r.left+12,r.top+6,r.right-8,r.top+32},theme.accent);
    canvas.target.PushAxisAlignedClip(list.Body(),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(entries.empty())canvas.Text(Tr(TextKey::MapNoResults),canvas.smallFormat,list.Body(),theme.secondaryText);
    for(std::size_t i=0;i<entries.size();++i){auto row=list.Row(i);if(entries[i].detail)row.left+=12;
        if(*panel_==MapFilterPanel::Layers&&i>=3){
            if(i==3)canvas.Text(entries[i].label,canvas.smallFormat,row,theme.secondaryText);
            else MapOpacitySlider{row}.Draw(canvas,theme,filters_.otherFloorOpacity);
            continue;
        }
        DrawMapCheck(canvas,theme,row,entries[i].label,entries[i].enabled,entries[i].category,
            entries[i].detail?MapIconBit(*entries[i].detail):0,&marker_images_);}
    DrawScrollbar(canvas,theme,{list.Bar(entries.size()),1});
    canvas.target.PopAxisAlignedClip();canvas.target.PopAxisAlignedClip();canvas.brush.SetOpacity(opacity);
}
void MapPage::Tick(float elapsed){
    const float dt=std::clamp(elapsed,0.0F,.05F);
    viewport_.Tick(dt);
    progress_=std::clamp(progress_+(Selected()?dt:-dt)/.36F,0.0F,1.0F);
    panel_progress_=std::clamp(panel_progress_+(panel_open_?dt:-dt)/.18F,0.0F,1.0F);
    floor_progress_=std::min(1.0F,floor_progress_+dt/.22F);clock_=std::fmod(clock_+dt,1.0F);
}
D2D1_RECT_F MapPage::ResetBounds() const noexcept {
    const auto r=layout_.viewport;return {std::max(r.left+8,r.right-130),r.top+10,r.right-10,r.top+42};
}
void MapPage::Draw(const UiCanvas& canvas,const UiTheme& theme) const {
    DrawPageHeader(canvas,theme,layout_.search.left,layout_.search.right,Tr(TextKey::NavMap));
    search_.Draw(canvas,theme,layout_.search,Tr(TextKey::MapSearch),clock_<.5F);
    if(unavailable_){canvas.Text(Tr(TextKey::Unavailable),canvas.body,layout_.content,theme.secondaryText);return;}
    const auto maps=MapItems();float mapIndex=0;
    for(std::size_t i=0;i<maps.size();++i)if(maps[i].id==map_id_)mapIndex=static_cast<float>(i);
    DrawTabBar<std::string_view>(canvas,theme,canvas.smallFormat,maps,layout_.maps,map_id_,std::optional<std::string_view>{},map_id_,1,mapIndex);
    canvas.target.PushAxisAlignedClip(layout_.content,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(progress_>0){
        const float reveal=MapLayout::Ease(progress_);const auto points=Points();
        const auto opacity=canvas.brush.GetOpacity();canvas.brush.SetOpacity(opacity*reveal);
        canvas.Round(layout_.strip,theme.cornerRadius,theme.surface);
        canvas.Text(Tr(TextKey::MapInteractions),canvas.smallFormat,
            {layout_.strip.left+14,layout_.strip.top+6,layout_.strip.right-8,layout_.strip.top+30},theme.secondaryText);
        const auto info=layout_.strip;
        if(const auto point=SelectedPoint()){
            marker_images_.Draw(canvas,theme,point->category,point->icons,{info.left+26,info.top+50});
            canvas.Text(point->title,canvas.smallFormat,
                {info.left+46,info.top+34,info.right-14,info.top+62},theme.primaryText);
            const auto source=std::find_if(points_.begin(),points_.end(),
                [&](const auto& p){return p.id==point->id;});
            const auto label=Tr(CategoryKeys[static_cast<std::size_t>(source->category)])
                +(source->sharedExtract?L" / Scav":L"")+L" · "+floors_[FloorIndex()].label;
            canvas.Text(label,canvas.smallFormat,{info.left+46,info.top+64,info.right-14,info.bottom-8},theme.accent);
        }else{
            const auto wrapping=canvas.smallFormat.GetWordWrapping();
            canvas.smallFormat.SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            canvas.Text(Tr(TextKey::MapInformationHint),canvas.smallFormat,
                {info.left+14,info.top+34,info.right-14,info.bottom-8},theme.secondaryText);
            canvas.smallFormat.SetWordWrapping(wrapping);
        }
        const auto r=layout_.viewport;
        const D2D1_RECT_F expanded{r.left,r.top,r.right,r.top+(r.bottom-r.top)*reveal};
        canvas.target.PushAxisAlignedClip(expanded,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        canvas.Round(r,theme.cornerRadius,theme.surface);
        const auto origin=viewport_.ToScreen({0,0}),end=viewport_.ToScreen({world_.width,world_.height});
        canvas.Fill({origin.x,origin.y,end.x,end.y},theme.background);canvas.brush.SetColor(theme.divider);
        if(real_&&filters_.geometry)images_[FloorIndex()].Draw(canvas.target,{origin.x,origin.y,end.x,end.y},opacity*reveal,expanded);
        for(float x=0;filters_.grid&&x<=world_.width;x+=100)
            canvas.target.DrawLine(viewport_.ToScreen({x,0}),viewport_.ToScreen({x,world_.height}),&canvas.brush,.5F);
        for(float y=0;filters_.grid&&y<=world_.height;y+=100)
            canvas.target.DrawLine(viewport_.ToScreen({0,y}),viewport_.ToScreen({world_.width,y}),&canvas.brush,.5F);
        if(!real_&&filters_.geometry)for(const auto block:MapPrototype::Buildings){const auto a=viewport_.ToScreen({block.left,block.top}),b=viewport_.ToScreen({block.right,block.bottom});
            canvas.Round({a.x,a.y,b.x,b.y},3,theme.selected);}
        const bool dense=points.size()>300&&viewport_.Scale()<viewport_.MinimumScale()*1.8F;
        // 先画半透明其他楼层，再画当前层；绘制与命中共用同一可见性规则。
        // Paint ghost floors before current-floor markers; rendering and hits share visibility.
        for(bool current:{false,true})for(const auto& p:points)if((p.floorId==floor_id_)==current&&MarkerOpacity(p)>0){
            const auto marker=viewport_.ToScreen(p.coordinate);
            if(marker.x<r.left-16||marker.x>r.right+16||marker.y<r.top-16||marker.y>r.bottom+16)continue;
            canvas.brush.SetOpacity(opacity*reveal*MarkerOpacity(p));
            if(dense&&p.id!=interaction_id_)canvas.Circle(marker,2.5F,theme.accent);
            else marker_images_.Draw(canvas,theme,p.category,p.icons,marker,p.id==interaction_id_);
        }
        canvas.brush.SetOpacity(opacity*reveal);
        canvas.Round(ResetBounds(),5,theme.surface);
        canvas.Text(Tr(TextKey::MapReset),canvas.smallFormat,ResetBounds(),theme.primaryText);
        canvas.Text(floors_[FloorIndex()].label,canvas.body,{r.left+14,r.top+10,r.left+70,r.top+38},theme.accent);
        if(points.empty())canvas.Text(Tr(TextKey::MapNoResults),canvas.smallFormat,
            {r.left+16,r.top+50,r.right-16,r.top+80},theme.secondaryText);
        canvas.Text(Tr(real_?TextKey::MapReference:TextKey::MapPreview),canvas.smallFormat,{r.left+14,r.bottom-30,r.right-14,r.bottom-6},theme.secondaryText);
        canvas.target.PopAxisAlignedClip();
        auto anchor=layout_.stack.Plate(0).vertices[1];const float index=FloorPosition();
        anchor.x-=index*layout_.stack.width*layout_.stack.stagger;anchor.y+=index*layout_.stack.width*.20F;
        FloorConnector{anchor,{r.left,r.top+(r.bottom-r.top)*.45F}}.Draw(canvas,theme.secondaryText,reveal);
        canvas.brush.SetOpacity(opacity);
        DrawNavigationButton(canvas,theme,layout_.back,NavigationGlyph::Back,Selected(),false,back_pressed_);
    }else{
        canvas.Text(Tr(real_?TextKey::MapReference:TextKey::MapPreview),canvas.smallFormat,
            {layout_.content.left,layout_.content.bottom-35,layout_.content.right,layout_.content.bottom},theme.secondaryText);
    }
    std::vector<std::wstring_view> labels;
    for(const auto& floor:floors_)labels.push_back(floor.label);
    layout_.stack.Draw(canvas,theme,labels,Selected()?std::optional<std::size_t>(FloorIndex()):std::nullopt,hovered_floor_);
    if(progress_>0)DrawFilters(canvas,theme);
    if(Selected()&&layout_.content.bottom-layout_.filters.back().bottom>=134){
        const auto left=layout_.filters.back().left,right=layout_.filters.back().right;
        const float top=layout_.content.bottom-128;
        canvas.Round({left,top,right,layout_.content.bottom-4},8,theme.surface);
        const auto text=[&](std::wstring_view value,int row,D2D1_COLOR_F color){
            canvas.Text(value,canvas.smallFormat,{left+8,top+4+row*19.0F,right-4,top+23+row*19.0F},color);};
        text(Tr("map.info"),0,theme.accent);
        if(const auto info=Information()){
            text(Tr("map.players")+L" · "+Wide(info->players),1,theme.primaryText);
            text(Tr("map.duration")+L" · "+std::to_wstring(info->raidDuration)+L" "+Tr("map.minutes"),2,theme.primaryText);
        }else{text(Tr(TextKey::MapPreview),1,theme.secondaryText);}
        text(Tr("map.game_time"),3,theme.secondaryText);
        const auto utc=MapUtcMilliseconds();text(MapClockText(utc),4,theme.primaryText);text(MapClockText(utc,true),5,theme.primaryText);
    }
    canvas.target.PopAxisAlignedClip();
}
std::optional<std::string_view> MapPage::MarkerAt(D2D1_POINT_2F p) const {
    if(!Selected()||!MapContains(layout_.viewport,p))return std::nullopt;
    const auto points=Points();
    for(bool current:{true,false}){
        std::optional<std::string_view> closest;float distance=14;
        for(const auto& marker:points)if((marker.floorId==floor_id_)==current&&MarkerOpacity(marker)>0){
            const auto q=viewport_.ToScreen(marker.coordinate);const float d=std::hypot(p.x-q.x,p.y-q.y);
            if(d<=distance){distance=d;closest=marker.id;}
        }
        if(closest)return closest;
    }
    return std::nullopt;
}
void MapPage::MouseMove(float x,float y){
    const D2D1_POINT_2F p{x,y};hovered_floor_=layout_.stack.Hit(p);
    if(opacity_drag_){filters_.otherFloorOpacity=MapOpacitySlider{FilterList().Row(4)}.Value(x);return;}
    if(scroll_drag_){if(const auto bar=FilterList().Bar(FilterEntries().size()))filter_scroll_=bar->OffsetFromThumbTop(y-*scroll_drag_);return;}
    if(drag_){viewport_.Pan({p.x-drag_->x,p.y-drag_->y});drag_=p;}
}
void MapPage::MouseDown(float x,float y){
    const D2D1_POINT_2F p{x,y};CancelDrag();
    if(MapContains(layout_.search,p)){search_.Focus();return;}search_.Blur();
    const auto maps=MapItems();pressed_map_=HitTestTabBar<std::string_view>(maps,layout_.maps,x,y);if(pressed_map_)return;
    // 主侧栏点击属于页面导航，不是画布的面板外点击。
    // Shell navigation clicks are not outside clicks within the canvas.
    if(!MapContains(layout_.content,p))return;
    if(Selected()&&MapContains(layout_.back,p)){back_pressed_=true;return;}
    if(Selected()&&progress_==1){
        for(std::size_t i=0;i<layout_.filters.size();++i)if(MapContains(layout_.filters[i],p)){pressed_filter_=static_cast<MapFilterPanel>(i);return;}
        if(panel_&&panel_progress_>0){
            if(MapContains(layout_.flyout,p)){
                if(panel_open_&&panel_progress_==1){const auto list=FilterList();
                    if(const auto bar=list.Bar(FilterEntries().size());bar&&MapContains(bar->track,p)){
                        const float h=bar->thumb.bottom-bar->thumb.top;
                        scroll_drag_=MapContains(bar->thumb,p)?p.y-bar->thumb.top:h*.5F;
                        filter_scroll_=bar->OffsetFromThumbTop(p.y-*scroll_drag_);return;}
                    pressed_row_=list.Hit(p,FilterEntries().size());pressed_row_check_=p.x<list.Body().left+28;
                    if(panel_==MapFilterPanel::Layers&&pressed_row_==4){
                        opacity_drag_=true;filters_.otherFloorOpacity=MapOpacitySlider{list.Row(4)}.Value(x);pressed_row_.reset();}}
                return;
            }
            panel_open_=false;
        }
    }
    pressed_floor_=layout_.stack.Hit(p);if(pressed_floor_)return;
    if(!Selected()||progress_<1)return;
    if(MapContains(ResetBounds(),p)){reset_pressed_=true;return;}
    if(const auto id=MarkerAt(p)){pressed_point_=*id;return;}
    if(MapContains(layout_.viewport,p)){viewport_.StopFocus();drag_=p;}
}
void MapPage::MouseUp(float x,float y){
    const D2D1_POINT_2F p{x,y};
    if(opacity_drag_){filters_.otherFloorOpacity=MapOpacitySlider{FilterList().Row(4)}.Value(x);CancelDrag();return;}
    const auto maps=MapItems();
    if(pressed_map_&&HitTestTabBar<std::string_view>(maps,layout_.maps,x,y)==pressed_map_){SelectMap(*pressed_map_);CancelDrag();return;}
    if(back_pressed_&&MapContains(layout_.back,p)){Overview();return;}
    if(pressed_filter_&&MapContains(layout_.filters[static_cast<std::size_t>(*pressed_filter_)],p)){
        TogglePanel(*pressed_filter_);CancelDrag();return;}
    if(pressed_row_&&panel_open_&&FilterList().Hit(p,FilterEntries().size())==pressed_row_){
        const auto row=*pressed_row_;const auto entries=FilterEntries();
        if(*panel_==MapFilterPanel::Points){
            if(entries[row].detail)filters_.hiddenIcons^=MapIconBit(*entries[row].detail);
            else {const auto category=static_cast<std::size_t>(*entries[row].category);filters_.categories[category]=!filters_.categories[category];}
        }
        else if(*panel_==MapFilterPanel::Layers){
            if(row==0)filters_.grid=!filters_.grid;
            else if(row==1)filters_.geometry=!filters_.geometry;
            else if(row==2)filters_.otherFloors=!filters_.otherFloors;
        }
        else if(pressed_row_check_)filters_.ToggleTask(entries[row].pointId);
        else if(FocusInteraction(entries[row].pointId))panel_open_=false;
        CancelDrag();return;
    }
    if(pressed_floor_&&layout_.stack.Hit(p)==pressed_floor_)SelectFloor(floors_[*pressed_floor_].id);
    if(!pressed_point_.empty()){
        auto id=MarkerAt(p);
        if(id&&*id==pressed_point_)FocusInteraction(*id);
    }
    if(reset_pressed_&&MapContains(ResetBounds(),p))viewport_.Fit();CancelDrag();
}
bool MapPage::Wheel(int delta,float x,float y){
    if(panel_&&panel_progress_>0&&MapContains(layout_.flyout,{x,y})){
        if(const auto bar=FilterList().Bar(FilterEntries().size()))
            filter_scroll_=std::clamp(filter_scroll_-static_cast<float>(delta)/WHEEL_DELTA*60,0.0F,bar->maximum);
        return true;
    }
    if(!Selected()||progress_<1||!MapContains(layout_.viewport,{x,y}))return false;
    viewport_.ZoomAt({x,y},static_cast<float>(delta)/WHEEL_DELTA);return true;
}
bool MapPage::Key(WPARAM key,bool control){
    if(search_.Focused()&&key==VK_RETURN){
        for(const auto& floor:floors_)if(Fold(floor.label)==Fold(search_.Text())){SelectFloor(floor.id);return true;}
        const auto matches=Points();if(!matches.empty())FocusInteraction(matches.front().id);return true;
    }
    return search_.HandleKeyDown(key,control);
}
}
