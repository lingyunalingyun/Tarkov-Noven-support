#pragma once
#include "ui/PageComponents.h"

namespace noven::ui {
// 统一卡片的绘制、悬停与点击边界；调用方持有列表顺序和动画偏移，长度均为 DIP。
// Share card bounds for drawing, hover and hit testing; the caller owns ordering/animation offsets, in DIPs.
struct RaidHistoryCardLayout final {
    static constexpr float RowHeight=104;
    D2D1_RECT_F bounds;

    RaidHistoryCardLayout(D2D1_RECT_F viewport,float top)
        : bounds(D2D1::RectF(viewport.left,top,viewport.right-16,top+RowHeight-8)) {}

    D2D1_RECT_F TextBounds(float top,float bottom) const noexcept {
        return D2D1::RectF(bounds.left+12,bounds.top+top,bounds.right-6,bounds.top+bottom);
    }
};

// 只借用当前语言文本；未知值的业务语义由格式化层保留，不在卡片内推断。
// Borrow active-locale text only; the formatting layer preserves Unknown semantics, without card-level inference.
inline void DrawRaidHistoryCard(const UiCanvas& canvas,const UiTheme& theme,
    const RaidHistoryCardLayout& layout,std::wstring_view title,std::wstring_view mode,
    std::wstring_view time,bool selected,bool hovered) {
    const auto b=layout.bounds;
    canvas.Round(b,theme.cornerRadius,selected?theme.selected:hovered?theme.hover:theme.surface);
    if(selected)canvas.Round(D2D1::RectF(b.left,b.top+12,b.left+3,b.bottom-12),1.5F,theme.accent);
    canvas.Text(title,canvas.label,layout.TextBounds(5,30),theme.primaryText);
    canvas.Text(mode,canvas.smallFormat,layout.TextBounds(30,52),theme.accent);
    canvas.Text(time,canvas.smallFormat,layout.TextBounds(54,88),theme.secondaryText);
}
}
