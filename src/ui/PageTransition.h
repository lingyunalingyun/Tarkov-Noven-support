#pragma once
#include "ui/TabBar.h"
#include <chrono>
#include <optional>

namespace noven::ui {
// 只保存旧页面身份与动画时间，不拥有页面、数据服务或渲染资源。
// Own outgoing identity and animation timing only, never pages, services or rendering resources.
template<typename Id> class PageTransition final {
public:
    using Clock=std::chrono::steady_clock;
    struct Pose { Id page; float opacity,shift; };
    void Start(Id from,Clock::time_point now=Clock::now()) {
        const auto pose=SampleTabTransition(progress_);
        // 连续切换延续当前透明度，最多保留一个旧页面。
        // Rapid retargeting continues current opacity and retains at most one outgoing page.
        if(progress_>=.42F || !outgoing_) {
            outgoing_=from; fromOpacity_=progress_>=1?1:pose.incomingOpacity;
        } else fromOpacity_*=pose.outgoingOpacity;
        progress_=0; started_=now;
    }
    void Tick(Clock::time_point now=Clock::now()) {
        if(!Active()) return;
        progress_=std::clamp(std::chrono::duration<float>(now-started_).count()/.36F,0.0F,1.0F);
        if(!Active()) outgoing_.reset();
    }
    bool Active() const { return progress_<1; }
    bool ShowingOutgoing(Id id) const { return progress_<.42F && outgoing_==id; }
    Pose Sample(Id active) const {
        const auto pose=SampleTabTransition(progress_);
        const bool old=progress_<.42F && outgoing_.has_value();
        return {old?*outgoing_:active,old?fromOpacity_*pose.outgoingOpacity:pose.incomingOpacity,
            old?-8*(1-pose.outgoingOpacity):8*(1-pose.incomingOpacity)};
    }
private:
    std::optional<Id> outgoing_;
    float progress_{1},fromOpacity_{1};
    Clock::time_point started_{};
};
}
