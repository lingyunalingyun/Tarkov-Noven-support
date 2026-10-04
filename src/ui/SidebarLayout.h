#pragma once
#include "ui/PageRegistry.h"
#include "ui/Theme.h"
#include <d2d1helper.h>
#include <cmath>

namespace noven::ui {
struct SidebarRow final {PageDescriptor page;D2D1_RECT_F rect;bool visible{};};
struct SidebarLayout final {
    std::vector<SidebarRow> rows;
    std::optional<D2D1_RECT_F> primaryLabel,secondaryLabel,secondaryDivider,bottomDivider;
    const SidebarRow* Find(const PageId& id) const {
        for(const auto& row:rows)if(row.page.id==id)return &row;
        return nullptr;
    }
    std::optional<PageId> HitTest(float x,float y) const {
        for(const auto& row:rows)if(row.visible&&x>=row.rect.left&&x<row.rect.right
            &&y>=row.rect.top&&y<row.rect.bottom)return row.page.id;
        return {};
    }
};
inline SidebarLayout BuildSidebarLayout(const PageRegistry& registry,float height,const UiTheme& theme) {
    SidebarLayout layout;
    height=std::isfinite(height)?(std::max)(0.0F,height):0;
    const float width=std::isfinite(theme.sidebarWidth)?(std::max)(64.0F,theme.sidebarWidth):64;
    const float rowHeight=std::isfinite(theme.navigationHeight)?(std::max)(1.0F,theme.navigationHeight):40;
    const float step=rowHeight+3;
    const auto pages=registry.Pages();
    const auto bottomCount=std::count_if(pages.begin(),pages.end(),[](const auto& page){return page.section==PageSection::Bottom;});
    const float bottomTop=(std::max)(0.0F,height-17-step*static_cast<float>(bottomCount)+3);
    const float contentBottom=bottomCount?(std::max)(0.0F,bottomTop-14):height;
    float top=186,bottom=bottomTop;
    bool primary{},secondary{};
    // 普通尺寸保持原有 43 DIP 行距和分区间隔；小高度裁掉不完整行，底部区不被覆盖。
    // Preserve accepted 43-DIP rhythm at normal size; clip incomplete rows instead of overlapping bottom pages.
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
            secondary=true;if(primary)top+=38;
            layout.secondaryDivider=D2D1::RectF(22,top-25,width-22,top-24);
            layout.secondaryLabel=D2D1::RectF(22,top-21,width-31,top-2);
        }
        layout.rows.push_back({page,D2D1::RectF(12,top,width-12,top+rowHeight),top+rowHeight<=contentBottom});
        top+=step;
    }
    if(bottomCount)layout.bottomDivider=D2D1::RectF(22,contentBottom,width-22,contentBottom+1);
    if(layout.primaryLabel&&layout.primaryLabel->bottom>contentBottom)layout.primaryLabel.reset();
    if(layout.secondaryLabel&&layout.secondaryLabel->bottom>contentBottom){layout.secondaryLabel.reset();layout.secondaryDivider.reset();}
    return layout;
}
}
