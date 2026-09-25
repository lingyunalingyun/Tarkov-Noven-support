#include "ui/MainWindowUi.h"

#include <algorithm>

namespace noven::ui {

bool MainWindowUi::Initialize(HWND window, std::wstring& error) {
    window_ = window;
    dpi_ = GetDpiForWindow(window);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2d_factory_.GetAddressOf()))) {
        error = L"Could not initialize Direct2D for the main window";
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf())))) {
        error = L"Could not initialize DirectWrite for the main window";
        return false;
    }
    const auto format = [&](float size, DWRITE_FONT_WEIGHT weight,
                            Microsoft::WRL::ComPtr<IDWriteTextFormat>& destination) {
        return SUCCEEDED(write_factory_->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", destination.GetAddressOf()));
    };
    if (!format(25, DWRITE_FONT_WEIGHT_SEMI_BOLD, title_format_)
        || !format(36, DWRITE_FONT_WEIGHT_SEMI_BOLD, page_title_format_)
        || !format(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, label_format_)
        || !format(15, DWRITE_FONT_WEIGHT_NORMAL, body_format_)
        || !format(12, DWRITE_FONT_WEIGHT_NORMAL, small_format_)) {
        error = L"Could not create main-window text formats";
        return false;
    }
    for (IDWriteTextFormat* text : {title_format_.Get(), page_title_format_.Get(),
                                   label_format_.Get(), body_format_.Get(),
                                   small_format_.Get()}) {
        text->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return CreateRenderTarget(error);
}

bool MainWindowUi::CreateRenderTarget(std::wstring& error) {
    RECT client{};
    GetClientRect(window_, &client);
    const auto pixels = D2D1::SizeU(std::max(1L, client.right),
                                    std::max(1L, client.bottom));
    const HRESULT hr = d2d_factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                     D2D1::PixelFormat(), static_cast<float>(dpi_),
                                     static_cast<float>(dpi_)),
        D2D1::HwndRenderTargetProperties(window_, pixels),
        render_target_.GetAddressOf());
    if (FAILED(hr)) {
        error = L"Could not create main-window Direct2D render target";
        return false;
    }
    if (FAILED(render_target_->CreateSolidColorBrush(theme_.primaryText,
                                                     brush_.GetAddressOf()))) {
        error = L"Could not create main-window brush";
        render_target_.Reset();
        return false;
    }
    return true;
}

void MainWindowUi::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (render_target_ == nullptr) {
        std::wstring error;
        CreateRenderTarget(error);
    }
    if (render_target_ != nullptr && brush_ != nullptr) {
        const D2D1_SIZE_F size = render_target_->GetSize();
        UiCanvas canvas{*render_target_.Get(), *brush_.Get(), *title_format_.Get(),
                        *page_title_format_.Get(), *label_format_.Get(),
                        *body_format_.Get(), *small_format_.Get()};
        render_target_->BeginDraw();
        render_target_->Clear(theme_.background);
        sidebar_.Draw(canvas, theme_, size.height, navigation_.Active(),
                      hovered_, pressed_);
        render_target_->PushAxisAlignedClip(
            D2D1::RectF(theme_.sidebarWidth, 0, size.width, size.height),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pages_.Draw(canvas, theme_, size.width, size.height,
                    navigation_.Active(), scanner_, mode_menu_open_,
                    mode_hovered_, hovered_mode_);
        render_target_->PopAxisAlignedClip();
        if (render_target_->EndDraw() == D2DERR_RECREATE_TARGET) {
            brush_.Reset();
            render_target_.Reset();
            Invalidate();
        }
    }
    EndPaint(window_, &paint);
}

void MainWindowUi::Resize(UINT width, UINT height) {
    if (render_target_ != nullptr && width != 0 && height != 0) {
        if (render_target_->Resize(D2D1::SizeU(width, height)) == D2DERR_RECREATE_TARGET) {
            brush_.Reset();
            render_target_.Reset();
        }
    }
    Invalidate();
}

