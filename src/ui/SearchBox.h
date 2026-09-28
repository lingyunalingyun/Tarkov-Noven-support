#pragma once

#include "ui/PageComponents.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <utility>

namespace noven::ui {

struct SearchTextLayout final {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    float caretX{};
    float scrollX{};
};

// 使用同一布局绘制文字和定位编辑光标；DIP 度量包含中文和比例字体。
// Share one layout for drawing and the editing caret, including CJK and proportional fonts.
[[nodiscard]] inline SearchTextLayout LayoutSearchText(IDWriteFactory& factory,
    IDWriteTextFormat& format, std::wstring_view text, float width, float height,
    std::size_t caret=std::wstring_view::npos) {
    SearchTextLayout result;
    if (FAILED(factory.CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
            &format, (std::max)(1.0F, width), height, &result.layout))) return result;
    result.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    result.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    DWRITE_HIT_TEST_METRICS hit{};
    float y{};
    result.layout->HitTestTextPosition(static_cast<UINT32>((std::min)(caret,text.size())), FALSE,
        &result.caretX, &y, &hit);
    result.scrollX = (std::max)(0.0F, result.caretX - (std::max)(0.0F, width - 1.0F));
    return result;
}

// 轻量搜索框只拥有编辑状态；目录查询由上层模型负责。
// This small search box owns input state only; the parent owns catalog queries.
class SearchBox final {
public:
    void Draw(const UiCanvas& canvas, const UiTheme& theme, D2D1_RECT_F bounds,
              std::wstring_view placeholder, bool caretVisible) const {
        canvas.Round(bounds, theme.cornerRadius, focused_ ? theme.selected : theme.surface);
        const std::wstring_view value = text_.empty() ? placeholder : std::wstring_view(text_);
        const auto viewport = D2D1::RectF(bounds.left + 16, bounds.top, bounds.right - 38, bounds.bottom);
        if (!canvas.textFactory) {
            canvas.Text(value, canvas.body, viewport,
                text_.empty() ? theme.secondaryText : theme.primaryText);
            return;
        }
        auto textLayout = LayoutSearchText(*canvas.textFactory, canvas.body, value,
            viewport.right - viewport.left, viewport.bottom - viewport.top,caret_);
        if (!textLayout.layout) return;
        // 占位文字不参与光标定位；长查询只平移绘制，不改变查询内容。
        // Placeholder text does not position the caret; scrolling never modifies the query.
        const float scroll = text_.empty() ? 0.0F : textLayout.scrollX;
        canvas.target.PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        canvas.brush.SetColor(text_.empty() ? theme.secondaryText : theme.primaryText);
        canvas.target.DrawTextLayout(D2D1::Point2F(viewport.left - scroll, viewport.top),
            textLayout.layout.Get(), &canvas.brush);
        if (focused_ && caretVisible) {
            const float x = viewport.left + (text_.empty() ? 0.0F : textLayout.caretX - scroll);
            canvas.Fill(D2D1::RectF(x, bounds.top + 9, x + 1, bounds.bottom - 9), theme.accent);
        }
        canvas.target.PopAxisAlignedClip();
    }

    [[nodiscard]] bool HitTest(D2D1_RECT_F bounds, float x, float y) const noexcept {
        return x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom;
    }
    void Focus() noexcept { focused_ = true; }
    void Blur() noexcept { focused_ = false; }
    [[nodiscard]] bool Focused() const noexcept { return focused_; }
    [[nodiscard]] const std::wstring& Text() const noexcept { return text_; }
    void SetText(std::wstring value) { text_ = std::move(value); caret_=text_.size(); selected_all_=false; }
    std::size_t Caret() const noexcept { return caret_; }
    [[nodiscard]] bool HandleKeyDown(WPARAM key, bool control) {
        if (!focused_) return false;
        if (control && key == 'A') { selected_all_ = true; return true; }
        if (key == VK_ESCAPE) { SetText({}); return true; }
        if (key == VK_LEFT || key == VK_RIGHT) {
            if(selected_all_) caret_=key==VK_LEFT?0:text_.size();
            else if(key==VK_LEFT) caret_=Previous();
            else if(caret_<text_.size()) {
                ++caret_;
                if(caret_<text_.size() && text_[caret_-1]>=0xd800 && text_[caret_-1]<=0xdbff
                    && text_[caret_]>=0xdc00 && text_[caret_]<=0xdfff) ++caret_;
            }
            selected_all_=false; return true;
        }
        if (key == VK_BACK) {
            if (selected_all_) SetText({});
            else if (caret_>0) { const auto previous=Previous(); text_.erase(previous,caret_-previous); caret_=previous; }
            selected_all_ = false;
            return true;
        }
        return false;
    }
    [[nodiscard]] bool HandleChar(wchar_t character) {
        if (!focused_ || character < L' ' || character == L'\x7f') return false;
        if (selected_all_) SetText({});
        text_.insert(caret_,1,character); ++caret_;
        selected_all_ = false;
        return true;
    }

private:
    // 左移与退格不能拆开 UTF-16 代理对。
    // Left movement and Backspace must not split UTF-16 surrogate pairs.
    std::size_t Previous() const {
        if(!caret_) return 0;
        auto index=caret_-1;
        if(index>0 && text_[index]>=0xdc00 && text_[index]<=0xdfff
            && text_[index-1]>=0xd800 && text_[index-1]<=0xdbff) --index;
        return index;
    }
    std::wstring text_;
    std::size_t caret_{};
    bool focused_{};
    bool selected_all_{};
};

} // namespace noven::ui
