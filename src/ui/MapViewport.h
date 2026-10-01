#pragma once
#include <d2d1.h>
#include <algorithm>
#include <cmath>

namespace noven::ui {
// 逻辑地图坐标到 DIP 的变换；不拥有图片、标识数据或窗口资源。
// Logical map-to-DIP transform; owns no images, markers or window resources.
class MapViewport final {
public:
    explicit MapViewport(D2D1_SIZE_F world):world_(world){}
    void SetBounds(D2D1_RECT_F bounds) noexcept {
        if(bounds.left==bounds_.left&&bounds.top==bounds_.top&&bounds.right==bounds_.right&&bounds.bottom==bounds_.bottom)return;
        const auto center=ToMap(Center());bounds_=bounds;
        if(!initialized_){Fit();return;}
        scale_=std::clamp(scale_,MinimumScale(),MaximumScale());Focus(center);
    }
    D2D1_RECT_F Bounds() const noexcept{return bounds_;}
    float Scale() const noexcept{return scale_;}
    float MinimumScale() const noexcept{return FitScale()*.65F;}
    float MaximumScale() const noexcept{return FitScale()*32;}
    D2D1_POINT_2F ToScreen(D2D1_POINT_2F map) const noexcept{return {offset_.x+map.x*scale_,offset_.y+map.y*scale_};}
    D2D1_POINT_2F ToMap(D2D1_POINT_2F screen) const noexcept{return {(screen.x-offset_.x)/scale_,(screen.y-offset_.y)/scale_};}
    void Fit() noexcept {
        scale_=FitScale();initialized_=true;Focus({world_.width*.5F,world_.height*.5F});
    }
    void Focus(D2D1_POINT_2F map) noexcept {
        const auto center=Center();offset_={center.x-map.x*scale_,center.y-map.y*scale_};ClampPan();
    }
    void ZoomAt(D2D1_POINT_2F cursor,float steps) noexcept {
        if(!std::isfinite(steps))return;
        const auto anchor=ToMap(cursor);
        scale_=std::clamp(scale_*std::pow(1.18F,std::clamp(steps,-40.0F,40.0F)),MinimumScale(),MaximumScale());
        offset_={cursor.x-anchor.x*scale_,cursor.y-anchor.y*scale_};ClampPan();
    }
    void Pan(D2D1_POINT_2F delta) noexcept {offset_.x+=delta.x;offset_.y+=delta.y;ClampPan();}
private:
    float FitScale() const noexcept {
        return std::max(.001F,std::min((bounds_.right-bounds_.left)*.9F/world_.width,
            (bounds_.bottom-bounds_.top)*.9F/world_.height));
    }
    D2D1_POINT_2F Center() const noexcept{return {(bounds_.left+bounds_.right)*.5F,(bounds_.top+bounds_.bottom)*.5F};}
    void ClampPan() noexcept {
        // 至少保留四分之一可见区域，或较小地图的一半；不能拖到完全不可见。
        // Retain a quarter of the viewport, or half a smaller map; never lose the whole map.
        const float visibleX=std::min((bounds_.right-bounds_.left)*.25F,world_.width*scale_*.5F);
        const float visibleY=std::min((bounds_.bottom-bounds_.top)*.25F,world_.height*scale_*.5F);
        offset_.x=std::clamp(offset_.x,bounds_.left+visibleX-world_.width*scale_,bounds_.right-visibleX);
        offset_.y=std::clamp(offset_.y,bounds_.top+visibleY-world_.height*scale_,bounds_.bottom-visibleY);
    }
    D2D1_SIZE_F world_;
    D2D1_RECT_F bounds_{};
    D2D1_POINT_2F offset_{};
    float scale_{1};
    bool initialized_{};
};
}
