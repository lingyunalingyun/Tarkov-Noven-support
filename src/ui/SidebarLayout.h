#pragma once
#include "ui/PageRegistry.h"
#include "ui/Theme.h"
#include <d2d1helper.h>
#include <cmath>

namespace noven::ui {
struct SidebarRow final {PageDescriptor page;D2D1_RECT_F rect;bool visible{};};
struct SidebarLayout final {
    std::vector<SidebarRow> rows;
    D2D1_RECT_F navigationViewport{};
    float scroll{},maximum{},contentHeight{};
    std::optional<D2D1_RECT_F> primaryLabel,secondaryLabel,secondaryDivider,bottomDivider;
    const SidebarRow* Find(const PageId& id) const {
        for(const auto& row:rows)if(row.page.id==id)return &row;
        return nullptr;
    }
    std::optional<PageId> HitTest(float x,float y) const {
        for(const auto& row:rows)if(row.visible&&(row.page.section==PageSection::Bottom||(y>=navigationViewport.top&&y<navigationViewport.bottom))&&x>=row.rect.left&&x<row.rect.right
            &&y>=row.rect.top&&y<row.rect.bottom)return row.page.id;
        return {};
    }
};
inline SidebarLayout BuildSidebarLayout(const PageRegistry& registry,float height,const UiTheme& theme,float scroll=0) {
    SidebarLayout layout;
    height=std::isfinite(height)?(std::max)(0.0F,height):0;
    const float width=std::isfinite(theme.sidebarWidth)?(std::max)(64.0F,theme.sidebarWidth):64;
    const float rowHeight=std::isfinite(theme.navigationHeight)?(std::max)(1.0F,theme.navigationHeight):40;
    const float step=rowHeight+3;
    const auto pages=registry.Pages();
    const auto bottomCount=std::count_if(pages.begin(),pages.end(),[](const auto& page){return page.section==PageSection::Bottom;});
    const float bottomTop=(std::max)(0.0F,height-17-step*static_cast<float>(bottomCount)+3);
    // 两个管理行时仅压缩分隔间隙 3 DIP，保留现有普通页面位置及 Settings 底部位置。
    // With two management rows compress only the separator gap by 3 DIP, preserving ordinary rows and Settings anchoring.
    const bool pluginRows=std::any_of(pages.begin(),pages.end(),[](const auto& page){return page.source==PageSource::Plugin;});
    const float gap=pluginRows?9.0F:bottomCount>1?11.0F:14.0F;
    const float contentBottom=bottomCount?(std::max)(0.0F,bottomTop-gap):height;
    layout.navigationViewport=D2D1::RectF(0,(std::min)(160.0F,contentBottom),width,contentBottom);
    const auto ordinary=pages.size()-static_cast<std::size_t>(bottomCount);
    float ordinaryStep=step,sectionGap=38;
    if(pluginRows&&ordinary>1){
        // 插件行需要空间时只压缩既有间隙；内置页面单独存在时保持验收坐标。
        // Compress defined gaps only when plugin rows need space; built-in-only accepted coordinates stay unchanged.
        float deficit=(std::max)(0.0F,186+step*static_cast<float>(ordinary-1)+sectionGap+rowHeight-contentBottom);
        const float sectionCompression=(std::min)(13.0F,deficit);sectionGap-=sectionCompression;deficit-=sectionCompression;
        ordinaryStep-=(std::min)(3.0F,deficit/static_cast<float>(ordinary-1));
    }
    float top=186,bottom=bottomTop;
    bool primary{},secondary{};
    // 所有普通行属于可滚动区域；底部管理行独立锚定，裁切和命中使用同一视口。
    // Ordinary rows share a scrollable viewport; bottom management stays anchored with shared draw/hit clipping.
    for(const auto& page:pages) {
        if(page.section==PageSection::Bottom) {
            const auto rect=D2D1::RectF(12,(std::min)(height,bottom),width-12,(std::min)(height,bottom+rowHeight));
            layout.rows.push_back({page,rect,rect.bottom>rect.top&&bottom+rowHeight<=height});
            bottom+=step;continue;
        }
        if(page.section==PageSection::Primary&&!primary) {
            primary=true;layout.primaryLabel=D2D1::RectF(22,top-26,width-31,top-5);
        }
        if(page.section==PageSection::Secondary&&!secondary) {
            secondary=true;if(primary)top+=sectionGap;
            layout.secondaryDivider=D2D1::RectF(22,top-25,width-22,top-24);
            layout.secondaryLabel=D2D1::RectF(22,top-21,width-31,top-2);
        }
        layout.rows.push_back({page,D2D1::RectF(12,top,width-12,top+rowHeight),false});
        top+=ordinaryStep;
    }
    if(bottomCount)layout.bottomDivider=D2D1::RectF(22,contentBottom,width-22,contentBottom+1);
    const float end=ordinary?top-ordinaryStep+rowHeight:layout.navigationViewport.top;
    layout.maximum=(std::max)(0.0F,end-contentBottom);
    layout.scroll=std::clamp(std::isfinite(scroll)?scroll:0.0F,0.0F,layout.maximum);
    layout.contentHeight=(std::max)(0.0F,end-layout.navigationViewport.top);
    for(auto& row:layout.rows)if(row.page.section!=PageSection::Bottom){row.rect.top-=layout.scroll;row.rect.bottom-=layout.scroll;
        row.visible=layout.navigationViewport.bottom>layout.navigationViewport.top&&row.rect.bottom>layout.navigationViewport.top&&row.rect.top<layout.navigationViewport.bottom;}
    for(auto* label:{&layout.primaryLabel,&layout.secondaryLabel,&layout.secondaryDivider})if(*label){(*label)->top-=layout.scroll;(*label)->bottom-=layout.scroll;}
    return layout;
}
}
