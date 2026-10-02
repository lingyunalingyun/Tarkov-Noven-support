#pragma once
#include "ui/MapLayout.h"
#include "ui/MapPrototypeData.h"
#include "ui/MapViewport.h"
#include "ui/MapClock.h"
#include "ui/MapSearch.h"
#include "ui/MapSidebar.h"
#include "ui/MapPicker.h"
#include "ui/SegmentedSwitch.h"
#include "ui/MapViewportControls.h"
#include "ui/SearchBox.h"
#include "ui/LocalImage.h"
#include "data/MapCatalog.h"
#include "data/GameMode.h"
#include <vector>

namespace noven::ui {
// 常驻 UI 状态独立于任务/藏身处；生产绑定失败不能回退成演示地图。
// Resident state is independent of Tasks/Hideout; failed production binding never displays demo maps.
class MapPage final {
public:
    MapPage();
    MapPage(const MapPage&)=delete;
    MapPage& operator=(const MapPage&)=delete;
    bool Initialize(const std::filesystem::path& assets,std::wstring& error);
    void SetAssetGeneration(std::filesystem::path generation);
    bool RealData() const noexcept{return real_;}
    const data::MapRecord* Information() const noexcept{return Catalog().Map(map_id_);}
    const data::MapCatalog& Catalog() const noexcept{return Mode()==data::GameMode::Pve?pve_catalog_:catalog_;}
    const data::MapCatalog& Catalog(data::GameMode mode) const noexcept{return mode==data::GameMode::Pve?pve_catalog_:catalog_;}
    data::GameMode Mode() const noexcept{return mode_;}
    bool SetMode(data::GameMode mode);
    bool OpenInteraction(std::string_view id);
    void ReleaseDetailImages() const noexcept;
    void Prepare(float width,float height,const UiTheme& theme);
    void Draw(const UiCanvas& canvas,const UiTheme& theme) const;
    void Tick(float elapsed);
    bool Animating() const noexcept{return mode_switch_.Animating()||progress_!=(expanded_?1.0F:0.0F)||panel_progress_!=(panel_open_?1.0F:0.0F)||floor_progress_<1||viewport_.Focusing()||picker_progress_!=(picker_open_?1.0F:0.0F);}
    bool ClockTick(std::int64_t utc) noexcept;
    bool Selected() const noexcept{return expanded_;}
    void Overview(){expanded_=false;panel_open_=false;search_.Blur();viewport_.StopFocus();CancelDrag();}
    std::string_view FloorId() const noexcept{return floor_id_;}
    std::string_view MapId() const noexcept{return map_id_;}
    void SelectMap(std::string_view id);
    std::string_view InteractionId() const noexcept{return interaction_id_;}
    const MapViewport& Viewport() const noexcept{return viewport_;}
    const MapLayout& Layout() const noexcept{return layout_;}
    const SearchBox& Search() const noexcept{return search_;}
    const MapFilters& Filters() const noexcept{return filters_;}
    float MarkerOpacity(const MapInteractionPoint& point) const {return MapFloorOpacity(filters_,floor_id_,point.floorId);}
    std::optional<MapFilterPanel> Panel() const noexcept{return panel_open_?panel_:std::nullopt;}
    MapFilterList FilterList() const noexcept{return {layout_.flyout,filter_scroll_};}
    void SelectFloor(std::string_view id);
    bool FocusInteraction(std::string_view id);
    const std::vector<MapInteractionPoint>& Points() const;
    std::size_t PointQueryBuilds() const noexcept{return point_query_builds_;}
    std::size_t FilterEntryBuilds() const noexcept{return filter_entry_builds_;}
    std::optional<MapInteractionPoint> SelectedPoint() const;
    bool MouseMove(float x,float y);
    void MouseLeave(){hovered_floor_.reset();}
    void MouseDown(float x,float y);
    void MouseUp(float x,float y);
    bool Wheel(int delta,float x,float y,bool control=false);
    bool Key(WPARAM key,bool control);
    bool Char(wchar_t value){return search_.HandleChar(value);}
    void CancelDrag(){drag_.reset();scroll_drag_.reset();opacity_drag_=false;pressed_floor_.reset();pressed_point_={};pressed_map_.reset();
        pressed_filter_.reset();pressed_row_.reset();reset_pressed_=false;back_pressed_=false;}
private:
    const std::vector<TabBarItem<std::string_view>>& MapItems() const;
    struct Floor {std::string id;std::wstring label;};
    struct Map {std::string id;std::wstring chinese,english;};
    struct Point {
        std::string id,floorId,mapId;MapMarkerType type;D2D1_POINT_2F coordinate;
        std::wstring chinese,english;MapPointCategory category;
        bool sharedExtract{};
        MapIconMask icons{};
        std::wstring searchChinese,searchEnglish;
        std::vector<D2D1_POINT_2F> outline;
        bool switchRequired{},paymentRequired{};
    };
    bool Allows(const Point& point) const;
    void BindPoints();
    void BindMapImages();
    void ReloadFloorImages();
    MapPicker Picker() const{return MapPicker::Sample(layout_,picker_scroll_,maps_.size());}
    bool UsesPicker() const noexcept{return real_&&maps_.size()>3;}
    D2D1_RECT_F ModeBounds() const noexcept{return {layout_.search.right-144,130,layout_.search.right,166};}
    D2D1_RECT_F ModeButton(bool pve) const noexcept{return SegmentedSwitch::Button(ModeBounds(),pve);}
    void DrawMapSelection(const UiCanvas& canvas,const UiTheme& theme) const;
    void DrawMapPicker(const UiCanvas& canvas,const UiTheme& theme) const;
    std::wstring ReferenceLabel() const;
    void TrimDetailImages() const noexcept;
    std::vector<Floor> floors_;
    std::vector<Map> maps_;
    // 名称视图只借用页面数据；重绑目录必须失效，语言切换按需重建。
    // Label views borrow page data; catalog rebinds invalidate them and locale changes rebuild lazily.
    mutable std::vector<TabBarItem<std::string_view>> map_items_;
    mutable std::string map_items_locale_;
    std::string floor_locale_;
    std::vector<Point> points_;
    // 返回值借用页面名称；只有查询、地图、语言或可见性筛选改变时重建，禁止跨重绑保存引用。
    // Results borrow page-owned labels; rebuild only on query/map/locale/visibility changes, not across rebinds.
    mutable std::vector<MapInteractionPoint> visible_points_;
    mutable std::vector<const Point*> visible_outlines_;
    mutable std::wstring cached_query_;
    mutable std::string cached_locale_;
    mutable std::string_view cached_map_;
    mutable MapFilters cached_filters_;
    mutable bool point_cache_valid_{};
    mutable std::size_t point_query_builds_{};
    std::vector<LocalImage> images_;
    MapIconImages marker_images_;
    LocalImage satellite_;
    std::vector<LocalImage> upper_images_;
    std::size_t previous_floor_{};
    data::MapCatalog catalog_;
    data::MapCatalog pve_catalog_;
    data::GameMode mode_{data::GameMode::Pvp};
    SegmentedSwitch mode_switch_;
    std::filesystem::path assets_;
    std::filesystem::path asset_generation_;
    D2D1_SIZE_F world_{MapPrototype::World};
    bool real_{},unavailable_{},missing_reference_{},generic_{};
    bool picker_open_{},picker_pressed_{};
    float picker_progress_{},picker_scroll_{};
    std::optional<std::size_t> picker_row_;
    std::optional<bool> mode_pressed_;
    std::size_t FloorIndex() const;
    float FloorPosition() const noexcept;
    std::optional<std::string_view> MarkerAt(D2D1_POINT_2F p) const;
    // 本地化返回值由条目拥有，不能保存临时翻译字符串的视图。
    // Entries own translated labels, never views into temporary localization results.
    struct FilterEntry {std::wstring label;bool enabled;std::string_view pointId;
        std::optional<MapPointCategory> category;std::optional<MapDetailIcon> detail;};
    const std::vector<FilterEntry>& FilterEntries() const;
    // 单一有界快照，不按每个查询累计缓存；绘制和命中共享同一条目身份。
    // One bounded snapshot, not a cache per query; drawing and hits share the same entry identities.
    mutable std::vector<FilterEntry> filter_entries_;
    mutable bool filter_entries_valid_{};
    mutable std::size_t filter_entry_builds_{};
    mutable std::optional<MapFilterPanel> filter_entries_panel_;
    mutable std::string filter_entries_map_,filter_entries_locale_;
    mutable std::wstring filter_entries_query_;
    mutable MapFilters filter_entries_filters_;
    void TogglePanel(MapFilterPanel panel);
    void DrawFilters(const UiCanvas& canvas,const UiTheme& theme) const;
    MapLayout layout_{};
    MapViewport viewport_{MapPrototype::World};
    SearchBox search_;
    std::string_view floor_id_,interaction_id_,pressed_point_;
    std::string_view map_id_{MapPrototype::Maps.front().id};
    std::optional<std::string_view> pressed_map_;
    MapFilters filters_;
    std::optional<MapFilterPanel> panel_,pressed_filter_;
    std::optional<std::size_t> pressed_row_;
    std::optional<float> scroll_drag_;
    float filter_scroll_{},panel_progress_{};
    bool panel_open_{},pressed_row_check_{};
    bool opacity_drag_{};
    float progress_{},floor_from_{},floor_to_{},floor_progress_{1};
    std::int64_t clock_utc_{MapUtcMilliseconds()};
    bool caret_visible_{true};
    std::optional<std::size_t> hovered_floor_,pressed_floor_;
    std::optional<D2D1_POINT_2F> drag_;
    bool reset_pressed_{},expanded_{},back_pressed_{};
};
}
