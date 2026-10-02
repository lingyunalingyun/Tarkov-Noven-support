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
    TextKey::MapSnipers,TextKey::MapSpawns,TextKey::MapScavSpawns,TextKey::MapBtr,TextKey::MapEasterEggs,"map.category.unknown_extract"};
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
    for(auto& p:points_){p.searchChinese=Fold(p.chinese);p.searchEnglish=Fold(p.english);}
}
bool MapPage::Initialize(const std::filesystem::path& assets,std::wstring& error){
    data::MapCatalog candidate;
    if(!candidate.Load(assets/L"data",error)){unavailable_=true;return false;}
    namespace reference=data::InterchangeReference;
    const auto* map=candidate.Map(reference::MapId);
    const bool generic=std::any_of(candidate.Maps().begin(),candidate.Maps().end(),[](const auto& m){return !m.floors.empty();});
    if(!generic&&(!map||candidate.Maps().size()!=1||map->normalizedName!="interchange")){
        error=L"Unsupported map reference";unavailable_=true;return false;}
    std::vector<LocalImage> images(reference::Floors.size());
    std::vector<LocalImage> upperImages(reference::Floors.size());LocalImage satellite;
    if(!generic&&!satellite.Load(assets/L"maps"/L"interchange"/L"Satellite.png")){
        error=L"Satellite background unavailable";unavailable_=true;return false;}
    for(std::size_t i=0;!generic&&i<2;++i)if(!upperImages[i].Load(assets/L"maps"/L"interchange"/(std::string(reference::Floors[i].id)+".overlay.png"))){
        error=L"Upper floor overlay unavailable";unavailable_=true;return false;}
    MapIconImages markerImages;
    if(!markerImages.Load(assets/L"maps"/L"icons")){
        error=L"Local DEV marker icons unavailable";unavailable_=true;return false;}
    for(std::size_t i=0;!generic&&i<images.size();++i)
        if(!images[i].Load(assets/L"maps"/L"interchange"/(std::string(reference::Floors[i].id)+".png"))){
            error=L"Interchange background unavailable";unavailable_=true;return false;}
    // 绑定新目录清除旧目录的查询/筛选；普通页面导航不重新绑定。
    // A new catalog clears old catalog queries/filters; ordinary navigation never rebinds.
    Overview();floor_id_={};map_id_={};interaction_id_={};progress_=0;search_.SetText(L"");filters_={};
    point_cache_valid_=false;visible_points_.clear();
    catalog_=std::move(candidate);maps_.clear();floors_.clear();points_.clear();images_=std::move(images);
    assets_=assets;generic_=generic;mode_=data::GameMode::Pvp;mode_switch_.Reset(false);
    if(std::filesystem::exists(assets/L"data"/L"pve"/L"map_maps.tsv")){
        if(!pve_catalog_.Load(assets/L"data"/L"pve",error)){unavailable_=true;return false;}
    }
    marker_images_=std::move(markerImages);
    satellite_=std::move(satellite);upper_images_=std::move(upperImages);
    for(const auto& entry:Catalog().Maps())maps_.push_back({entry.id,Wide(entry.nameZh),Wide(entry.nameEn)});
    map_id_=maps_.front().id;
    if(Catalog().Map(reference::MapId))map_id_=reference::MapId;
    for(const auto& f:reference::Floors)floors_.push_back({std::string(f.id),std::wstring(f.label)});
    BindPoints();
    if(generic_)BindMapImages();
    else world_={static_cast<float>(reference::Width),static_cast<float>(reference::Height)};
    viewport_=MapViewport(world_);real_=true;unavailable_=false;filters_.grid=false;error.clear();return true;
}
void MapPage::BindPoints(){
    namespace reference=data::InterchangeReference;
    points_.clear();point_cache_valid_=false;visible_points_.clear();visible_outlines_.clear();
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
    for(const auto& p:Catalog().Points()){
        const auto* map=Catalog().Map(p.mapId);
        if(generic_&&(!map||map->floors.empty()))continue;
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
            category=p.subtype=="unknown"?MapPointCategory::UnknownExtract:cooperative?MapPointCategory::CoopExtract:p.subtype=="scav"?MapPointCategory::ScavExtract:MapPointCategory::PmcExtract;
            shared=p.subtype=="shared"&&!cooperative;
        }else if(p.kind=="transit"){category=MapPointCategory::Transit;type=MapMarkerType::Extract;}
        else if(p.kind=="boss")category=MapPointCategory::Boss;
        else if(p.kind=="btr")category=MapPointCategory::Btr;
        else if(p.kind=="hazard")category=p.subtype=="minefield"?MapPointCategory::Mine:MapPointCategory::Sniper;
        else if(p.kind=="spawn"){
            if(p.subtype.find("\"boss\"")!=std::string::npos)category=MapPointCategory::Boss;
            else category=p.subtype.find("\"scav\"")!=std::string::npos?MapPointCategory::ScavSpawn:MapPointCategory::Spawn;
        }
        const auto projected=generic_?map->projection.Project(p.position):std::array<double,2>{reference::Project(p.position).x,reference::Project(p.position).y};
        MapIconMask icons=0;for(const auto& icon:p.icons)icons|=MapIconFor(icon);
        const auto floor=generic_?data::MapFloorFor(p.position,map->extents,map->baseFloor):reference::FloorFor(p.position);
        points_.push_back({p.id,std::string(floor),p.mapId,type,
            {static_cast<float>(projected[0]),static_cast<float>(projected[1])},displayName(p,category,icons,"zh-CN"),
            displayName(p,category,icons,"en-US"),category,shared,icons});
        auto& point=points_.back();
        for(const auto& vertex:p.outline){
            const auto xy=generic_?map->projection.Project(vertex):std::array<double,2>{reference::Project(vertex).x,reference::Project(vertex).y};
            point.outline.push_back({static_cast<float>(xy[0]),static_cast<float>(xy[1])});
        }
        for(const auto& condition:p.conditions){
            const bool present=condition.value!="false"&&condition.value!="null"&&condition.value!="[]"&&condition.value!="\"\"";
            if(condition.field=="switch"||condition.field=="switches")point.switchRequired|=present;
            if(condition.field=="transferItem")point.paymentRequired|=present;
        }
    }
    for(auto& p:points_){p.searchChinese=Fold(p.chinese);p.searchEnglish=Fold(p.english);}
}
void MapPage::BindMapImages(){
    ReleaseDetailImages();floors_.clear();images_.clear();upper_images_.clear();satellite_=LocalImage{};
    floor_id_={};previous_floor_=0;floor_from_=floor_to_=0;floor_progress_=1;
    const auto* map=Information();missing_reference_=!map||map->floors.empty();
    if(missing_reference_)return;
    world_={static_cast<float>(map->projection.width),static_cast<float>(map->projection.height)};
    for(const auto& floor:map->floors){
        floors_.push_back({floor.id,Wide(UiLocalization().ActiveLocale()=="zh-CN"?floor.nameZh:floor.nameEn)});
    }
    ReloadFloorImages();
    if(std::none_of(upper_images_.begin(),upper_images_.end(),[](const auto& image){return image.Ready();}))filters_.satellite=false;
    viewport_=MapViewport(world_);viewport_.SetBounds(layout_.viewport);
}
void MapPage::ReloadFloorImages(){
    const auto* map=Information();if(!map)return;
    images_.clear();upper_images_.clear();
    const auto load=[&](const std::string& name){
        LocalImage image;if(name.empty())return image;
        const auto path=std::filesystem::path(std::u8string(name.begin(),name.end()));
        // 整代缓存只含已验证的衍生文件；坏缓存独立回退，不重绑页面或导航状态。
        // Published generations contain verified products; invalid cache falls back without rebinding page/navigation state.
        if(!asset_generation_.empty()&&image.Load(asset_generation_/path))return image;
        image=LocalImage{};image.Load(assets_/path);return image;
    };
    for(const auto& floor:map->floors){images_.push_back(load(floor.abstractPath));upper_images_.push_back(load(floor.satellitePath));}
}
void MapPage::SetAssetGeneration(std::filesystem::path generation){
    if(asset_generation_==generation)return;
    asset_generation_=std::move(generation);
    if(real_&&generic_)ReloadFloorImages();
}
bool MapPage::SetMode(data::GameMode mode){
    if(mode==data::GameMode::Seasonal)mode=data::GameMode::Pvp;
    if(!real_||mode==mode_||!Catalog(mode).Ready()||(mode!=data::GameMode::Pvp&&mode!=data::GameMode::Pve))return false;
    const std::string oldMap(map_id_);Overview();interaction_id_={};mode_=mode;
    mode_switch_.Select(mode==data::GameMode::Pve);
    maps_.clear();for(const auto& map:Catalog().Maps())maps_.push_back({map.id,Wide(map.nameZh),Wide(map.nameEn)});
    map_id_=Catalog().Map(oldMap)?Catalog().Map(oldMap)->id:maps_.front().id;
    BindPoints();BindMapImages();return true;
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
    if(generic_)if(const auto* map=Information())for(std::size_t i=0;i<floors_.size();++i)
        floors_[i].label=Wide(UiLocalization().ActiveLocale()=="zh-CN"?map->floors[i].nameZh:map->floors[i].nameEn);
    layout_=MapLayout::Sample(width,height,theme,progress_,floors_.size(),maps_.size());viewport_.SetBounds(layout_.viewport);
    picker_scroll_=std::clamp(picker_scroll_,0.0F,Picker().Maximum(maps_.size()));
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
    map_id_=found->id;Overview();interaction_id_={};
    if(real_&&generic_)BindMapImages();else viewport_.Fit();
}
void MapPage::SelectFloor(std::string_view id){
    const auto found=std::find_if(floors_.begin(),floors_.end(),[&](const auto& floor){return floor.id==id;});
    if(found==floors_.end()||(Selected()&&floor_id_==id))return;
    const bool first=!Selected();previous_floor_=FloorIndex();floor_from_=FloorPosition();floor_id_=found->id;
    floor_to_=static_cast<float>(FloorIndex());floor_progress_=first?1.0F:0.0F;
    if(first)floor_from_=floor_to_;
    expanded_=true;
    interaction_id_={};viewport_.StopFocus();CancelDrag();
    TrimDetailImages();
}
bool MapPage::FocusInteraction(std::string_view id){
    const auto found=std::find_if(points_.begin(),points_.end(),[&](const auto& point){return point.id==id;});
    if(found==points_.end()||!Allows(*found))return false;
    SelectMap(found->mapId);
    SelectFloor(found->floorId);interaction_id_=found->id;viewport_.FocusSmooth(found->coordinate);return true;
}
bool MapPage::OpenInteraction(std::string_view id){
    const auto found=std::find_if(points_.begin(),points_.end(),[&](const auto& p){return p.id==id;});
    if(found==points_.end())return false;
    // 精确外部导航解除遮挡目标的查询/筛选，保留其他地图状态。
    // Exact external navigation reveals the target through query/filters, preserving other map state.
    search_.SetText(L"");search_.Blur();panel_open_=false;
    filters_.categories[static_cast<std::size_t>(found->category)]=true;
    filters_.hiddenTasks.erase(found->id);filters_.hiddenIcons&=~found->icons;
    return FocusInteraction(found->id);
}
const std::vector<MapInteractionPoint>& MapPage::Points() const {
    const auto& locale=UiLocalization().ActiveLocale();
    if(point_cache_valid_&&cached_query_==search_.Text()&&cached_map_==map_id_&&cached_locale_==locale
        &&cached_filters_.categories==filters_.categories&&cached_filters_.hiddenIcons==filters_.hiddenIcons
        &&cached_filters_.hiddenTasks==filters_.hiddenTasks)return visible_points_;
    cached_query_=search_.Text();cached_map_=map_id_;cached_locale_=locale;cached_filters_=filters_;
    point_cache_valid_=true;++point_query_builds_;visible_points_.clear();visible_outlines_.clear();
    const bool chinese=locale=="zh-CN";const MapSearchQuery query(search_.Text());
    const auto floor=std::find_if(floors_.begin(),floors_.end(),[&](const auto& f){return Fold(f.label)==query.term;});
    const auto map=std::find_if(maps_.begin(),maps_.end(),[&](const auto& m){return m.id==map_id_;});
    const bool mapMatch=!query.markers&&map!=maps_.end()&&(query.Matches(Fold(map->chinese))||query.Matches(Fold(map->english)));
    MapIconMask matchingIcons=0;
    if(query.markers){auto labels=UiLocalization();
        for(std::size_t i=0;i<MapDetailIcons.size();++i){
            labels.SetLocale("zh-CN");const bool zh=query.Matches(Fold(labels.Get(MapDetailIcons[i].key)));
            labels.SetLocale("en-US");if(zh||query.Matches(Fold(labels.Get(MapDetailIcons[i].key))))matchingIcons|=MapIconMask{1}<<i;
        }
    }
    for(const auto& p:points_){
        if(p.mapId!=map_id_||!Allows(p))continue;
        const auto icons=p.icons&~filters_.hiddenIcons;
        const bool nameMatch=query.Matches(p.searchChinese)||query.Matches(p.searchEnglish);
        if(query.markers){if(!nameMatch&&!(icons&matchingIcons))continue;}
        else if(!query.term.empty()&&!mapMatch){
            if(floor!=floors_.end()){if(p.floorId!=floor->id)continue;}
            else if(p.category!=MapPointCategory::Task||!nameMatch)continue;
        }
        visible_points_.push_back({p.id,p.floorId,p.type,p.coordinate,chinese?p.chinese:p.english,p.category,
            query.markers&&(icons&matchingIcons)?icons&matchingIcons:icons});
        if(p.outline.size()>1&&(p.category==MapPointCategory::Mine||p.category==MapPointCategory::Sniper||p.category==MapPointCategory::Artillery))
            visible_outlines_.push_back(&p);
    }
    return visible_points_;
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
        entries.push_back({Tr("map.layer.satellite"),filters_.satellite,{},std::nullopt});
        entries.push_back({Tr("map.layer.abstract"),!filters_.satellite,{},std::nullopt});
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
    const auto motion=MapSidebarMotion::Sample(progress_);const float originalOpacity=canvas.brush.GetOpacity();
    D2D1_MATRIX_3X2_F original;canvas.target.GetTransform(&original);
    canvas.target.SetTransform(D2D1::Matrix3x2F::Translation(0,motion.offset)*original);canvas.brush.SetOpacity(originalOpacity*motion.opacity);
    for(std::size_t i=0;i<layout_.filters.size();++i)
        DrawDropdownHeader(canvas,theme,layout_.filters[i],Tr(FilterKeys[i]),panel_open_&&panel_==static_cast<MapFilterPanel>(i),false);
    canvas.target.SetTransform(original);canvas.brush.SetOpacity(originalOpacity);
    if(!panel_||panel_progress_==0)return;
    const auto list=FilterList();const auto r=list.bounds;const auto entries=FilterEntries();
    const float opacity=canvas.brush.GetOpacity();canvas.brush.SetOpacity(opacity*panel_progress_*motion.opacity);
    canvas.target.PushAxisAlignedClip({r.left,r.top,r.left+(r.right-r.left)*MapLayout::Ease(panel_progress_),r.bottom},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    canvas.Round(r,8,theme.surface);canvas.brush.SetColor(theme.divider);
    canvas.target.DrawRoundedRectangle(D2D1::RoundedRect(r,8,8),&canvas.brush,1);
    canvas.Text(Tr(FilterKeys[static_cast<std::size_t>(*panel_)]),canvas.smallFormat,
        {r.left+12,r.top+6,r.right-8,r.top+32},theme.accent);
    canvas.target.PushAxisAlignedClip(list.Body(),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(entries.empty())canvas.Text(Tr(TextKey::MapNoResults),canvas.smallFormat,list.Body(),theme.secondaryText);
    for(std::size_t i=0;i<entries.size();++i){auto row=list.Row(i);if(entries[i].detail)row.left+=12;
        if(row.bottom<=list.Body().top||row.top>=list.Body().bottom)continue;
        if(*panel_==MapFilterPanel::Layers&&(i==3||i==4)){
            if(i==3)canvas.Text(entries[i].label,canvas.smallFormat,row,theme.secondaryText);
            else MapOpacitySlider{row}.Draw(canvas,theme,filters_.otherFloorOpacity);
            continue;
        }
        DrawMapCheck(canvas,theme,row,entries[i].label,entries[i].enabled,entries[i].category,
            entries[i].detail?MapIconBit(*entries[i].detail):0,&marker_images_);}
    DrawScrollbar(canvas,theme,{list.Bar(entries.size()),1});
    canvas.target.PopAxisAlignedClip();canvas.target.PopAxisAlignedClip();canvas.brush.SetOpacity(opacity);
}
void MapPage::ReleaseDetailImages() const noexcept {
    satellite_.ReleaseDetailCache();for(const auto& image:images_)image.ReleaseDetailCache();
    for(const auto& image:upper_images_)image.ReleaseDetailCache();
}
void MapPage::TrimDetailImages() const noexcept {
    if(progress_==0||!filters_.geometry){ReleaseDetailImages();return;}
    const auto needed=[&](std::size_t i){return i==FloorIndex()||(floor_progress_<1&&i==previous_floor_);};
    if(!filters_.satellite)satellite_.ReleaseDetailCache();
    for(std::size_t i=0;i<images_.size();++i)if(filters_.satellite||!needed(i))images_[i].ReleaseDetailCache();
    for(std::size_t i=0;i<upper_images_.size();++i)if(!filters_.satellite||!needed(i))upper_images_[i].ReleaseDetailCache();
}
void MapPage::Tick(float elapsed){
    const bool switching=floor_progress_<1,closing=progress_>0&&!Selected();
    const float dt=std::clamp(elapsed,0.0F,.05F);
    viewport_.Tick(dt);
    mode_switch_.Tick(dt);
    progress_=std::clamp(progress_+(Selected()?dt:-dt)/.36F,0.0F,1.0F);
    panel_progress_=std::clamp(panel_progress_+(panel_open_?dt:-dt)/.18F,0.0F,1.0F);
    floor_progress_=std::min(1.0F,floor_progress_+dt/.22F);
    picker_progress_=std::clamp(picker_progress_+(picker_open_?dt:-dt)/.18F,0.0F,1.0F);
    if((switching&&floor_progress_==1)||(closing&&progress_==0))TrimDetailImages();
}
bool MapPage::ClockTick(std::int64_t utc) noexcept {
    const bool caret=(utc%1000+1000)%1000<500;
    const bool repaint=(search_.Focused()&&caret!=caret_visible_)
        ||(Selected()&&MapInformationCard::Bounds(layout_)&&MapGameSeconds(utc)!=MapGameSeconds(clock_utc_));
    clock_utc_=utc;caret_visible_=caret;return repaint;
}
void MapPage::Draw(const UiCanvas& canvas,const UiTheme& theme) const {
    DrawPageHeader(canvas,theme,layout_.search.left,layout_.search.right,Tr(TextKey::NavMap));
    search_.Draw(canvas,theme,layout_.search,Tr(TextKey::MapSearch),caret_visible_);
    if(unavailable_){canvas.Text(Tr(TextKey::Unavailable),canvas.body,layout_.content,theme.secondaryText);return;}
    DrawMapSelection(canvas,theme);
    if(missing_reference_){canvas.Text(Tr("map.no_background"),canvas.body,layout_.content,theme.secondaryText);DrawMapPicker(canvas,theme);return;}
    canvas.target.PushAxisAlignedClip(layout_.content,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(progress_>0){
        const float reveal=MapLayout::Ease(progress_);const auto& points=Points();
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
            canvas.Text(label,canvas.smallFormat,{info.left+46,info.top+60,info.right-14,info.top+80},theme.accent);
            std::wstring conditions;
            if(source->switchRequired)conditions=Tr("map.extract.switch");
            if(source->paymentRequired){if(!conditions.empty())conditions+=L" · ";conditions+=Tr("map.extract.payment");}
            if(!conditions.empty())canvas.Text(conditions,canvas.smallFormat,{info.left+46,info.top+80,info.right-14,info.bottom-2},theme.secondaryText);
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
        if(real_&&filters_.geometry){
            const auto imageRect=D2D1_RECT_F{origin.x,origin.y,end.x,end.y};const float transition=MapLayout::Ease(floor_progress_);
            if(filters_.satellite&&!generic_)satellite_.Draw(canvas.target,imageRect,opacity*reveal,expanded);
            const auto drawFloor=[&](std::size_t index,float alpha){
                if(alpha<=0)return;
                const auto& preferred=filters_.satellite?upper_images_[index]:images_[index];
                const auto& image=generic_&&!preferred.Ready()?(filters_.satellite?images_[index]:upper_images_[index]):preferred;
                if(image.Ready())image.Draw(canvas.target,imageRect,opacity*reveal*alpha,expanded);};
            if(floor_progress_<1)drawFloor(previous_floor_,(generic_||filters_.satellite)?1-transition:1.0F);
            drawFloor(FloorIndex(),transition);
        }
        for(float x=0;filters_.grid&&x<=world_.width;x+=100)
            canvas.target.DrawLine(viewport_.ToScreen({x,0}),viewport_.ToScreen({x,world_.height}),&canvas.brush,.5F);
        for(float y=0;filters_.grid&&y<=world_.height;y+=100)
            canvas.target.DrawLine(viewport_.ToScreen({0,y}),viewport_.ToScreen({world_.width,y}),&canvas.brush,.5F);
        if(!real_&&filters_.geometry)for(const auto block:MapPrototype::Buildings){const auto a=viewport_.ToScreen({block.left,block.top}),b=viewport_.ToScreen({block.right,block.bottom});
            canvas.Round({a.x,a.y,b.x,b.y},3,theme.selected);}
        // 危险区边界与点位共用投影/筛选；仅静态参考，不表示实时伤害范围。
        // Hazard outlines share marker projection/filtering; static references are not live damage ranges.
        for(const auto* cached:visible_outlines_){
            const auto& point=*cached;
            const float alpha=MapFloorOpacity(filters_,floor_id_,point.floorId);
            if(alpha<=0)continue;
            canvas.brush.SetColor(theme.accent);canvas.brush.SetOpacity(opacity*reveal*alpha*.7F);
            for(std::size_t i=0;i<point.outline.size();++i)canvas.target.DrawLine(viewport_.ToScreen(point.outline[i]),
                viewport_.ToScreen(point.outline[(i+1)%point.outline.size()]),&canvas.brush,1.3F);
        }
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
        MapResetButton{r}.Draw(canvas,theme);
        canvas.Text(floors_[FloorIndex()].label,canvas.body,{r.left+14,r.top+10,r.left+70,r.top+38},theme.accent);
        if(real_&&generic_&&filters_.geometry&&!images_[FloorIndex()].Ready()&&!upper_images_[FloorIndex()].Ready())
            canvas.Text(Tr("map.no_background"),canvas.smallFormat,
                {r.left+16,r.top+50,r.right-16,r.top+80},theme.secondaryText);
        else if(points.empty())canvas.Text(Tr(TextKey::MapNoResults),canvas.smallFormat,
            {r.left+16,r.top+50,r.right-16,r.top+80},theme.secondaryText);
        canvas.Text(ReferenceLabel(),canvas.smallFormat,{r.left+14,r.bottom-30,r.right-64,r.bottom-6},theme.secondaryText);
        canvas.target.PopAxisAlignedClip();
        auto anchor=layout_.stack.Plate(0).vertices[1];const float index=FloorPosition();
        anchor.x-=index*layout_.stack.width*layout_.stack.stagger;anchor.y+=index*layout_.stack.width*.20F;
        FloorConnector{anchor,{r.left,r.top+(r.bottom-r.top)*.45F}}.Draw(canvas,theme.secondaryText,reveal);
        canvas.brush.SetOpacity(opacity);
        DrawNavigationButton(canvas,theme,layout_.back,NavigationGlyph::Back,Selected(),false,back_pressed_);
    }else{
        canvas.Text(ReferenceLabel(),canvas.smallFormat,
            {layout_.content.left,layout_.content.bottom-35,layout_.content.right,layout_.content.bottom},theme.secondaryText);
    }
    std::vector<std::wstring_view> labels;
    std::vector<const LocalImage*> previews,overlays;
    for(const auto& floor:floors_)labels.push_back(floor.label);
    if(real_)for(std::size_t i=0;i<floors_.size();++i){
        if(generic_){const auto* image=filters_.satellite?&upper_images_[i]:&images_[i];
            if(!image->Ready())image=filters_.satellite?&images_[i]:&upper_images_[i];previews.push_back(image);overlays.push_back(nullptr);}
        else {previews.push_back(filters_.satellite?&satellite_:&images_[i]);overlays.push_back(filters_.satellite&&upper_images_[i].Ready()?&upper_images_[i]:nullptr);}}
    layout_.stack.Draw(canvas,theme,labels,Selected()?std::optional<std::size_t>(FloorIndex()):std::nullopt,hovered_floor_,previews,overlays,FloorPosition());
    if(progress_>0)DrawFilters(canvas,theme);
    if(progress_>0)if(const auto bounds=MapInformationCard::Bounds(layout_);bounds&&Information()){
        const auto motion=MapSidebarMotion::Sample(progress_);const float opacity=canvas.brush.GetOpacity();
        D2D1_MATRIX_3X2_F original;canvas.target.GetTransform(&original);
        canvas.target.SetTransform(D2D1::Matrix3x2F::Translation(0,motion.offset)*original);canvas.brush.SetOpacity(opacity*motion.opacity);
        const auto* info=Information();MapInformationCard{*bounds}.Draw(canvas,theme,Wide(info->players),info->raidDuration,clock_utc_);
        canvas.target.SetTransform(original);canvas.brush.SetOpacity(opacity);
    }
    canvas.target.PopAxisAlignedClip();
    DrawMapPicker(canvas,theme);
}
std::wstring MapPage::ReferenceLabel() const {
    if(!real_)return Tr(TextKey::MapPreview);
    if(!generic_)return Tr(TextKey::MapReference);
    auto label=Tr("map.reference.generic");
    if(const auto* map=Information();map&&!map->author.empty())label+=L" · "+Wide(map->author);
    return label;
}
void MapPage::DrawMapPicker(const UiCanvas& canvas,const UiTheme& theme) const {
    if(UsesPicker()&&picker_progress_>0){
        const auto picker=Picker();const auto opacity=canvas.brush.GetOpacity();canvas.brush.SetOpacity(opacity*MapLayout::Ease(picker_progress_));
        canvas.Round(picker.panel,theme.cornerRadius,theme.surface);canvas.target.PushAxisAlignedClip(picker.panel,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const auto maps=MapItems();for(std::size_t i=0;i<maps.size();++i){const auto row=picker.Row(i);
            if(row.bottom<picker.panel.top||row.top>picker.panel.bottom)continue;
            DrawDropdownOption(canvas,theme,row,maps[i].label,map_id_==maps[i].id,true,false);}
        canvas.target.PopAxisAlignedClip();canvas.brush.SetOpacity(opacity);
    }
}
void MapPage::DrawMapSelection(const UiCanvas& canvas,const UiTheme& theme) const {
    const auto maps=MapItems();float mapIndex=0;
    for(std::size_t i=0;i<maps.size();++i)if(maps[i].id==map_id_)mapIndex=static_cast<float>(i);
    if(UsesPicker())DrawDropdownHeader(canvas,theme,Picker().header,maps[static_cast<std::size_t>(mapIndex)].label,picker_open_,false);
    else DrawTabBar<std::string_view>(canvas,theme,canvas.smallFormat,maps,layout_.maps,map_id_,std::optional<std::string_view>{},map_id_,1,mapIndex);
    if(pve_catalog_.Ready()){
        const auto pvp=Tr("map.mode.pvp"),pve=Tr("map.mode.pve");
        mode_switch_.Draw(canvas,theme,ModeBounds(),{pvp,pve});
    }
}
std::optional<std::string_view> MapPage::MarkerAt(D2D1_POINT_2F p) const {
    if(!Selected()||!MapContains(layout_.viewport,p))return std::nullopt;
    const auto& points=Points();
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
bool MapPage::MouseMove(float x,float y){
    const D2D1_POINT_2F p{x,y};const auto hovered=layout_.stack.Hit(p);const bool changed=hovered!=hovered_floor_;hovered_floor_=hovered;
    if(opacity_drag_){const float previous=filters_.otherFloorOpacity;filters_.otherFloorOpacity=MapOpacitySlider{FilterList().Row(4)}.Value(x);return changed||previous!=filters_.otherFloorOpacity;}
    if(scroll_drag_){const float previous=filter_scroll_;if(const auto bar=FilterList().Bar(FilterEntries().size()))filter_scroll_=bar->OffsetFromThumbTop(y-*scroll_drag_);return changed||previous!=filter_scroll_;}
    if(drag_){const bool moved=p.x!=drag_->x||p.y!=drag_->y;viewport_.Pan({p.x-drag_->x,p.y-drag_->y});drag_=p;return changed||moved;}
    return changed;
}
void MapPage::MouseDown(float x,float y){
    const D2D1_POINT_2F p{x,y};CancelDrag();
    if(MapContains(layout_.search,p)){search_.Focus();return;}search_.Blur();
    picker_pressed_=false;picker_row_.reset();mode_pressed_.reset();
    if(pve_catalog_.Ready())for(bool pve:{false,true})if(MapContains(ModeButton(pve),p)){mode_pressed_=pve;return;}
    if(UsesPicker()){
        if(MapContains(Picker().header,p)){picker_pressed_=true;return;}
        if(picker_open_){picker_row_=Picker().Hit(x,y,maps_.size());if(picker_row_)return;picker_open_=false;}
    }
    const auto maps=MapItems();pressed_map_=UsesPicker()?std::nullopt:HitTestTabBar<std::string_view>(maps,layout_.maps,x,y);if(pressed_map_)return;
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
    if(MapResetButton{layout_.viewport}.Hit(p)){reset_pressed_=true;return;}
    if(const auto id=MarkerAt(p)){pressed_point_=*id;return;}
    if(MapContains(layout_.viewport,p)){viewport_.StopFocus();drag_=p;}
}
void MapPage::MouseUp(float x,float y){
    const D2D1_POINT_2F p{x,y};
    if(mode_pressed_){const bool pve=*mode_pressed_;mode_pressed_.reset();if(MapContains(ModeButton(pve),p))SetMode(pve?data::GameMode::Pve:data::GameMode::Pvp);return;}
    if(picker_pressed_){picker_pressed_=false;if(MapContains(Picker().header,p))picker_open_=!picker_open_;return;}
    if(picker_row_){const auto row=*picker_row_;picker_row_.reset();
        if(Picker().Hit(x,y,maps_.size())==row){picker_open_=false;SelectMap(maps_[row].id);}return;}
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
            else if(row==5)filters_.satellite=true;
            else if(row==6)filters_.satellite=false;
            if(row==1||row==5||row==6)TrimDetailImages();
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
    if(reset_pressed_&&MapResetButton{layout_.viewport}.Hit(p))viewport_.FitSmooth();
    CancelDrag();
}
bool MapPage::Wheel(int delta,float x,float y,bool control){
    if(picker_open_&&MapContains(Picker().panel,{x,y})){
        picker_scroll_=std::clamp(picker_scroll_-static_cast<float>(delta)/WHEEL_DELTA*64,0.0F,Picker().Maximum(maps_.size()));return true;}
    if(panel_&&panel_progress_>0&&MapContains(layout_.flyout,{x,y})){
        if(const auto bar=FilterList().Bar(FilterEntries().size()))
            filter_scroll_=std::clamp(filter_scroll_-static_cast<float>(delta)/WHEEL_DELTA*60,0.0F,bar->maximum);
        return true;
    }
    // Ctrl 滚轮按显示顺序切层，边界停留且不泄漏为缩放；侧展列表保留自身滚动。
    // Ctrl-wheel steps through displayed floors, clamping without zoom; flyouts retain their own scrolling.
    if(control&&Selected()&&!floors_.empty()&&MapContains(layout_.content,{x,y})){
        const auto index=std::clamp(static_cast<int>(FloorIndex())-delta/WHEEL_DELTA,0,static_cast<int>(floors_.size())-1);
        if(static_cast<std::size_t>(index)!=FloorIndex())SelectFloor(floors_[index].id);
        return true;
    }
    if(!Selected()||progress_<1||!MapContains(layout_.viewport,{x,y}))return false;
    viewport_.ZoomAt({x,y},static_cast<float>(delta)/WHEEL_DELTA);return true;
}
bool MapPage::Key(WPARAM key,bool control){
    if(key==VK_ESCAPE&&picker_open_){picker_open_=false;return true;}
    if(key==VK_ESCAPE&&!interaction_id_.empty()){
        interaction_id_={};viewport_.StopFocus();CancelDrag();
        (void)search_.HandleKeyDown(key,control);return true;
    }
    if(search_.Focused()&&key==VK_RETURN){
        for(const auto& floor:floors_)if(Fold(floor.label)==Fold(search_.Text())){SelectFloor(floor.id);return true;}
        const MapSearchQuery query(search_.Text());
        if(!query.markers)for(const auto& map:maps_)if(query.Matches(Fold(map.chinese))||query.Matches(Fold(map.english))){
            SelectMap(map.id);break;}
        const auto& matches=Points();if(!matches.empty())FocusInteraction(matches.front().id);return true;
    }
    return search_.HandleKeyDown(key,control);
}
}
