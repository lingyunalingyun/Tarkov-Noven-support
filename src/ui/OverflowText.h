#pragma once

#include "ui/UiCanvas.h"

#include <algorithm>
#include <cmath>

namespace noven::ui {

// 单份文字往末尾移动，起始停留 0.75 秒、末尾停留 2.5 秒，再重复周期。
// Move one text copy to its end, pause 0.75s at the start and 2.5s at the end, then repeat.
inline float SampleOverflowTextOffset(float overflow,float elapsed) {
    const float duration=(std::min)(6.0F,1.8F+overflow/42.0F);
    constexpr float kStartPause=0.75F;
    constexpr float kEndPause=2.5F;
    const float cycle=kStartPause+duration+kEndPause;
    const float local=std::fmod((std::max)(0.0F,elapsed),cycle);
    const float progress=std::clamp((local-kStartPause)/duration,0.0F,1.0F);
    const float eased=progress*progress*(3.0F-2.0F*progress);
    return overflow*eased;
}

// 仅在激活且确实溢出时平滑移动单行文字；调用方负责重置 elapsed。
// Smoothly move one-line text only when active and overflowing; callers reset elapsed.
inline bool DrawOverflowText(const UiCanvas& canvas,std::wstring_view value,IDWriteTextFormat& format,
    D2D1_RECT_F rect,D2D1_COLOR_F color,bool active,float elapsed) {
    if(!canvas.textFactory||value.empty()){canvas.Text(value,format,rect,color);return false;}
    Microsoft::WRL::ComPtr<IDWriteTextLayout> measure;
    if(FAILED(canvas.textFactory->CreateTextLayout(value.data(),static_cast<UINT32>(value.size()),&format,
            10000.0F,rect.bottom-rect.top,&measure))){canvas.Text(value,format,rect,color);return false;}
    DWRITE_TEXT_METRICS metrics{};
    if(FAILED(measure->GetMetrics(&metrics))||metrics.width<=rect.right-rect.left||!active){canvas.Text(value,format,rect,color);return false;}
    const float overflow=metrics.width-(rect.right-rect.left);
    const float offset=SampleOverflowTextOffset(overflow,elapsed);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if(FAILED(canvas.textFactory->CreateTextLayout(value.data(),static_cast<UINT32>(value.size()),&format,
            metrics.width+2.0F,rect.bottom-rect.top,&layout))){canvas.Text(value,format,rect,color);return false;}
    canvas.target.PushAxisAlignedClip(rect,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    canvas.brush.SetColor(color);
    canvas.target.DrawTextLayout(D2D1::Point2F(rect.left-offset,rect.top),layout.Get(),&canvas.brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
    canvas.target.PopAxisAlignedClip();
    return true;
}

} // namespace noven::ui
