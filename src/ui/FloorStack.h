#pragma once
#include "ui/UiCanvas.h"
#include "ui/Theme.h"
#include <array>
#include <cmath>
#include <optional>
#include <span>

namespace noven::ui {
struct FloorPlate final {
    std::array<D2D1_POINT_2F,4> vertices;
    D2D1_POINT_2F anchor;
    D2D1_RECT_F label;
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
    FloorPlate Plate(std::size_t index,float pull=0) const noexcept {
        const float x=origin.x-static_cast<float>(index)*width*.13F+pull;
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
    void Draw(const UiCanvas& canvas,const UiTheme& theme,std::span<const std::wstring_view> labels,
        std::optional<std::size_t> selected,std::optional<std::size_t> hovered) const {
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
            canvas.brush.SetColor(selected==i?theme.selected:hovered==i?theme.hover:theme.surface);
            canvas.target.FillGeometry(path.Get(),&canvas.brush);
            canvas.brush.SetColor(selected==i?theme.accent:theme.divider);
            canvas.target.DrawGeometry(path.Get(),&canvas.brush,selected==i?2.0F:1.0F);
            const float separation=selected?std::abs(static_cast<float>(i)-static_cast<float>(*selected))*width*.2F:100;
            const bool showLabel=width>=150||selected==i||((i==0||i==count-1)&&separation>=24);
            if(i<labels.size()&&showLabel){auto label=plate.label;
                if(label.bottom-label.top<24)label.bottom=label.top+24;
                canvas.Text(labels[i],canvas.body,label,selected==i?theme.accent:theme.secondaryText);}
        }
    }
};
}
