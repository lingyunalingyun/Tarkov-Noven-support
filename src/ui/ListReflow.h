#pragma once
#include <algorithm>
#include <cmath>

namespace noven::ui {
struct ListReflowPose { float slot; float opacity; };

// 先淡出被过滤项，再弹性补位；调用方按稳定 ID 保留当前姿态以支持连续输入。
// Fade filtered rows first, then spring into place; callers retain poses by stable ID on interruption.
inline ListReflowPose SampleListReflow(float from, float to, float opacity,
    bool retained, float progress) noexcept {
    const float fade = std::clamp(progress / 0.28F, 0.0F, 1.0F);
    const float t = std::clamp((progress - 0.28F) / 0.72F, 0.0F, 1.0F);
    const float spring = t >= 1 ? 1 : 1 - std::exp(-7 * t) * std::cos(8 * t);
    const float smooth = t * t * (3 - 2 * t);
    return {from + (to - from) * spring,
        retained ? opacity + (1 - opacity) * smooth : opacity * (1 - fade * fade * (3 - 2 * fade))};
}
}
