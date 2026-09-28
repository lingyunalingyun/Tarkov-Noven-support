#pragma once
#include "ui/TabBar.h"
#include <vector>

namespace noven::ui {
// 复用标签模板的曲线；快速重选从当前底线和颜色接续，不绑定业务身份。
// Reuse the tab template curve; retarget from current underline/colors without business identities.
class TabSelectionAnimation {
public:
    void Select(std::size_t index,std::size_t count,bool snap=false) {
        if(count!=weights_.size() || snap) {
            weights_.assign(count,0); if(index<count) weights_[index]=1;
            position_=from_=static_cast<float>(index);target_=index;progress_=1;return;
        }
        if(index==target_) return;
        from_=position_;fromWeights_=weights_;target_=index;progress_=0;
    }
    bool Tick(float seconds) {
        if(progress_>=1) return false;
        progress_=(std::min)(1.0F,progress_+std::clamp(seconds,0.0F,.05F)/.36F);
        const auto pose=SampleTabTransition(progress_);
        position_=from_+(static_cast<float>(target_)-from_)*pose.underlineProgress;
        for(std::size_t i=0;i<weights_.size();++i)
            weights_[i]=fromWeights_[i]+((i==target_?1.0F:0.0F)-fromWeights_[i])*pose.textProgress;
        return Active();
    }
    bool Active() const { return progress_<1; }
    float Position() const { return position_; }
    float Weight(std::size_t index) const { return index<weights_.size()?weights_[index]:0; }
private:
    std::vector<float> weights_,fromWeights_;
    std::size_t target_{};
    float position_{},from_{},progress_{1};
};
}
