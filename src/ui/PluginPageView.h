#pragma once
#include "ui/PluginPages.h"
#include "ui/UiCanvas.h"
#include "ui/Scrollbar.h"
#include "ui/Theme.h"
namespace noven::ui {
// Noven 拥有全部布局、样式和命中矩形；插件文档不能提供原生句柄或绘制指令。
// Noven owns all layout/style/hit rectangles; documents expose neither native handles nor drawing commands.
class PluginPageView final {
public:
    void SetPage(const PluginOwnedPage& page);
    void Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label);
    void Draw(const UiCanvas& canvas,const UiTheme& theme) const;
    void Down(float x,float y);
    std::optional<std::string> Up(float x,float y);
    bool Move(float x,float y);
    bool Wheel(int delta,float x,float y);
    void Cancel(){pressed_.reset();hovered_.reset();grab_.reset();}
    void Tick(float elapsed);
    bool Animating() const {return std::abs(scroll_-target_)>.01F;}
    std::optional<D2D1_RECT_F> ActionBounds(std::string_view id) const;
private:
    struct Block {plugins::BlockType type;std::wstring text;std::string action;float top{},height{};Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;};
    D2D1_RECT_F Bounds(const Block& block) const;
    std::optional<std::string> Hit(float x,float y) const;
    std::optional<ScrollbarGeometry> Bar() const;
    PluginOwnedPage page_;
    std::vector<Block> blocks_;
    bool dirty_{true};float width_{-1},content_{},scroll_{},target_{};D2D1_RECT_F viewport_{};
    std::optional<std::string> pressed_,hovered_;std::optional<float> grab_;
};
}
