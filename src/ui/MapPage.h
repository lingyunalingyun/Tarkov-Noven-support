#pragma once
#include "ui/MapLayout.h"
#include "ui/MapPrototypeData.h"
#include "ui/MapViewport.h"
#include "ui/SearchBox.h"
#include <vector>

namespace noven::ui {
// 常驻 UI 状态独立于任务/藏身处；演示数据仅从 MapPrototypeData 读取。
// Resident UI state is independent of Tasks/Hideout; demo data comes only from MapPrototypeData.
class MapPage final {
public:
    void Prepare(float width,float height,const UiTheme& theme);
    void Draw(const UiCanvas& canvas,const UiTheme& theme) const;
    void Tick(float elapsed);
    bool Animating() const noexcept{return progress_!=(expanded_?1.0F:0.0F)||floor_progress_<1||search_.Focused();}
    bool Selected() const noexcept{return expanded_;}
    void Overview(){expanded_=false;search_.Blur();CancelDrag();}
    std::string_view FloorId() const noexcept{return floor_id_;}
    std::string_view MapId() const noexcept{return map_id_;}
    void SelectMap(std::string_view id);
    std::string_view InteractionId() const noexcept{return interaction_id_;}
    const MapViewport& Viewport() const noexcept{return viewport_;}
    const MapLayout& Layout() const noexcept{return layout_;}
    const SearchBox& Search() const noexcept{return search_;}
    void SelectFloor(std::string_view id);
    bool FocusInteraction(std::string_view id);
    std::vector<MapInteractionPoint> Points() const;
    void MouseMove(float x,float y);
    void MouseLeave(){hovered_floor_.reset();}
    void MouseDown(float x,float y);
    void MouseUp(float x,float y);
    bool Wheel(int delta,float x,float y);
    bool Key(WPARAM key,bool control);
    bool Char(wchar_t value){return search_.HandleChar(value);}
    void CancelDrag(){drag_.reset();pressed_floor_.reset();pressed_point_={};pressed_map_.reset();reset_pressed_=false;back_pressed_=false;}
private:
    std::size_t FloorIndex() const;
    float FloorPosition() const noexcept;
    D2D1_RECT_F ResetBounds() const noexcept;
    std::optional<std::string_view> MarkerAt(D2D1_POINT_2F p) const;
    MapLayout layout_{};
    MapViewport viewport_{MapPrototype::World};
    SearchBox search_;
    std::string_view floor_id_,interaction_id_,pressed_point_;
    std::string_view map_id_{MapPrototype::Maps.front().id};
    std::optional<std::string_view> pressed_map_;
    float progress_{},floor_from_{},floor_to_{},floor_progress_{1},clock_{};
    std::optional<std::size_t> hovered_floor_,pressed_floor_;
    std::optional<D2D1_POINT_2F> drag_;
    bool reset_pressed_{},expanded_{},back_pressed_{};
};
}
