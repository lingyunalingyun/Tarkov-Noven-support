#pragma once
#include "ui/PageComponents.h"

namespace noven::ui {
// 统一卡片的绘制、悬停与点击边界；调用方持有列表顺序和动画偏移，长度均为 DIP。
// Share card bounds for drawing, hover and hit testing; the caller owns ordering/animation offsets, in DIPs.
struct RaidHistoryCardLayout final {
    static constexpr float RowHeight=118;
    D2D1_RECT_F bounds;

    RaidHistoryCardLayout(D2D1_RECT_F viewport,float top)
        : bounds(D2D1::RectF(viewport.left,top,viewport.right-16,top+RowHeight-10)) {}

    D2D1_RECT_F TextBounds(float top,float bottom) const noexcept {
        return D2D1::RectF(bounds.left+20,bounds.top+top,bounds.right-18,bounds.top+bottom);
    }
    D2D1_RECT_F SurfaceBounds(float extraHeight) const noexcept {
        auto surface=bounds;surface.bottom+=(std::max)(0.0F,extraHeight);return surface;
    }
};

// 只借用当前语言文本；未知值的业务语义由格式化层保留，不在卡片内推断。
// Borrow active-locale text only; the formatting layer preserves Unknown semantics, without card-level inference.
inline void DrawRaidHistoryCard(const UiCanvas& canvas,const UiTheme& theme,
    const RaidHistoryCardLayout& layout,std::wstring_view title,std::wstring_view mode,
    std::wstring_view time,bool hovered,float extraHeight=0) {
    // 展开详情与标题共用物价卡片底座，不再叠加第二张卡片。
    // Expanded details and header share the price-card surface instead of nesting a second card.
    // 与物价卡片一致，不绘制选中黄条；不能由展开高度是否为零切换选中样式。
    // Match price cards without a selection rail; zero expansion extent must not toggle selection styling.
    DrawListCardSurface(canvas,theme,layout.SurfaceBounds(extraHeight),false,hovered&&extraHeight<=0);
    canvas.Text(title,canvas.label,layout.TextBounds(10,39),theme.primaryText);
    canvas.Text(mode,canvas.smallFormat,layout.TextBounds(40,63),theme.secondaryText);
    canvas.Text(time,canvas.smallFormat,layout.TextBounds(71,93),theme.secondaryText);
}
}
