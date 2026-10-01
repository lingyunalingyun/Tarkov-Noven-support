#pragma once
#include "ui/UiCanvas.h"
#include "ui/Theme.h"
#include "ui/LocalImage.h"
#include <array>
#include <cmath>
#include <optional>
#include <span>

namespace noven::ui {
struct FloorPlate final {
    std::array<D2D1_POINT_2F,4> vertices;
    D2D1_POINT_2F anchor;
    D2D1_RECT_F label;
    D2D1::Matrix3x2F PreviewTransform() const noexcept {
        const auto a=vertices[3],b=vertices[0],c=vertices[2];
        return {b.x-a.x,b.y-a.y,c.x-a.x,c.y-a.y,a.x,a.y};
    }
    bool Contains(D2D1_POINT_2F p) const noexcept {
        for(std::size_t i=0;i<4;++i){
            const auto a=vertices[i],b=vertices[(i+1)%4];
            if((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x)<0) return false;
        }
        return true;
    }
};

// 楼层索引只描述传入顺序，不保存业务身份；绘制与命中使用相同几何。
// Indices describe caller order, not business identity; painting and hits share geometry.
class FloorStack final {
public:
    D2D1_POINT_2F origin{};
    float width{250};
    std::size_t count{};
    float stagger{.13F};
    FloorPlate Plate(std::size_t index,float pull=0) const noexcept {
        const float x=origin.x-static_cast<float>(index)*width*stagger+pull;
        const float y=origin.y+static_cast<float>(index)*width*.20F-pull;
        return {{{{x+width*.55F,y},{x+width,y+width*.18F},
            {x+width*.45F,y+width*.32F},{x,y+width*.14F}}},
            {x+width*.45F,y+width*.32F},{x-52,y+width*.07F,x-4,y+width*.23F}};
    }
    std::optional<std::size_t> Hit(D2D1_POINT_2F point) const noexcept {
        // 上层最后绘制，因此重叠区域优先命中上层。
        // Upper plates paint last and win overlapping hits.
        for(std::size_t i=0;i<count;++i)if(Plate(i).Contains(point))return i;
        return std::nullopt;
    }
    bool LabelVisible(std::size_t index,std::optional<std::size_t> selected) const noexcept {
        // 展开后只标注选中层；概览保持大尺寸全部标注或小尺寸首尾标注。
        // Selection labels only the active floor; overview labels all large plates or compact endpoints.
        return selected?selected==index:width>=150||index==0||index==count-1;
    }
    void Draw(const UiCanvas& canvas,const UiTheme& theme,std::span<const std::wstring_view> labels,
        std::optional<std::size_t> selected,std::optional<std::size_t> hovered,
        std::span<const LocalImage* const> previews={},std::span<const LocalImage* const> overlays={},float selectedPosition=-1) const {
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;canvas.target.GetFactory(&factory);
        for(std::size_t n=count;n>0;--n){
            const auto i=n-1;const auto plate=Plate(i);
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if(FAILED(factory->CreatePathGeometry(&path))||FAILED(path->Open(&sink)))continue;
            sink->BeginFigure(plate.vertices[0],D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(plate.vertices.data()+1,3);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if(FAILED(sink->Close()))continue;
            const float active=selected?std::max(0.0F,1-std::abs(static_cast<float>(i)-(selectedPosition<0?static_cast<float>(*selected):selectedPosition))):0;
            const auto blend=[&](D2D1_COLOR_F a,D2D1_COLOR_F b){return D2D1_COLOR_F{a.r+(b.r-a.r)*active,a.g+(b.g-a.g)*active,a.b+(b.b-a.b)*active,a.a+(b.a-a.a)*active};};
            canvas.brush.SetColor(blend(hovered==i?theme.hover:theme.surface,theme.selected));
            canvas.target.FillGeometry(path.Get(),&canvas.brush);
            // 单位矩形仿射映射为楼层平面；复用预览缓存，不读取高清块。
            // Affine-map a unit rectangle onto the plate using cached previews, not detail tiles.
            if(i<previews.size()&&previews[i]){
                D2D1_MATRIX_3X2_F original;canvas.target.GetTransform(&original);
                canvas.target.SetTransform(plate.PreviewTransform()*original);
                previews[i]->Draw(canvas.target,{0,0,1,1},canvas.brush.GetOpacity()*.85F);
                if(i<overlays.size()&&overlays[i])overlays[i]->Draw(canvas.target,{0,0,1,1},canvas.brush.GetOpacity()*.85F);
                canvas.target.SetTransform(original);
            }
            canvas.brush.SetColor(blend(theme.divider,theme.accent));
            canvas.target.DrawGeometry(path.Get(),&canvas.brush,1+active);
            if(i<labels.size()&&LabelVisible(i,selected)){auto label=plate.label;
                if(label.bottom-label.top<24)label.bottom=label.top+24;
                canvas.Text(labels[i],canvas.body,label,selected==i?theme.accent:theme.secondaryText);}
        }
    }
};
}
