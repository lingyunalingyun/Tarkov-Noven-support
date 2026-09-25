#include "ui/PageHost.h"

#include <algorithm>
#include <string>

namespace noven::ui {

D2D1_RECT_F PageHost::ModeSelectorRect(const UiTheme& theme) const noexcept {
    const float x = theme.sidebarWidth + theme.contentPadding;
    return D2D1::RectF(x + 256, 421, x + 436, 455);
}

std::optional<data::GameMode> PageHost::ModeOptionAt(
    float x, float y, const UiTheme& theme) const noexcept {
    const auto selector = ModeSelectorRect(theme);
    if (x < selector.left || x >= selector.right || y < selector.bottom + 4
        || y >= selector.bottom + 100) return std::nullopt;
    return data::AllGameModes()[static_cast<std::size_t>((y - selector.bottom - 4) / 32)];
}

void PageHost::Draw(const UiCanvas& canvas, const UiTheme& theme, float width,
                    float height, MainPage active, const ScannerPageState& scanner,
                    bool modeMenuOpen, bool modeHovered,
                    std::optional<data::GameMode> hoveredMode) const {
    const PageInfo* page = FindPage(active);
    if (page == nullptr) return;
    const float x = theme.sidebarWidth + theme.contentPadding;
    const float right = (std::max)(x + 1.0F, width - theme.contentPadding);

    canvas.Text(page->chinese, canvas.pageTitle,
                D2D1::RectF(x, 55, right, 110), theme.primaryText);
    canvas.Text(page->english, canvas.body,
                D2D1::RectF(x + 2, 113, right, 144), theme.secondaryText);
    canvas.Fill(D2D1::RectF(x, 162, right, 163), theme.divider);

    if (active != MainPage::Scanner) {
        const float card_right = (std::min)(right, x + 600.0F);
        canvas.Round(D2D1::RectF(x, 205, card_right, 355),
                     theme.cornerRadius, theme.surface);
        canvas.Circle(D2D1::Point2F(x + 39, 246), 14, theme.selected);
        canvas.Circle(D2D1::Point2F(x + 39, 246), 5, theme.accent);
        canvas.Text(page->chinese, canvas.label,
                    D2D1::RectF(x + 70, 222, card_right - 20, 258), theme.primaryText);
        canvas.Text(page->description, canvas.body,
                    D2D1::RectF(x + 24, 280, card_right - 24, 337), theme.secondaryText);
        return;
    }

    const float card_right = (std::min)(right, x + 620.0F);
    canvas.Round(D2D1::RectF(x, 205, card_right, 328),
                 theme.cornerRadius, theme.surface);
    canvas.Round(D2D1::RectF(x + 22, 227, x + 70, 275), 10.0F, theme.selected);
    canvas.Text(L"F2", canvas.label, D2D1::RectF(x + 30, 232, x + 68, 269), theme.accent);
    canvas.Text(L"快捷扫描已就绪", canvas.label,
                D2D1::RectF(x + 88, 221, card_right - 20, 258), theme.primaryText);
    canvas.Text(L"将鼠标悬停在物品上，按 F2 触发单次扫描。",
                canvas.body, D2D1::RectF(x + 88, 260, card_right - 20, 311),
                theme.secondaryText);

    canvas.Text(L"扫描器状态", canvas.label,
                D2D1::RectF(x, 353, card_right, 385), theme.primaryText);
    canvas.Text(L"SCANNER STATUS", canvas.smallFormat,
                D2D1::RectF(x, 381, card_right, 402), theme.secondaryText);
    canvas.Round(D2D1::RectF(x, 408, card_right, 592),
                 theme.cornerRadius, theme.surface);
    canvas.Text(L"当前游戏模式", canvas.body,
                D2D1::RectF(x + 22, 422, x + 218, 455), theme.secondaryText);
    canvas.Fill(D2D1::RectF(x + 22, 466, card_right - 22, 467), theme.divider);
    canvas.Text(L"OCR", canvas.body,
                D2D1::RectF(x + 22, 478, x + 170, 510), theme.secondaryText);
    canvas.Text(scanner.ocrReady ? L"Ready" : L"Unavailable", canvas.body,
                D2D1::RectF(x + 256, 478, card_right - 20, 510),
                scanner.ocrReady ? theme.primaryText : theme.secondaryText);
    canvas.Fill(D2D1::RectF(x + 22, 520, card_right - 22, 521), theme.divider);
    canvas.Text(L"Item Catalog", canvas.body,
                D2D1::RectF(x + 22, 532, x + 200, 564), theme.secondaryText);
    const std::wstring catalog_status = scanner.catalogItems == 0
        ? L"Unavailable" : L"Ready  ·  " + std::to_wstring(scanner.catalogItems) + L" items";
    canvas.Text(catalog_status, canvas.body,
                D2D1::RectF(x + 256, 532, card_right - 20, 565),
                scanner.catalogItems != 0 ? theme.primaryText : theme.secondaryText);

    if (height > 660.0F) {
        canvas.Text(L"经济数据在后台独立更新；扫描使用本地缓存。", canvas.smallFormat,
                    D2D1::RectF(x + 2, 612, right, 641), theme.secondaryText);
    }

    // 模式选择器与卡片共用 Direct2D 画面，避免子窗口在缩放时独立闪烁。
    // Paint the selector in the same Direct2D frame as the card to avoid
    // independent child-window repainting during resize.
    const auto selector = ModeSelectorRect(theme);
    canvas.Round(selector, 6.0F, modeHovered || modeMenuOpen ? theme.hover : theme.selected);
    canvas.Text(data::GameModeName(scanner.mode), canvas.body,
                D2D1::RectF(selector.left + 12, selector.top, selector.right - 28,
                            selector.bottom), theme.primaryText);
    canvas.brush.SetColor(theme.secondaryText);
    canvas.target.DrawLine(D2D1::Point2F(selector.right - 21, selector.top + 14),
                           D2D1::Point2F(selector.right - 15, selector.top + 20),
                           &canvas.brush, 1.5F);
    canvas.target.DrawLine(D2D1::Point2F(selector.right - 15, selector.top + 20),
                           D2D1::Point2F(selector.right - 9, selector.top + 14),
                           &canvas.brush, 1.5F);
    if (modeMenuOpen) {
        for (std::size_t index = 0; index < data::AllGameModes().size(); ++index) {
            const auto mode = data::AllGameModes()[index];
            const float top = selector.bottom + 4 + static_cast<float>(index) * 32;
            const auto row = D2D1::RectF(selector.left, top, selector.right, top + 32);
            canvas.Round(row, 4.0F, hoveredMode == mode || scanner.mode == mode
                ? theme.selected : theme.surface);
            canvas.Text(data::GameModeName(mode), canvas.body,
                        D2D1::RectF(row.left + 12, row.top, row.right - 12, row.bottom),
                        scanner.mode == mode ? theme.accent : theme.primaryText);
        }
    }
}

} // namespace noven::ui
