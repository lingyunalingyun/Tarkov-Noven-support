#pragma once
#include "ui/MapDetailIcon.h"
#include "ui/LocalImage.h"

namespace noven::ui {
// 页面拥有本地 DEV 图标；所有地图表面共用身份映射，不在绘制中下载或解码。
// Page-owned local DEV icons share identities across map surfaces, without draw-time downloads/decoding.
class MapIconImages final {
public:
    inline static constexpr std::array<std::string_view,MapCategoryCount> Categories{
        "container","loose_loot","lock","switch","stationary","mine","artillery","boss","task",
        "pmc_extract","scav_extract","coop_extract","transit","hidden_extract","sniper","spawn",
        "scav_spawn","btr","easter_egg","hidden_extract"};
    bool Load(const std::filesystem::path& directory){
        ready_=false;details_.resize(MapDetailIcons.size());categories_.resize(Categories.size());
        for(std::size_t i=0;i<MapDetailIcons.size();++i)
            if(!details_[i].Load(directory/("detail_"+std::string(MapDetailIcons[i].id)+".png")))return false;
        for(std::size_t i=0;i<Categories.size();++i)
            if(!categories_[i].Load(directory/("category_"+std::string(Categories[i])+".png")))return false;
        ready_=true;return true;
    }
    bool Ready() const noexcept{return ready_;}
    void Draw(const UiCanvas& canvas,const UiTheme& theme,MapPointCategory category,MapIconMask icons,
        D2D1_POINT_2F point,bool selected=false,float size=12) const {
        if(!ready_){DrawMapDetailIcon(canvas,theme,category,icons,point,selected,size);return;}
        const auto index=icons?static_cast<std::size_t>(std::countr_zero(icons)):details_.size();
        const auto& image=index<details_.size()?details_[index]:categories_[static_cast<std::size_t>(category)];
        if(!image.Draw(canvas.target,{point.x-size,point.y-size,point.x+size,point.y+size},canvas.brush.GetOpacity())){
            DrawMapDetailIcon(canvas,theme,category,icons,point,selected,size);return;
        }
        if(selected){canvas.brush.SetColor(theme.accent);
            canvas.target.DrawEllipse(D2D1::Ellipse(point,size+5,size+5),&canvas.brush,1.5F);}
    }
private:
    std::vector<LocalImage> details_;
    std::vector<LocalImage> categories_;
    bool ready_{};
};
}
