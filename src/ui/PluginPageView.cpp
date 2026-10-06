#include "ui/PluginPageView.h"
#include "ui/PageTitle.h"
#include "ui/PageComponents.h"
#include "ui/NavigationButton.h"
#include <cmath>
namespace noven::ui {
void PluginPageView::SetPage(const PluginOwnedPage& page){
    if(page_.generation!=page.generation){scroll_=target_=0;Cancel();}
    dirty_=dirty_||page_.page.document!=page.page.document;page_=page;
    if(pressed_&&!page_.page.document.HasAction(*pressed_))pressed_.reset();
}
void PluginPageView::Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label){
    const float left=theme.sidebarWidth+theme.contentPadding,right=(std::max)(left+100,width-theme.contentPadding);
    viewport_=D2D1::RectF(left,100,right,(std::max)(101.0F,height-22));const float available=right-left-56;
    if(dirty_||width_!=available){
        width_=available;dirty_=false;blocks_.clear();content_=16;
        for(const auto& value:page_.page.document.blocks){
            Block block;block.type=value.type;block.action=value.action;block.top=content_;block.text=PluginWide(value.text);
            if(value.type==plugins::BlockType::KeyValue)block.text+=L": "+PluginWide(value.value);
            block.height=value.type==plugins::BlockType::Separator?1.0F:value.type==plugins::BlockType::Button?40.0F:28.0F;
            if(value.type!=plugins::BlockType::Separator&&value.type!=plugins::BlockType::Button&&factory){
                auto* format=value.type==plugins::BlockType::Heading?label:body;
                if(format&&SUCCEEDED(factory->CreateTextLayout(block.text.data(),static_cast<UINT32>(block.text.size()),format,available,100000,&block.layout))){
                    // 共享格式可能垂直居中；测量后顶对齐并收缩，保证正文位于声明式块内。
                    // Shared formats may center vertically; top-align and fit measured text inside its declarative block.
                    block.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);block.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    block.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);DWRITE_TEXT_METRICS metrics{};if(SUCCEEDED(block.layout->GetMetrics(&metrics)))block.height=(std::max)(28.0F,metrics.height);
                    block.layout->SetMaxHeight(block.height);
                }
            }
            content_+=block.height+14;blocks_.push_back(std::move(block));
        }
        content_+=2;
    }
    const float maximum=(std::max)(0.0F,content_-(viewport_.bottom-viewport_.top));scroll_=std::clamp(scroll_,0.0F,maximum);target_=std::clamp(target_,0.0F,maximum);
}
D2D1_RECT_F PluginPageView::Bounds(const Block& block) const {const float top=viewport_.top+block.top-scroll_;return D2D1::RectF(viewport_.left+16,top,viewport_.right-40,top+block.height);}
bool PluginPageView::TextInsideBlocks() const {for(const auto& block:blocks_)if(block.layout){DWRITE_TEXT_METRICS metrics{};if(FAILED(block.layout->GetMetrics(&metrics))||metrics.top<0||metrics.top+metrics.height>block.height+1)return false;}return true;}
std::optional<ScrollbarGeometry> PluginPageView::Bar() const {return MakeScrollbar(D2D1::RectF(viewport_.right-12,viewport_.top,viewport_.right,viewport_.bottom),content_,scroll_);}
void PluginPageView::Draw(const UiCanvas& canvas,const UiTheme& theme) const {
    DrawPageHeader(canvas,theme,viewport_.left,viewport_.right,PluginWide(page_.page.title));
    canvas.target.PushAxisAlignedClip(viewport_,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DrawListCardSurface(canvas,theme,D2D1::RectF(viewport_.left,viewport_.top-scroll_,viewport_.right-20,viewport_.top+(std::max)(content_,viewport_.bottom-viewport_.top)-scroll_));
    for(const auto& block:blocks_){
        const auto bounds=Bounds(block);if(bounds.bottom<viewport_.top||bounds.top>viewport_.bottom)continue;
        if(block.type==plugins::BlockType::Separator)canvas.Fill(bounds,theme.divider);
        else if(block.type==plugins::BlockType::Button)DrawTextButton(canvas,theme,bounds,block.text,hovered_==block.action,pressed_==block.action);
        else {canvas.brush.SetColor(block.type==plugins::BlockType::Badge?theme.accent:block.type==plugins::BlockType::Heading?theme.primaryText:theme.secondaryText);if(block.layout)canvas.target.DrawTextLayout(D2D1::Point2F(bounds.left,bounds.top),block.layout.Get(),&canvas.brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);}
    }
    canvas.target.PopAxisAlignedClip();DrawScrollbar(canvas,theme,{Bar(),1});
}
std::optional<std::string> PluginPageView::Hit(float x,float y) const {
    if(!HitNavigationButton(viewport_,x,y))return {};for(const auto& block:blocks_)if(block.type==plugins::BlockType::Button&&HitNavigationButton(Bounds(block),x,y))return block.action;return {};
}
std::optional<D2D1_RECT_F> PluginPageView::ActionBounds(std::string_view id) const {for(const auto& block:blocks_)if(block.action==id&&block.type==plugins::BlockType::Button)return Bounds(block);return {};}
void PluginPageView::Down(float x,float y){pressed_=Hit(x,y);if(const auto bar=Bar();bar&&HitNavigationButton(bar->thumb,x,y)){grab_=y-bar->thumb.top;target_=scroll_;}}
std::optional<std::string> PluginPageView::Up(float x,float y){const auto hit=Hit(x,y);const auto action=hit==pressed_?hit:std::nullopt;pressed_.reset();grab_.reset();return action;}
bool PluginPageView::Move(float x,float y){if(grab_)if(const auto bar=Bar()){target_=scroll_=bar->OffsetFromThumbTop(y-*grab_);return true;}const auto hit=Hit(x,y);if(hit==hovered_)return false;hovered_=hit;return true;}
bool PluginPageView::Wheel(int delta,float x,float y){if(!HitNavigationButton(viewport_,x,y)||grab_)return false;target_=std::clamp(target_-static_cast<float>(delta)/WHEEL_DELTA*66,0.0F,(std::max)(0.0F,content_-(viewport_.bottom-viewport_.top)));return Animating();}
void PluginPageView::Tick(float elapsed){scroll_+=(target_-scroll_)*(1-std::exp(-16*(std::max)(0.0F,elapsed)));if(std::abs(scroll_-target_)<.5F)scroll_=target_;}
}
