#pragma once

#include "ui/HorizontalCardStrip.h"
#include "ui/Scrollbar.h"
#include "ui/SearchBox.h"
#include "ui/TabSelectionAnimation.h"
#include "data/TaskBrowser.h"
#include "data/TaskMapLinks.h"

#include <optional>
#include <unordered_map>
#include <wrl/client.h>

namespace noven::ui {

// 页面只拥有选择与滚动状态；任务身份和内容均由只读目录拥有。
// The page owns selection and scroll state only; the read-only catalog owns task identity and content.
class TasksPage final {
public:
    struct Action {enum class Destination {Prices,Map};Destination destination;std::string id;data::GameMode mode{data::GameMode::Pvp};};
    void SetMode(data::GameMode mode);
    void SetMapLinks(const data::MapCatalog& catalog,data::GameMode mode=data::GameMode::Pvp);
    struct MapTargetRow {std::string id;std::wstring title,source;D2D1_RECT_F bounds;};
    struct MapTargetList {D2D1_RECT_F bounds,close;std::vector<MapTargetRow> rows;};
    [[nodiscard]] std::optional<MapTargetList> MapTargetsLayout() const;
    void Initialize(const std::filesystem::path& directory,const data::ItemCatalog& items);
    void Prepare(float width, float height, const UiTheme& theme);
    void Draw(const UiCanvas& canvas, const UiTheme& theme,
        const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const;
    void MouseDown(float x, float y);
    std::optional<Action> MouseUp(float x, float y);
    [[nodiscard]] std::optional<D2D1_RECT_F> ObjectiveBounds(std::string_view id) const;
    void MouseMove(float x, float y);
    void CancelDrag();
    bool Wheel(int delta, float x, float y);
    bool Key(WPARAM key, bool control);
    bool Char(wchar_t character);
    bool Tick(float seconds);
    bool Animating() const;
    bool GoBackTask();
    bool OpenTask(std::string_view id,data::GameMode mode);
    const data::TaskCatalog& Catalog() const noexcept {return catalog_;}
    void Activate() { textScrollTime_=0.0F; }

    [[nodiscard]] const std::wstring& QueryText() const { return search_.Text(); }
    [[nodiscard]] std::string_view SelectedTrader() const;
    [[nodiscard]] std::string_view SelectedTask() const;
    [[nodiscard]] float Scroll() const { return scroll_; }
    [[nodiscard]] bool Narrow() const { return narrow_; }
    [[nodiscard]] data::GameMode Mode() const { return mode_; }
    [[nodiscard]] std::vector<std::string> VisibleImages() const;

private:
    struct RewardEntry {
        std::wstring text;
        std::string itemId;
    };
    struct RewardGroups {
        std::vector<RewardEntry> direct;
        std::vector<RewardEntry> crafts;
        std::vector<RewardEntry> offers;
    };

    void Refresh(bool queryChanged = false);
    void BeginScrollbarTransition();
    void SelectTrader(std::size_t visibleIndex, bool snap = false);
    void SelectTask(std::size_t visibleIndex, bool snap = false);
    void NavigateTask(std::string_view id, bool remember = true);
    void RevealTask(std::size_t visibleIndex);
    [[nodiscard]] const data::TaskTrader* Trader() const;
    [[nodiscard]] const data::TaskRecord* Task() const;
    [[nodiscard]] std::wstring Text(const std::string& zh,const std::string& en) const;
    [[nodiscard]] std::vector<std::string> Unlocks() const;
    [[nodiscard]] RewardGroups Rewards() const;
    [[nodiscard]] D2D1_RECT_F SearchBounds() const;
    [[nodiscard]] D2D1_RECT_F TaskListBounds() const;
    [[nodiscard]] D2D1_RECT_F DetailBounds() const;
    [[nodiscard]] D2D1_RECT_F TraderArrowBounds(int direction) const;
    [[nodiscard]] std::optional<int> TraderArrowAt(float x, float y) const;
    [[nodiscard]] std::optional<std::size_t> TaskAt(float x, float y) const;
    [[nodiscard]] std::optional<std::string> ChainAt(float x, float y) const;
    [[nodiscard]] std::optional<std::string> RewardItemAt(float x, float y) const;
    [[nodiscard]] std::optional<std::string> ObjectiveMapPointAt(float x,float y) const;
    [[nodiscard]] const data::TaskMapLinks& MapLinks() const {return mode_==data::GameMode::Pve?pveMapLinks_:mapLinks_;}
    [[nodiscard]] std::optional<std::string> MapTargetAt(float x,float y) const;
    void CloseMapTargets();
    [[nodiscard]] std::optional<ScrollbarGeometry> Bar() const;
    [[nodiscard]] std::optional<ScrollbarGeometry> TaskBar() const;
    [[nodiscard]] ScrollbarPose TaskBarPose() const;
    [[nodiscard]] D2D1_RECT_F TaskBackBounds() const;
    [[nodiscard]] float DetailHeight() const;
    [[nodiscard]] float MaxScroll() const;
    [[nodiscard]] float TaskMaximum() const;

    SearchBox search_;
    data::TaskCatalog catalog_;
    data::TaskMapLinks mapLinks_;
    data::TaskMapLinks pveMapLinks_;
    std::optional<std::string> mapObjective_,pressedTarget_,hoveredTarget_;
    std::size_t mapTargetOffset_{};
    bool mapClosePressed_{},mapDismissPressed_{};
    std::unique_ptr<data::TaskBrowser> browser_;
    std::vector<data::TaskView> rows_;
    HorizontalCardStrip traderStrip_;
    TabSelectionAnimation traderAnimation_;
    TabSelectionAnimation taskAnimation_;
    std::vector<const data::TaskTrader*> traders_;
    std::vector<const data::TaskRecord*> tasks_;
    data::GameMode mode_{data::GameMode::Pvp};
    std::string traderId_;
    std::string taskId_;
    std::vector<std::string> taskHistory_;
    std::string locale_;
    float left_{}, right_{}, height_{};
    float scroll_{}, scrollTarget_{};
    float taskScroll_{}, taskTarget_{};
    std::optional<ScrollbarGeometry> taskBarFrom_;
    std::optional<ScrollbarGeometry> detailBarFrom_;
    float taskBarProgress_{1.0F};
    float detailProgress_{1.0F};
    float textScrollTime_{};
    mutable bool textOverflowActive_{};
    bool narrow_{};
    bool stackedDetails_{};
    std::optional<float> scrollGrab_;
    std::optional<float> taskScrollGrab_;
    bool taskBackPressed_{}, taskBackHovered_{};
    std::optional<std::size_t> pressedTrader_;
    std::optional<std::size_t> pressedTask_;
    std::optional<std::size_t> hoveredTask_;
    std::optional<std::string> pressedChain_;
    std::optional<std::string> pressedRewardItem_;
    std::optional<std::string> pressedMapPoint_,hoveredMapPoint_;
    std::optional<int> pressedArrow_;
    std::optional<int> hoveredArrow_;
};

} // namespace noven::ui
