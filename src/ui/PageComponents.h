#pragma once

#include "ui/UiCanvas.h"
#include "ui/Theme.h"
#include <algorithm>

namespace noven::ui {

// 绘制函数只借用 UI 线程资源与文本；不保存业务对象，也不执行网络/磁盘操作。
// Drawing helpers borrow UI-thread resources and text; they retain no business objects and perform no I/O.
inline void DrawPageHeader(const UiCanvas& canvas, const UiTheme& theme,
    float left, float right, std::wstring_view chinese, std::wstring_view english) {
    canvas.Text(chinese, canvas.title, D2D1::RectF(left, 20, left + 110, 59), theme.primaryText);
    canvas.Text(english, canvas.body, D2D1::RectF(left + 120, 26, right, 59), theme.secondaryText);
    canvas.Fill(D2D1::RectF(left, 70, right, 71), theme.divider);
}

// 借用文本在 DrawItemCard 返回前必须有效；位图属于当前 Direct2D 渲染目标。
// Text must live until DrawItemCard returns; the bitmap must belong to the current Direct2D target.
struct ItemCardView final {
    std::wstring_view title;
    std::wstring_view detail;
    std::wstring_view leftValue;
    std::wstring_view rightValue;
    ID2D1Bitmap* image{};
};

// 等比例居中，不拉伸物品图片；输入与输出均为 DIP。
// Center with preserved aspect ratio; both input bounds and output use DIPs.
[[nodiscard]] inline D2D1_RECT_F FitImage(D2D1_SIZE_F size, D2D1_RECT_F bounds) noexcept {
    if (size.width <= 0 || size.height <= 0) return D2D1::RectF(bounds.left, bounds.top, bounds.left, bounds.top);
    const float fit = (std::min)((bounds.right - bounds.left) / size.width,
                               (bounds.bottom - bounds.top) / size.height);
    const float w = size.width * fit;
    const float h = size.height * fit;
    const float cx = (bounds.left + bounds.right) / 2;
    const float cy = (bounds.top + bounds.bottom) / 2;
    return D2D1::RectF(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
}

inline void DrawItemCard(const UiCanvas& canvas, const UiTheme& theme,
                        D2D1_RECT_F bounds, const ItemCardView& item) {
    const float x = bounds.left, top = bounds.top, right = bounds.right;
    canvas.Round(bounds, theme.cornerRadius, theme.surface);
    canvas.Round(D2D1::RectF(x + 16, top + 15, x + 96, top + 103), 6.0F, theme.background);
    if (item.image) {
        canvas.target.DrawBitmap(item.image, FitImage(item.image->GetSize(),
            D2D1::RectF(x + 20, top + 19, x + 92, top + 99)),
            canvas.brush.GetOpacity(), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        canvas.Text(L"图片暂无", canvas.smallFormat,
            D2D1::RectF(x + 26, top + 43, x + 92, top + 75), theme.secondaryText);
    }
    canvas.Text(item.title, canvas.label,
        D2D1::RectF(x + 112, top + 10, right - 18, top + 39), theme.primaryText);
    canvas.Text(item.detail, canvas.smallFormat,
        D2D1::RectF(x + 112, top + 40, right - 18, top + 63), theme.secondaryText);
    canvas.Text(item.leftValue, canvas.body,
        D2D1::RectF(x + 112, top + 71, x + 300, top + 93), theme.primaryText);
    canvas.Text(item.rightValue, canvas.body,
        D2D1::RectF(x + 310, top + 71, right - 18, top + 93), theme.primaryText);
}

inline void DrawEmptyState(const UiCanvas& canvas, const UiTheme& theme,
    D2D1_RECT_F bounds, float textRight, std::wstring_view title,
    std::wstring_view description, std::wstring_view english) {
    canvas.Round(bounds, theme.cornerRadius, theme.surface);
    canvas.Text(title, canvas.label, D2D1::RectF(bounds.left + 25, bounds.top + 23,
        textRight, bounds.top + 55), theme.primaryText);
    canvas.Text(description, canvas.body, D2D1::RectF(bounds.left + 25, bounds.top + 62,
        textRight, bounds.top + 94), theme.secondaryText);
    canvas.Text(english, canvas.smallFormat, D2D1::RectF(bounds.left + 25, bounds.top + 101,
        textRight, bounds.top + 127), theme.secondaryText);
}

// 内容缩放/透明度只在此作用域生效；裁剪由页面设置，退出后恢复原状态。
// Scale/opacity apply only within this scope; the page owns clipping and original state is restored on exit.
class ScopedContentTransition final {
public:
    ScopedContentTransition(const UiCanvas& canvas, D2D1_POINT_2F center, float opacity, float scale)
        : canvas_(canvas), opacity_(canvas.brush.GetOpacity()) {
        canvas_.target.GetTransform(&transform_);
        canvas_.target.SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, center) * transform_);
        canvas_.brush.SetOpacity(opacity_ * opacity);
    }
    ~ScopedContentTransition() {
        canvas_.brush.SetOpacity(opacity_);
        canvas_.target.SetTransform(transform_);
    }
    ScopedContentTransition(const ScopedContentTransition&) = delete;
    ScopedContentTransition& operator=(const ScopedContentTransition&) = delete;
private:
    const UiCanvas& canvas_;
    float opacity_{};
    D2D1_MATRIX_3X2_F transform_{};
};

} // namespace noven::ui
