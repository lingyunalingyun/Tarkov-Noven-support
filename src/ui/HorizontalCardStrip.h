#pragma once
#include "ui/UiCanvas.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace noven::ui {
// 只维护 DIP 几何和滚动状态；内容、选择身份与资源加载由调用方负责。
// Own DIP geometry/scroll state only; the caller owns content, stable selection and resources.
class HorizontalCardStrip {
public:
    static constexpr float pitch=136, cardWidth=124;
    void Layout(D2D1_RECT_F bounds,std::size_t count) {
        bounds_=bounds; count_=count;
        target_=std::clamp(target_,0.0F,Maximum()); offset_=std::clamp(offset_,0.0F,Maximum());
    }
    float Maximum() const { return (std::max)(0.0F,count_*pitch-12-(bounds_.right-bounds_.left)); }
    bool Contains(float x,float y) const { return x>=bounds_.left && x<bounds_.right && y>=bounds_.top && y<bounds_.bottom; }
    D2D1_RECT_F Card(std::size_t i) const {
        const float x=bounds_.left+static_cast<float>(i)*pitch-offset_;
        return D2D1::RectF(x,bounds_.top,x+cardWidth,bounds_.bottom-12);
    }
    std::optional<std::size_t> Hit(float x,float y) const {
        if (!Contains(x,y) || y>=bounds_.bottom-12) return {};
        const auto i=static_cast<std::size_t>((x-bounds_.left+offset_)/pitch);
        if(i<count_ && x<Card(i).right) return i;
        return {};
    }
    D2D1_RECT_F Thumb() const {
        const float w=bounds_.right-bounds_.left;
        const float size=(std::min)(w,(std::max)(32.0F,w*w/(w+Maximum())));
        const float x=bounds_.left+(Maximum()>0?offset_/Maximum()*(w-size):0);
        return D2D1::RectF(x,bounds_.bottom-6,x+size,bounds_.bottom-2);
    }
    bool Press(float x,float y) {
        if(!Contains(x,y) || y<bounds_.bottom-12 || Maximum()<=0) return false;
        const auto thumb=Thumb(); grab_=x>=thumb.left && x<thumb.right?x-thumb.left:(thumb.right-thumb.left)/2;
        Drag(x); return true;
    }
    void Drag(float x) {
        if(!grab_) return;
        const auto thumb=Thumb(); const float travel=bounds_.right-bounds_.left-(thumb.right-thumb.left);
        offset_=target_=travel>0?std::clamp((x-bounds_.left-*grab_)/travel,0.0F,1.0F)*Maximum():0;
    }
    bool Release() { const bool was=grab_.has_value(); grab_.reset(); return was; }
    void Move(float delta) { target_=std::clamp(target_+delta,0.0F,Maximum()); }
    void Reveal(std::size_t i) {
        const float x=static_cast<float>(i)*pitch, w=bounds_.right-bounds_.left;
        if(x<target_) target_=x; else if(x+cardWidth>target_+w) target_=x+cardWidth-w;
        target_=std::clamp(target_,0.0F,Maximum());
    }
    bool Tick(float dt) {
        offset_+=(target_-offset_)*(1-std::exp(-20*std::clamp(dt,0.0F,0.05F)));
        if(std::abs(target_-offset_)<0.1F) offset_=target_;
        return Animating();
    }
    bool Animating() const { return offset_!=target_; }
    float Offset() const { return offset_; }
    bool CanMove(int direction) const { return direction<0?target_>0:target_<Maximum(); }
private:
    D2D1_RECT_F bounds_{};
    std::size_t count_{};
    float offset_{},target_{};
    std::optional<float> grab_;
};
}
