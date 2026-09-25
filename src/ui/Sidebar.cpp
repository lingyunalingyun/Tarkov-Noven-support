#include "ui/Sidebar.h"

#include <algorithm>

namespace noven::ui {
namespace {

void DrawIcon(const UiCanvas& canvas, MainPage page, float x, float y,
              D2D1_COLOR_F color) {
    canvas.brush.SetColor(color);
    const auto bounds = D2D1::RoundedRect(D2D1::RectF(x, y, x + 18, y + 18), 4.0F, 4.0F);
    canvas.target.DrawRoundedRectangle(bounds, &canvas.brush, 1.4F);
    if (page == MainPage::Scanner) {
        canvas.target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 9, y + 9), 4, 4),
                                  &canvas.brush, 1.2F);
        canvas.Circle(D2D1::Point2F(x + 9, y + 9), 1.5F, color);
    } else if (page == MainPage::Prices) {
        canvas.target.DrawLine(D2D1::Point2F(x + 4, y + 13),
                               D2D1::Point2F(x + 4, y + 9), &canvas.brush, 1.5F);
        canvas.target.DrawLine(D2D1::Point2F(x + 9, y + 13),
                               D2D1::Point2F(x + 9, y + 5), &canvas.brush, 1.5F);
        canvas.target.DrawLine(D2D1::Point2F(x + 14, y + 13),
                               D2D1::Point2F(x + 14, y + 7), &canvas.brush, 1.5F);
    } else {
        canvas.target.DrawLine(D2D1::Point2F(x + 5, y + 7),
                               D2D1::Point2F(x + 13, y + 7), &canvas.brush, 1.3F);
        canvas.target.DrawLine(D2D1::Point2F(x + 5, y + 11),
                               D2D1::Point2F(x + 11, y + 11), &canvas.brush, 1.3F);
    }
}

} // namespace

D2D1_RECT_F Sidebar::ItemRect(MainPage page, float height,
                              const UiTheme& theme) const noexcept {
    float top = 0.0F;
    switch (page) {
    case MainPage::Scanner: top = 186.0F; break;
    case MainPage::Prices: top = 229.0F; break;
    case MainPage::Hideout: top = 272.0F; break;
    case MainPage::Tasks: top = 315.0F; break;
    case MainPage::Map: top = 358.0F; break;
    case MainPage::RaidHistory: top = 439.0F; break;
    case MainPage::Squad: top = 482.0F; break;
    case MainPage::Events: top = 525.0F; break;
    case MainPage::RecentScans: top = 568.0F; break;
    case MainPage::Settings: top = height - 57.0F; break;
    }
    return D2D1::RectF(12.0F, top, theme.sidebarWidth - 12.0F,
                       top + theme.navigationHeight);
}

std::optional<MainPage> Sidebar::HitTest(float x, float y, float height,
                                          const UiTheme& theme) const noexcept {
    if (x < 0.0F || x >= theme.sidebarWidth) return std::nullopt;
    for (const PageInfo& page : kPages) {
        const D2D1_RECT_F rect = ItemRect(page.id, height, theme);
        if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom)
            return page.id;
    }
    return std::nullopt;
}

void Sidebar::Draw(const UiCanvas& canvas, const UiTheme& theme, float height,
                   MainPage active, std::optional<MainPage> hovered,
                   std::optional<MainPage> pressed) const {
    canvas.Fill(D2D1::RectF(0, 0, theme.sidebarWidth, height), theme.sidebar);
    canvas.Circle(D2D1::Point2F(37, 47), 19, theme.accent);
    canvas.Text(L"N", canvas.title, D2D1::RectF(26, 27, 51, 70), theme.sidebar);
    canvas.Text(L"NOVEN", canvas.title, D2D1::RectF(65, 25, 215, 58), theme.primaryText);
    canvas.Text(L"TARKOV SUPPORT", canvas.smallFormat,
                D2D1::RectF(67, 57, 220, 78), theme.secondaryText);

    canvas.Round(D2D1::RectF(12, 96, theme.sidebarWidth - 12, 151),
                 theme.cornerRadius, theme.surface);
    canvas.Circle(D2D1::Point2F(42, 124), 16, theme.hover);
    canvas.Circle(D2D1::Point2F(42, 120), 5, theme.secondaryText);
    canvas.Round(D2D1::RectF(34, 127, 50, 134), 4, theme.secondaryText);
    canvas.Text(L"用户名", canvas.label, D2D1::RectF(69, 105, 215, 131), theme.primaryText);
    canvas.Text(L"本地使用", canvas.smallFormat, D2D1::RectF(70, 127, 215, 147), theme.secondaryText);

    canvas.Text(L"主要功能  /  PRIMARY", canvas.smallFormat,
                D2D1::RectF(22, 160, 225, 181), theme.secondaryText);
    canvas.Fill(D2D1::RectF(22, 414, theme.sidebarWidth - 22, 415), theme.divider);
    canvas.Text(L"更多  /  MORE", canvas.smallFormat,
                D2D1::RectF(22, 418, 225, 437), theme.secondaryText);
    canvas.Fill(D2D1::RectF(22, height - 71, theme.sidebarWidth - 22,
                            height - 70), theme.divider);

    for (const PageInfo& page : kPages) {
        const D2D1_RECT_F rect = ItemRect(page.id, height, theme);
        const bool selected = page.id == active;
        const bool is_hovered = hovered == page.id;
        if (selected || is_hovered || pressed == page.id) {
            canvas.Round(rect, 8.0F, selected ? theme.selected : theme.hover);
        }
        if (selected) {
            canvas.Round(D2D1::RectF(rect.left, rect.top + 9, rect.left + 3,
                                     rect.bottom - 9), 1.5F, theme.accent);
        }
        const D2D1_COLOR_F color = selected ? theme.primaryText : theme.secondaryText;
        DrawIcon(canvas, page.id, rect.left + 14, rect.top + 11, color);
        canvas.Text(page.chinese, canvas.label,
                    D2D1::RectF(rect.left + 45, rect.top + 5,
                                rect.right - 9, rect.bottom), color);
    }
}

} // namespace noven::ui