void MainWindowUi::DpiChanged(UINT dpi) {
    dpi_ = dpi;
    if (render_target_ != nullptr) render_target_->SetDpi(
        static_cast<float>(dpi), static_cast<float>(dpi));
    Invalidate();
}

float MainWindowUi::DipHeight() const noexcept {
    RECT client{};
    GetClientRect(window_, &client);
    return static_cast<float>(client.bottom) / Scale();
}

std::optional<MainPage> MainWindowUi::HitTest(int x, int y) const noexcept {
    return sidebar_.HitTest(static_cast<float>(x) / Scale(),
                            static_cast<float>(y) / Scale(), DipHeight(), theme_);
}

bool MainWindowUi::OnModeSelector(int x, int y) const noexcept {
    if (navigation_.Active() != MainPage::Scanner) return false;
    const auto rect = pages_.ModeSelectorRect(theme_);
    const float dip_x = static_cast<float>(x) / Scale();
    const float dip_y = static_cast<float>(y) / Scale();
    return dip_x >= rect.left && dip_x < rect.right
        && dip_y >= rect.top && dip_y < rect.bottom;
}

std::optional<data::GameMode> MainWindowUi::ModeOptionAt(int x, int y) const noexcept {
    if (!mode_menu_open_ || navigation_.Active() != MainPage::Scanner)
        return std::nullopt;
    return pages_.ModeOptionAt(static_cast<float>(x) / Scale(),
                               static_cast<float>(y) / Scale(), theme_);
}

void MainWindowUi::Invalidate() const {
    if (window_ != nullptr) InvalidateRect(window_, nullptr, FALSE);
}

void MainWindowUi::MouseMove(int x, int y) {
    const auto next = HitTest(x, y);
    const bool mode_hovered = OnModeSelector(x, y);
    const auto hovered_mode = ModeOptionAt(x, y);
    if (next != hovered_ || mode_hovered != mode_hovered_
        || hovered_mode != hovered_mode_) {
        hovered_ = next;
        mode_hovered_ = mode_hovered;
        hovered_mode_ = hovered_mode;
        Invalidate();
    }
}

void MainWindowUi::MouseLeave() {
    if (hovered_.has_value() || mode_hovered_ || hovered_mode_.has_value()) {
        hovered_.reset();
        mode_hovered_ = false;
        hovered_mode_.reset();
        Invalidate();
    }
}

void MainWindowUi::MouseDown(int x, int y) {
    pressed_ = HitTest(x, y);
    mode_pressed_ = OnModeSelector(x, y);
    pressed_mode_ = ModeOptionAt(x, y);
    if (pressed_ || mode_pressed_ || pressed_mode_) Invalidate();
}

std::optional<data::GameMode> MainWindowUi::MouseUp(int x, int y) {
    const auto released = HitTest(x, y);
    const bool page_changed = pressed_.has_value() && pressed_ == released
        && navigation_.Active() != *pressed_ && navigation_.Select(*pressed_);
    const auto released_mode = ModeOptionAt(x, y);
    std::optional<data::GameMode> selection;
    if (pressed_mode_ && pressed_mode_ == released_mode) {
        if (scanner_.mode != *pressed_mode_) {
            scanner_.mode = *pressed_mode_;
            selection = scanner_.mode;
        }
        mode_menu_open_ = false;
    } else if (mode_pressed_ && OnModeSelector(x, y)) {
        mode_menu_open_ = !mode_menu_open_;
    } else if (mode_menu_open_ || page_changed) {
        mode_menu_open_ = false;
    }
    pressed_.reset();
    mode_pressed_ = false;
    pressed_mode_.reset();
    hovered_mode_.reset();
    Invalidate();
    return selection;
}

void MainWindowUi::SetScannerState(ScannerPageState state) {
    scanner_ = state;
    Invalidate();
}

} // namespace noven::ui
