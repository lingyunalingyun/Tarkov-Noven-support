#pragma once
#include "ui/MapLayout.h"
#include "ui/MapPrototypeData.h"
#include "ui/MapViewport.h"
#include "ui/MapClock.h"
#include "ui/MapSearch.h"
#include "ui/MapSidebar.h"
#include "ui/MapViewportControls.h"
#include "ui/SearchBox.h"
#include "ui/LocalImage.h"
#include "data/MapCatalog.h"
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
    bool RealData() const noexcept{return real_;}
    const data::MapRecord* Information() const noexcept{return catalog_.Map(map_id_);}
    const data::MapCatalog& Catalog() const noexcept{return catalog_;}
    bool OpenInteraction(std::string_view id);
    void ReleaseDetailImages() const noexcept;
    void Prepare(float width,float height,const UiTheme& theme);
    void Draw(const UiCanvas& canvas,const UiTheme& theme) const;
    void Tick(float elapsed);
    bool Animating() const noexcept{return progress_!=(expanded_?1.0F:0.0F)||panel_progress_!=(panel_open_?1.0F:0.0F)||floor_progress_<1||viewport_.Focusing();}
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
    std::optional<MapInteractionPoint> SelectedPoint() const;
    bool MouseMove(float x,float y);
    void MouseLeave(){hovered_floor_.reset();}
    void MouseDown(float x,float y);
    void MouseUp(float x,float y);
    bool Wheel(int delta,float x,float y);
    bool Key(WPARAM key,bool control);
    bool Char(wchar_t value){return search_.HandleChar(value);}
    void CancelDrag(){drag_.reset();scroll_drag_.reset();opacity_drag_=false;pressed_floor_.reset();pressed_point_={};pressed_map_.reset();
        pressed_filter_.reset();pressed_row_.reset();reset_pressed_=false;back_pressed_=false;}
private:
    std::vector<TabBarItem<std::string_view>> MapItems() const;
    struct Floor {std::string id;std::wstring label;};
    struct Map {std::string id;std::wstring chinese,english;};
    struct Point {
        std::string id,floorId,mapId;MapMarkerType type;D2D1_POINT_2F coordinate;
        std::wstring chinese,english;MapPointCategory category;
        bool sharedExtract{};
        MapIconMask icons{};
        std::wstring searchChinese,searchEnglish;
    };
    bool Allows(const Point& point) const;
    void TrimDetailImages() const noexcept;
    std::vector<Floor> floors_;
    std::vector<Map> maps_;
    std::vector<Point> points_;
    // 返回值借用页面名称；只有查询、地图、语言或可见性筛选改变时重建，禁止跨重绑保存引用。
    // Results borrow page-owned labels; rebuild only on query/map/locale/visibility changes, not across rebinds.
    mutable std::vector<MapInteractionPoint> visible_points_;
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
    D2D1_SIZE_F world_{MapPrototype::World};
    bool real_{},unavailable_{};
    std::size_t FloorIndex() const;
    float FloorPosition() const noexcept;
    std::optional<std::string_view> MarkerAt(D2D1_POINT_2F p) const;
    // 本地化返回值由条目拥有，不能保存临时翻译字符串的视图。
    // Entries own translated labels, never views into temporary localization results.
    struct FilterEntry {std::wstring label;bool enabled;std::string_view pointId;
        std::optional<MapPointCategory> category;std::optional<MapDetailIcon> detail;};
    std::vector<FilterEntry> FilterEntries() const;
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
