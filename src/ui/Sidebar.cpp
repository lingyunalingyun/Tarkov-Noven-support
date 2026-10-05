#include "ui/Sidebar.h"
#include "ui/localization/LocalizationService.h"

#include <algorithm>
#include <cmath>

namespace noven::ui {
namespace {

void DrawIcon(const UiCanvas& canvas, PageIcon icon, float x, float y,
              D2D1_COLOR_F color) {
    canvas.brush.SetColor(color);
    const auto line=[&](float a,float b,float c,float d){canvas.target.DrawLine(D2D1::Point2F(x+a,y+b),D2D1::Point2F(x+c,y+d),&canvas.brush,1.4F);};
    // 原生矢量轮廓，无图标字体或位图依赖。
    // Native vector outlines without icon-font or bitmap dependencies.
    if(icon==PageIcon::Hideout){line(0,8,9,1);line(9,1,18,8);line(3,7,3,17);line(3,17,15,17);line(15,17,15,7);line(7,17,7,11);line(7,11,11,11);line(11,11,11,17);return;}
    if(icon==PageIcon::Map){line(1,3,6,1);line(6,1,12,4);line(12,4,17,2);line(17,2,17,15);line(17,15,12,17);line(12,17,6,14);line(6,14,1,16);line(1,16,1,3);line(6,1,6,14);line(12,4,12,17);return;}
    if(icon==PageIcon::RaidHistory){canvas.target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+9,y+9),8,8),&canvas.brush,1.4F);line(9,4,9,9);line(9,9,13,11);return;}
    if(icon==PageIcon::Squad){for(float offset:{5.0F,13.0F}){canvas.target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+offset,y+5),3,3),&canvas.brush,1.4F);line(offset-4,16,offset-4,12);line(offset-4,12,offset+4,12);line(offset+4,12,offset+4,16);}return;}
    if(icon==PageIcon::Settings){canvas.target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+9,y+9),5,5),&canvas.brush,1.4F);canvas.target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+9,y+9),2,2),&canvas.brush,1.4F);for(int i=0;i<8;++i){float a=i*0.785398F;line(9+5*std::cos(a),9+5*std::sin(a),9+8*std::cos(a),9+8*std::sin(a));}return;}
    if(icon==PageIcon::RecentScans){line(1,5,1,17);line(1,17,13,17);line(5,1,17,1);line(17,1,17,13);line(17,13,5,13);line(5,13,5,1);line(8,5,14,5);line(8,9,12,9);return;}
    const auto bounds = D2D1::RoundedRect(D2D1::RectF(x, y, x + 18, y + 18), 4.0F, 4.0F);
    canvas.target.DrawRoundedRectangle(bounds, &canvas.brush, 1.4F);
    if (icon==PageIcon::Scanner) {
        canvas.target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 9, y + 9), 4, 4),
                                  &canvas.brush, 1.2F);
        canvas.Circle(D2D1::Point2F(x + 9, y + 9), 1.5F, color);
    } else if (icon==PageIcon::Prices) {
        canvas.target.DrawLine(D2D1::Point2F(x + 4, y + 13),
                               D2D1::Point2F(x + 4, y + 9), &canvas.brush, 1.5F);
        canvas.target.DrawLine(D2D1::Point2F(x + 9, y + 13),
                               D2D1::Point2F(x + 9, y + 5), &canvas.brush, 1.5F);
        canvas.target.DrawLine(D2D1::Point2F(x + 14, y + 13),
                               D2D1::Point2F(x + 14, y + 7), &canvas.brush, 1.5F);
    } else if(icon==PageIcon::Events) {
        line(0,5,18,5);line(5,-1,5,3);line(13,-1,13,3);line(6,10,8,13);line(8,13,13,8);
    } else {
        canvas.target.DrawLine(D2D1::Point2F(x + 5, y + 7),
                               D2D1::Point2F(x + 13, y + 7), &canvas.brush, 1.3F);
        canvas.target.DrawLine(D2D1::Point2F(x + 5, y + 11),
                               D2D1::Point2F(x + 11, y + 11), &canvas.brush, 1.3F);
    }
}

} // namespace

const SidebarLayout& Sidebar::Layout(float height,const UiTheme& theme) const {
    if(revision_!=registry_.Revision()||height_!=height||width_!=theme.sidebarWidth||rowHeight_!=theme.navigationHeight) {
        layout_=BuildSidebarLayout(registry_,height,theme);revision_=registry_.Revision();
        height_=height;width_=theme.sidebarWidth;rowHeight_=theme.navigationHeight;
    }
    return layout_;
}
D2D1_RECT_F Sidebar::ItemRect(PageId page,float height,const UiTheme& theme) const noexcept {
    const auto* row=Layout(height,theme).Find(page);
    return row?row->rect:D2D1::RectF(0,0,0,0);
}
std::optional<PageId> Sidebar::HitTest(float x,float y,float height,const UiTheme& theme) const noexcept {
    return Layout(height,theme).HitTest(x,y);
}

void Sidebar::Draw(const UiCanvas& canvas, const UiTheme& theme, float height,
                   PageId active, std::optional<PageId> hovered,
                   std::optional<PageId> pressed) const {
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
    canvas.Text(Tr(TextKey::UserName), canvas.label, D2D1::RectF(69, 105, 215, 131), theme.primaryText);
    canvas.Text(Tr(TextKey::LocalUse), canvas.smallFormat, D2D1::RectF(70, 127, 215, 147), theme.secondaryText);

    const auto& layout=Layout(height,theme);
    if(layout.primaryLabel)canvas.Text(Tr(TextKey::Primary),canvas.smallFormat,*layout.primaryLabel,theme.secondaryText);
    if(layout.secondaryDivider)canvas.Fill(*layout.secondaryDivider,theme.divider);
    if(layout.secondaryLabel)canvas.Text(Tr(TextKey::More),canvas.smallFormat,*layout.secondaryLabel,theme.secondaryText);
    if(layout.bottomDivider)canvas.Fill(*layout.bottomDivider,theme.divider);

    const auto* selectedRow=layout.Find(active);
    if(selectedRow&&selectedRow->visible) {
    const auto selection=SelectionRect(active,height,theme);
    canvas.Round(selection,8.0F,theme.selected);
    canvas.Round(D2D1::RectF(selection.left,selection.top+9,selection.left+3,selection.bottom-9),1.5F,theme.accent);
    }
    for (const auto& row : layout.rows) {
        if(!row.visible)continue;
        const auto& page=row.page;
        const D2D1_RECT_F rect = row.rect;
        const bool selected = page.id == active;
        const bool is_hovered = hovered == page.id;
        if (!selected && (is_hovered || pressed == page.id))canvas.Round(rect,8.0F,theme.hover);
        const D2D1_COLOR_F color = selected ? theme.primaryText : theme.secondaryText;
        DrawIcon(canvas, page.icon, rect.left + 14, rect.top + 11, color);
        canvas.Text(Tr(page.titleKey), canvas.label,
                    D2D1::RectF(rect.left + 45, rect.top + 5,
                                rect.right - 9, rect.bottom), color);
    }
}

} // namespace noven::ui
