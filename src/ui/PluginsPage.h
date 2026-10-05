#pragma once
#include "ui/UiCanvas.h"
#include "ui/Scrollbar.h"
#include "plugins/PluginDiscovery.h"
#include "plugins/PluginRuntimeManager.h"
#include "ui/TabBar.h"
#include <utility>

namespace noven::ui {
struct PluginPresentation final {std::wstring title,body,status;std::string id;bool enable{},disable{};};
enum class PluginCenterTab {Marketplace,MyPlugins};
struct PluginControlAction final {std::string id;bool enable{};};
std::wstring HostStateText(plugins::HostState state);
std::vector<PluginPresentation> PresentPlugins(const plugins::PluginSnapshot& snapshot);
std::wstring PluginStateText(plugins::PluginState state);
// 保护页面只产生显式启用/禁用请求；确认与授权由 Noven 拥有，市场不接网络。
// Protected page emits explicit enable/disable requests; Noven owns consent/grants, marketplace is offline.
class PluginsPage final {
public:
    void SetSnapshot(plugins::PluginSnapshot snapshot){snapshot_=std::move(snapshot);dirty_=true;}
    void SetRuntime(std::vector<plugins::HostSnapshot> runtime){runtime_=std::move(runtime);dirty_=true;}
    std::optional<PluginControlAction> TakeControlAction(){return std::exchange(controlAction_,{});}
    PluginCenterTab Tab() const noexcept {return tab_;}
    void SelectTab(PluginCenterTab tab);
    std::wstring MarketplaceText() const;
    std::optional<D2D1_RECT_F> ControlBounds(std::string_view id) const;
    const plugins::PluginSnapshot& Snapshot() const noexcept {return snapshot_;}
    const std::vector<PluginPresentation>& Rows() const noexcept {return rows_;}
    std::wstring EmptyText() const;
    void Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label);
    void Draw(const UiCanvas& canvas,const UiTheme& theme) const;
    void Down(float x,float y);
    bool Up(float x,float y);
    bool Move(float x,float y);
    bool Leave(){const bool changed=hovered_||hoveredTab_.has_value();hovered_=false;hoveredTab_.reset();return changed;}
    bool Wheel(int delta,float x,float y);
    void CancelDrag() noexcept {pressed_=false;grab_.reset();pressedTab_.reset();pressedControl_.reset();}
    void Blur(){scroll_=target_;tabProgress_=1;underline_=tab_==PluginCenterTab::Marketplace?0.0F:1.0F;CancelDrag();Leave();}
    bool Animating() const noexcept {return std::abs(scroll_-target_)>.01F||tabProgress_<1;}
    void Tick(float elapsed);
    D2D1_RECT_F RefreshBounds() const noexcept {return refresh_;}
    float Scroll() const noexcept {return scroll_;}
private:
    struct Card {Microsoft::WRL::ComPtr<IDWriteTextLayout> title,body;float top{},height{},titleHeight{};};
    std::optional<ScrollbarGeometry> Bar() const;
    TabBarLayout TabLayout() const {return {viewport_.left,95,135,(std::min)(150.0F,(std::max)(40.0F,(refresh_.left-viewport_.left-16)/2)),23};}
    std::array<TabBarItem<PluginCenterTab>,2> Tabs() const;
    std::optional<std::size_t> ControlAt(float x,float y) const;
    plugins::PluginSnapshot snapshot_;
    std::vector<PluginPresentation> rows_;
    std::vector<Card> cards_;
    std::string locale_;
    float width_{-1},content_{},scroll_{},target_{};
    D2D1_RECT_F viewport_{},refresh_{};
    bool dirty_{true},pressed_{},hovered_{};
    std::optional<float> grab_;
    std::vector<plugins::HostSnapshot> runtime_;
    PluginCenterTab tab_{PluginCenterTab::MyPlugins},outgoingTab_{PluginCenterTab::MyPlugins};
    float tabProgress_{1},underlineFrom_{1},underline_{1};
    std::optional<PluginCenterTab> pressedTab_,hoveredTab_;
    std::optional<std::size_t> pressedControl_;
    std::optional<PluginControlAction> controlAction_;
    std::wstring marketplaceLabel_,myLabel_;
};
}
