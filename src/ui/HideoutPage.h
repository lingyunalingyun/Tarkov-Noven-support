#pragma once
#include "data/HideoutBrowser.h"
#include "ui/SearchBox.h"
#include "ui/HorizontalCardStrip.h"
#include "ui/Scrollbar.h"
#include "ui/TabSelectionAnimation.h"
#include <memory>
#include <unordered_map>

namespace noven::ui {
// 设施和等级均为查看状态，不代表玩家进度；UI 不拥有经济或图片工作线程。
// Station/level selection is browsing state, not player progress; UI owns no economy/image worker.
class HideoutPage {
public:
    void Initialize(const std::filesystem::path& directory, const data::ItemCatalog& items,
        const data::ItemEconomyStore& economy);
    void Prepare(float width,float height,const UiTheme& theme);
    void Draw(const UiCanvas& canvas,const UiTheme& theme,
        const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const;
    void MouseDown(float x,float y);
    std::optional<std::string> MouseUp(float x,float y);
    void MouseMove(float x,float y);
    void CancelDrag() { grab_.reset(); strip_.Release(); }
    bool Wheel(int delta,float x=-1,float y=-1);
    bool Key(WPARAM key,bool control);
    bool Char(wchar_t character);
    bool Tick(float seconds);
    bool Animating() const;
    std::vector<std::string> VisibleImages() const;
    data::GameMode Mode() const { return mode_; }
    const std::wstring& QueryText() const { return search_.Text(); }
    const std::string& ExpandedId() const { return selectedLevel_; }
    const std::string& SelectedStation() const { return station_; }
    float Scroll() const { return scroll_; }
    float StationScroll() const { return strip_.Offset(); }
    std::size_t StationCount() const { return stations_.size(); }
    const std::vector<data::HideoutRow>& Rows() const { return rows_; }
private:
    void BeginTransition();
    void Refresh();
    void Select(std::size_t index);
    void Edited();
    void LayoutStrip();
    float DetailHeight() const;
    float MaxScroll() const;
    float CraftTop() const;
    std::optional<ScrollbarGeometry> Bar() const;
    std::optional<ScrollbarGeometry> RailBar() const;
    std::optional<std::size_t> LevelAt(float x,float y) const;
    std::optional<int> ArrowAt(float x,float y) const;
    D2D1_RECT_F ArrowBounds(int direction) const;
    std::optional<std::string> ItemAt(float x,float y) const;
    TabBarLayout Tabs() const;
    data::HideoutCatalog catalog_;
    std::unique_ptr<data::HideoutBrowser> browser_;
    std::vector<data::HideoutRow> rows_;
    std::vector<std::size_t> stations_, levels_;
    std::vector<data::HideoutCraftView> crafts_;
    std::unordered_map<std::string,std::int64_t> viewedLevels_;
    SearchBox search_;
    HorizontalCardStrip strip_;
    TabSelectionAnimation stationAnimation_,levelAnimation_;
    std::optional<data::HideoutRow> outgoingRow_;
    std::vector<data::HideoutCraftView> outgoingCrafts_;
    std::vector<std::int64_t> outgoingLevels_;
    std::optional<ScrollbarGeometry> outgoingBar_;
    std::optional<ScrollbarGeometry> outgoingRailBar_;
    float detailProgress_{1},detailFromOpacity_{1},outgoingScroll_{};
    data::GameMode mode_{data::GameMode::Pvp}, outgoing_{data::GameMode::Pvp};
    std::string locale_,station_,selectedLevel_;
    std::size_t selected_{};
    float tabProgress_{1},underline_{},underlineFrom_{};
    float scroll_{},target_{},height_{},left_{},right_{};
    std::optional<float> grab_;
    std::optional<std::size_t> pressedStation_,pressedLevel_;
    std::optional<int> pressedArrow_;
    std::optional<int> hoveredArrow_;
    std::optional<std::string> pressedItem_;
    std::optional<data::GameMode> pressedTab_,hoveredTab_;
    std::chrono::system_clock::time_point updated_{};
};
}
