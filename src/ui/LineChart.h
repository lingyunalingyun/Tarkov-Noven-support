#pragma once
#include "ui/TabBar.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace noven::ui {
// 纯显示几何，不持有行情、网络、线程或 D2D 资源；页面在 UI 线程提供归一化顶点。
// Pure presentation geometry with no economy, network, threads or D2D resources; supply normalized vertices on the UI thread.
inline D2D1_POINT_2F SamplePolyline(const std::vector<D2D1_POINT_2F>& line, float position) {
    const float index = position * static_cast<float>(line.size() - 1);
    const auto first = static_cast<std::size_t>(index);
    const auto second = (std::min)(first + 1, line.size() - 1);
    const float t = index - first;
    return {line[first].x + (line[second].x - line[first].x) * t,
        line[first].y + (line[second].y - line[first].y) * t};
}

// 合并顶点参数以兼容不同采样数量；插值只用于绘制，提示框必须读取真实数据。
// Merge vertex parameters for unequal sample counts; interpolation is visual only, tooltips must use real data.
inline std::vector<D2D1_POINT_2F> SamplePolylineMorph(const std::vector<D2D1_POINT_2F>& from,
    const std::vector<D2D1_POINT_2F>& to, float progress) {
    if (from.empty() || progress >= 1) return to;
    if (to.empty()) return from;
    std::vector<float> positions{0, 1};
    for (const auto* line : {&from, &to})
        for (std::size_t i = 1; i + 1 < line->size(); ++i)
            positions.push_back(static_cast<float>(i) / (line->size() - 1));
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    const float t = SampleTabTransition(progress).underlineProgress;
    std::vector<D2D1_POINT_2F> result;
    for (float position : positions) {
        const auto a = SamplePolyline(from, position);
        const auto b = SamplePolyline(to, position);
        result.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
    }
    return result;
}

// 首尾采样贴边，单时间点居中；调用方负责提供按时间排序的采样。
// Fit endpoints to plot edges and center a single timestamp; callers supply time-sorted samples.
inline float ChartTimePosition(std::int64_t time, std::int64_t start, std::int64_t end) {
    return end > start ? static_cast<float>(static_cast<double>(time - start) / (end - start)) : 0.5F;
}

inline float AdvanceChartMarker(float current, float target, float seconds) {
    const float next = current + (target - current) * (1 - std::exp(-20 * std::clamp(seconds, 0.0F, 0.05F)));
    return std::abs(next - target) < 0.005F ? target : next;
}
}
