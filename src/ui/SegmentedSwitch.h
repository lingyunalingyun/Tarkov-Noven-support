#pragma once
#include "ui/TabBar.h"

namespace noven::ui {
// 双选项控件只拥有视觉状态；业务身份与切换由调用方管理，复用页面动画时钟。
// Two-option control owns visual state only; callers own identities and share the page animation clock.
class SegmentedSwitch final {
public:
    bool Animating() const noexcept{return progress_<1;}
    float Position() const noexcept {
        return from_+(to_-from_)*SampleTabTransition(progress_*.25F).textProgress;
    }
    void Reset(bool second) noexcept{from_=to_=second?1.0F:0.0F;progress_=1;}
    void Select(bool second) noexcept{
        const float target=second?1.0F:0.0F;
        if(to_==target)return;
        from_=Position();to_=target;progress_=0;
    }
    void Tick(float elapsed) noexcept{progress_=std::min(1.0F,progress_+std::clamp(elapsed,0.0F,.05F)/.24F);}
    static D2D1_RECT_F Button(D2D1_RECT_F bounds,bool second) noexcept{
        const float width=(bounds.right-bounds.left-4)*.5F;
        const float left=bounds.left+(second?width+4:0);
        return {left,bounds.top,left+width,bounds.bottom};
    }
    void Draw(const UiCanvas& canvas,const UiTheme& theme,D2D1_RECT_F bounds,
              const std::array<std::wstring_view,2>& labels) const {
        for(bool second:{false,true})canvas.Round(Button(bounds,second),theme.cornerRadius,theme.surface);
        const auto first=Button(bounds,false),last=Button(bounds,true);
        const float x=Position()*(last.left-first.left);
        canvas.Round({first.left+x,first.top,first.right+x,first.bottom},theme.cornerRadius,theme.selected);
        for(std::size_t i=0;i<labels.size();++i){
            const float weight=1-std::abs(Position()-static_cast<float>(i));
            const auto a=theme.secondaryText,b=theme.accent;
            canvas.CenteredText(labels[i],canvas.smallFormat,Button(bounds,i==1),
                {a.r+(b.r-a.r)*weight,a.g+(b.g-a.g)*weight,a.b+(b.b-a.b)*weight,a.a+(b.a-a.a)*weight});
        }
    }
private:
    float from_{},to_{},progress_{1};
};
}
