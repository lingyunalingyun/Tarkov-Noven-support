#pragma once
#include "data/PriceHistory.h"
#include "ui/PageComponents.h"
#include "ui/TabBar.h"
#include "ui/ExpandableCard.h"
#include "ui/LineChart.h"
#include <array>
#include <cmath>
#include <ctime>

namespace noven::ui {
inline constexpr float kPriceDetailsHeight = 310;
inline constexpr std::array kHistoryRanges{7, 30, 90, 365};
struct PriceDetailsState : ExpandableCardState {
    std::string id;
    int days{30};
    data::HistorySnapshot history;
    std::optional<D2D1_POINT_2F> pointer;
    std::optional<float> hoverIndex;
    float hoverTarget{};
    int previousDays{30};
    float tabProgress{1};
    float underlineIndex{1};
    float underlineFrom{1};
    float chartProgress{1};
    std::vector<D2D1_POINT_2F> chartFrom;
    std::vector<D2D1_POINT_2F> chartTo;
};

// 行情适配层只提供业务尺寸；通用组件不依赖物品或历史服务。
// The history adapter supplies business dimensions; reusable components know no items or history services.
inline float DetailsScrollExtra(const PriceDetailsState& state, std::size_t count,
    float rowHeight, float viewportHeight) {
    return state.id.empty() ? 0 : state.ScrollExtra(count, rowHeight, viewportHeight, kPriceDetailsHeight);
}
inline void AdvanceDetailsExpansion(PriceDetailsState& state, float seconds) {
    state.Advance(seconds, kPriceDetailsHeight);
}
inline std::vector<D2D1_POINT_2F> HistoryLinePose(const PriceDetailsState& state) {
    return SamplePolylineMorph(state.chartFrom, state.chartTo, state.chartProgress);
}
inline float HistoryTimePosition(std::int64_t time, std::int64_t start, std::int64_t end) {
    return ChartTimePosition(time, start, end);
}

inline void SetHistoryChart(PriceDetailsState& state, const data::HistorySnapshot& snapshot) {
    if (snapshot.loading && snapshot.points.empty()) return;
    auto current = HistoryLinePose(state);
    std::vector<D2D1_POINT_2F> target;
    if (const auto extreme = data::HistoryExtrema(snapshot.points)) {
        const auto start = snapshot.points.front().timeMs;
        const auto end = snapshot.points.back().timeMs;
        for (const auto& point : snapshot.points)
            target.push_back({HistoryTimePosition(point.timeMs, start, end),
                static_cast<float>(static_cast<double>(point.price) / extreme->second)});
    }
    if (target.size() == state.chartTo.size()
        && std::equal(target.begin(), target.end(), state.chartTo.begin(),
            [](auto a, auto b) { return a.x == b.x && a.y == b.y; })) return;
    state.chartFrom = std::move(current);
    state.chartTo = std::move(target);
    state.chartProgress = state.chartFrom.empty() || state.chartTo.empty() ? 1.0F : 0.0F;
}

// 时间标签和折线复用现有弹性曲线；过渡只改变显示姿态，不改变历史数值。
// Range tabs and chart reuse the existing spring curve; transitions affect presentation, never historical values.
inline void AdvanceHistoryTransition(PriceDetailsState& state, float seconds) {
    state.tabProgress = (std::min)(1.0F, state.tabProgress + seconds / 0.45F);
    state.chartProgress = (std::min)(1.0F, state.chartProgress + seconds / 0.55F);
    const auto found = std::find(kHistoryRanges.begin(), kHistoryRanges.end(), state.days);
    const float target = static_cast<float>(found - kHistoryRanges.begin());
    state.underlineIndex = state.underlineFrom + (target - state.underlineFrom)
        * SampleTabTransition(state.tabProgress).underlineProgress;
}
// 只平滑视觉索引，价格提示始终读取真实目标采样；连续移动从当前姿态接续。
// Smooth only the visual index; tooltip prices use the real target sample and retargeting preserves the current pose.
inline float AdvanceHistoryHover(float current, float target, float seconds) {
    return AdvanceChartMarker(current, target, seconds);
}
inline D2D1_RECT_F HistoryPlotRect(D2D1_RECT_F bounds) {
    return D2D1::RectF(bounds.left + 20, bounds.top + 117, bounds.right - 20, bounds.top + 220);
}
inline std::wstring HistoryDate(std::int64_t ms) {
    const auto seconds = static_cast<time_t>(ms / 1000);
    tm local{}; localtime_s(&local, &seconds);
    wchar_t text[32]{};
    wcsftime(text, std::size(text), L"%Y-%m-%d", &local);
    return text;
}

struct HistoryHover {
    data::HistoryPoint point;
    std::int64_t low{}, average{}, high{};
    std::size_t count{};
};

// 整个图表区域可悬浮；按 X 轴时间距离选最近采样，统计和日期跟随该点而非鼠标日期。
// Hover anywhere in the plot; select the nearest sample by time/X distance and report that sample's local day.
inline std::optional<HistoryHover> HitHistory(const data::HistorySnapshot& snapshot,
    D2D1_RECT_F plot, D2D1_POINT_2F pointer) {
    if (snapshot.points.empty() || pointer.x < plot.left || pointer.x > plot.right
        || pointer.y < plot.top || pointer.y > plot.bottom) return std::nullopt;
    const auto start = snapshot.points.front().timeMs;
    const auto end = snapshot.points.back().timeMs;
    const auto time = start + static_cast<std::int64_t>(
        (pointer.x - plot.left) / (plot.right - plot.left) * (end - start));
    const auto nearest = std::min_element(snapshot.points.begin(), snapshot.points.end(),
        [time](const auto& a, const auto& b) {
            return std::abs(a.timeMs - time) < std::abs(b.timeMs - time);
        });
    const auto seconds = static_cast<time_t>(nearest->timeMs / 1000);
    tm local{}; localtime_s(&local, &seconds);
    local.tm_hour = local.tm_min = local.tm_sec = 0; local.tm_isdst = -1;
    const auto dayStart = static_cast<std::int64_t>(mktime(&local)) * 1000;
    ++local.tm_mday; local.tm_isdst = -1;
    const auto dayEnd = static_cast<std::int64_t>(mktime(&local)) * 1000;
    std::optional<HistoryHover> result = HistoryHover{*nearest, nearest->price, 0, nearest->price, 0};
    long double sum = 0;
    for (const auto& point : snapshot.points) {
        if (point.timeMs < dayStart || point.timeMs >= dayEnd) continue;
        result->low = (std::min)(result->low, point.price);
        result->high = (std::max)(result->high, point.price);
        sum += point.price; ++result->count;
    }
    if (result) result->average = static_cast<std::int64_t>(sum / result->count + 0.5L);
    return result;
}

inline std::wstring HistoryMoney(std::int64_t value) {
    auto result = std::to_wstring(value);
    for (int i = static_cast<int>(result.size()) - 3; i > 0; i -= 3) result.insert(i, L",");
    return L"₽" + result;
}
inline D2D1_RECT_F HistoryRangeRect(D2D1_RECT_F bounds, std::size_t index) {
    const float width = (std::min)(90.0F, (bounds.right - bounds.left - 40) / 4);
    const float left = bounds.left + 20 + index * width;
    return D2D1::RectF(left, bounds.top + 46, left + width - 6, bounds.top + 76);
}

// 横轴保留真实时间间隔；仅连接原始采样，不聚合、不平滑，也不延伸到无数据的两端。
// Preserve real time spacing; connect raw samples without aggregation, smoothing or endpoint extrapolation.
inline D2D1_POINT_2F HistoryPlotPoint(const data::HistoryPoint& point,
    std::int64_t start, std::int64_t end, std::int64_t maximum, D2D1_RECT_F plot) {
    const float time = HistoryTimePosition(point.timeMs, start, end);
    const float price = static_cast<float>(static_cast<double>(point.price) / maximum);
    return D2D1::Point2F(plot.left + time * (plot.right - plot.left),
        plot.bottom - price * (plot.bottom - plot.top));
}

inline void DrawPriceHistory(const UiCanvas& canvas, const UiTheme& theme,
    D2D1_RECT_F bounds, const PriceDetailsState& details, float viewportTop) {
    const auto rangeBounds = bounds;
    canvas.Text(Tr(TextKey::HistoryTitle), canvas.label,
        D2D1::RectF(bounds.left + 20, bounds.top + 8, bounds.right - 20, bounds.top + 40),
        theme.primaryText);
    std::array<std::wstring, 4> labels;
    std::array<TabBarItem<int>, 4> tabs;
    for (std::size_t i = 0; i < kHistoryRanges.size(); ++i) {
        labels[i] = UiLocalization().Format(TextKey::HistoryDays,
            {{L"days", std::to_wstring(kHistoryRanges[i])}});
        tabs[i] = {kHistoryRanges[i], labels[i]};
    }
    const float tabWidth = (std::min)(90.0F, (rangeBounds.right - rangeBounds.left - 40) / 4);
    DrawTabBar(canvas, theme, canvas.smallFormat, tabs,
        {rangeBounds.left + 20, rangeBounds.top + 46, rangeBounds.top + 76, tabWidth, 22},
        details.days, std::optional<int>{}, details.previousDays, details.tabProgress, details.underlineIndex);
    const auto& snapshot = details.history;
    const auto extreme = data::HistoryExtrema(snapshot.points);
    const auto plot = HistoryPlotRect(rangeBounds);
    const auto start = snapshot.points.empty() ? snapshot.asOfMs : snapshot.points.front().timeMs;
    const auto end = snapshot.points.empty() ? snapshot.asOfMs : snapshot.points.back().timeMs;
    if (!extreme) {
        canvas.Text(Tr(snapshot.loading ? TextKey::HistoryLoading : snapshot.failed
            ? TextKey::HistoryFailed : TextKey::HistoryEmpty), canvas.body,
            D2D1::RectF(bounds.left + 20, bounds.top + 90, bounds.right - 20, bounds.top + 185),
            theme.secondaryText);
    } else {
        canvas.Text(UiLocalization().Format(TextKey::HistoryExtrema,
            {{L"low", std::to_wstring(extreme->first)}, {L"high", std::to_wstring(extreme->second)}}),
            canvas.smallFormat, D2D1::RectF(bounds.left + 20, bounds.top + 80,
                bounds.right - 20, bounds.top + 110), theme.primaryText);
        canvas.Fill(D2D1::RectF(plot.left, plot.bottom, plot.right, plot.bottom + 1), theme.divider);
        {
        const auto line = HistoryLinePose(details);
        const auto mapPoint = [&](D2D1_POINT_2F point) {
            return D2D1::Point2F(plot.left + point.x * (plot.right - plot.left),
                plot.bottom - point.y * (plot.bottom - plot.top));
        };
        auto previous = line.empty()
            ? HistoryPlotPoint(snapshot.points.front(), start, end, extreme->second, plot)
            : mapPoint(line.front());
        canvas.brush.SetColor(theme.accent);
        for (std::size_t i = 1; i < line.size(); ++i) {
            const auto next = mapPoint(line[i]);
            canvas.target.DrawLine(previous, next, &canvas.brush, 2.0F);
            previous = next;
        }
        // 单个采样也可见，但不伪造一段走势。
        // Keep a single sample visible without inventing a trend.
        canvas.Circle(previous, 2.5F, theme.accent);
        }
        canvas.Text(HistoryDate(start) + L" — " + HistoryDate(end),
            canvas.smallFormat, D2D1::RectF(plot.left, plot.bottom + 4, plot.right, plot.bottom + 27),
            theme.secondaryText);

    }
    canvas.Text(Tr(details.days > data::kHistoryRetentionDays ? TextKey::HistoryMemory : TextKey::HistoryCache),
        canvas.smallFormat, D2D1::RectF(bounds.left + 20, bounds.top + 249, bounds.right - 20,
            bounds.top + 276), theme.secondaryText);
    std::wstring status = Tr(TextKey::HistorySource);
    if (snapshot.loading) status += L" · " + Tr(TextKey::HistoryLoading);
    else if (snapshot.failed) status += L" · " + Tr(TextKey::HistoryFailed);
    else if (snapshot.cached) status += L" · " + Tr(TextKey::HistoryCached);
    canvas.Text(status, canvas.smallFormat, D2D1::RectF(bounds.left + 20, bounds.top + 276,
        bounds.right - 20, bounds.top + 302), theme.secondaryText);
        if (extreme && details.open && details.pointer && details.chartProgress >= 1) {
            if (const auto hover = HitHistory(snapshot, plot, *details.pointer)) {
                auto point = HistoryPlotPoint(hover->point, start, end, extreme->second, plot);
                if (details.hoverIndex) {
                    const float index = std::clamp(*details.hoverIndex, 0.0F,
                        static_cast<float>(snapshot.points.size() - 1));
                    const auto first = static_cast<std::size_t>(index);
                    const auto second = (std::min)(first + 1, snapshot.points.size() - 1);
                    const auto from = HistoryPlotPoint(snapshot.points[first], start, end, extreme->second, plot);
                    const auto to = HistoryPlotPoint(snapshot.points[second], start, end, extreme->second, plot);
                    const float fraction = index - first;
                    point = D2D1::Point2F(from.x + (to.x - from.x) * fraction, from.y + (to.y - from.y) * fraction);
                }
                canvas.Fill(D2D1::RectF(point.x, plot.top, point.x + 1, plot.bottom), theme.divider);
                canvas.Circle(point, 3.5F, theme.accent);
                const float width = (std::min)(280.0F, plot.right - plot.left);
                const float left = std::clamp(point.x + 14, plot.left, plot.right - width);
                const float top = (std::max)(viewportTop + 4, plot.top - 116);
                const auto tip = D2D1::RectF(left, top, left + width, top + 112);
                canvas.Round(tip, 6, theme.selected);
                canvas.brush.SetColor(theme.divider);
                canvas.target.DrawRoundedRectangle(D2D1::RoundedRect(tip, 6, 6), &canvas.brush, 1);
                const std::array lines{
                    HistoryDate(hover->point.timeMs),
                    UiLocalization().Format(TextKey::HistoryHoverCount, {{L"count", std::to_wstring(hover->count)}}),
                    UiLocalization().Format(TextKey::HistoryHoverLow, {{L"price", HistoryMoney(hover->low)}}),
                    UiLocalization().Format(TextKey::HistoryHoverAverage, {{L"price", HistoryMoney(hover->average)}}),
                    UiLocalization().Format(TextKey::HistoryHoverHigh, {{L"price", HistoryMoney(hover->high)}})};
                for (std::size_t i = 0; i < lines.size(); ++i)
                    canvas.Text(lines[i], canvas.smallFormat,
                        D2D1::RectF(tip.left + 10, tip.top + 4 + i * 21,
                            tip.right - 10, tip.top + 25 + i * 21), theme.primaryText);
            }
        }
}
}
