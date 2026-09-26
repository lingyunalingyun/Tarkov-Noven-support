#include "ui/MainWindowUi.h"

#include <algorithm>
#include <cmath>

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
    item_bitmaps_.clear();
    for (const auto& [id, image] : image_pixels_) BuildItemBitmap(image);
    return true;
}

void MainWindowUi::BuildItemBitmap(const ItemImage& image) {
    if (!render_target_) return;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    if (SUCCEEDED(render_target_->CreateBitmap(D2D1::SizeU(image.width, image.height),
            image.pixels.data(), image.width * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), &bitmap))) {
        item_bitmaps_[image.id] = std::move(bitmap);
    }
}

void MainWindowUi::ItemImagesReady() {
    // Direct2D 资源只在 UI 线程创建；保留 CPU 像素用于设备丢失后的重建。
    // Create Direct2D resources only on the UI thread; retain pixels for device recovery.
    for (auto& image : image_cache_.TakeReady()) {
        if (std::none_of(recent_.begin(), recent_.end(), [&](const auto& entry) {
                return entry.stableItemId == image.id;
            })) {
            image_cache_.Forget(image.id);
            continue;
        }
        BuildItemBitmap(image);
        image_pixels_[image.id] = std::move(image);
    }
    Invalidate();
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
                    mode_hovered_, hovered_mode_, recent_, recent_filter_,
                    hovered_recent_tab_, recent_scroll_, recent_transition_, item_bitmaps_);
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
    CancelScrollDrag();
    if (render_target_ != nullptr && width != 0 && height != 0) {
        if (render_target_->Resize(D2D1::SizeU(width, height)) == D2DERR_RECREATE_TARGET) {
            brush_.Reset();
            render_target_.Reset();
        }
    }
    recent_scroll_ = (std::min)(recent_scroll_,
        PageHost::RecentMaxScroll(DipHeight(),
            PageHost::RecentFilteredCount(recent_, recent_filter_)));
    recent_scroll_target_ = (std::min)(recent_scroll_target_,
        PageHost::RecentMaxScroll(DipHeight(),
            PageHost::RecentFilteredCount(recent_, recent_filter_)));
    Invalidate();
}

void MainWindowUi::DpiChanged(UINT dpi) {
    CancelScrollDrag();
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

std::optional<RecentScrollbar> MainWindowUi::Scrollbar() const noexcept {
    // 过渡滑块仅作视觉插值，落定后才使用目标列表的拖动映射。
    // The transitioning thumb is visual only; enable target-list dragging once settled.
    if (navigation_.Active() != MainPage::RecentScans
        || recent_transition_.progress < 1.0F) return std::nullopt;
    RECT client{};
    GetClientRect(window_, &client);
    return PageHost::RecentScrollGeometry(static_cast<float>(client.right) / Scale(),
        DipHeight(), theme_, PageHost::RecentFilteredCount(recent_, recent_filter_),
        recent_scroll_);
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
    if (recent_scroll_grab_) {
        if (const auto bar = Scrollbar()) {
            // 拖动直接跟手，滚轮仍使用原有平滑动画。
            // Dragging follows the pointer directly; the wheel keeps its smooth animation.
            recent_scroll_ = recent_scroll_target_ = bar->OffsetFromThumbTop(
                static_cast<float>(y) / Scale() - *recent_scroll_grab_);
            Invalidate();
        }
        return;
    }
    const auto next = HitTest(x, y);
    const bool mode_hovered = OnModeSelector(x, y);
    const auto hovered_mode = ModeOptionAt(x, y);
    const auto recent_tab = navigation_.Active() == MainPage::RecentScans
        ? pages_.RecentTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
    if (next != hovered_ || mode_hovered != mode_hovered_
        || hovered_mode != hovered_mode_ || recent_tab != hovered_recent_tab_) {
        hovered_ = next;
        mode_hovered_ = mode_hovered;
        hovered_mode_ = hovered_mode;
        hovered_recent_tab_ = recent_tab;
        Invalidate();
    }
}

void MainWindowUi::MouseLeave() {
    if (hovered_.has_value() || mode_hovered_ || hovered_mode_.has_value()
        || hovered_recent_tab_.has_value()) {
        hovered_.reset();
        mode_hovered_ = false;
        hovered_mode_.reset();
        hovered_recent_tab_.reset();
        Invalidate();
    }
}

void MainWindowUi::MouseDown(int x, int y) {
    CancelScrollDrag();
    if (const auto bar = Scrollbar()) {
        const float dx = static_cast<float>(x) / Scale();
        const float dy = static_cast<float>(y) / Scale();
        if (dx >= bar->track.left && dx < bar->track.right
            && dy >= bar->track.top && dy < bar->track.bottom) {
            recent_scroll_grab_ = dy >= bar->thumb.top && dy < bar->thumb.bottom
                ? dy - bar->thumb.top : (bar->thumb.bottom - bar->thumb.top) / 2.0F;
            recent_scroll_ = recent_scroll_target_ = bar->OffsetFromThumbTop(
                dy - *recent_scroll_grab_);
            pressed_.reset();
            pressed_recent_tab_.reset();
            Invalidate();
            return;
        }
    }
    pressed_ = HitTest(x, y);
    mode_pressed_ = OnModeSelector(x, y);
    pressed_mode_ = ModeOptionAt(x, y);
    pressed_recent_tab_ = navigation_.Active() == MainPage::RecentScans
        ? pages_.RecentTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
    if (pressed_ || mode_pressed_ || pressed_mode_ || pressed_recent_tab_) Invalidate();
}

std::optional<data::GameMode> MainWindowUi::MouseUp(int x, int y) {
    if (recent_scroll_grab_) {
        MouseMove(x, y);
        CancelScrollDrag();
        return std::nullopt;
    }
    const auto released = HitTest(x, y);
    const bool page_changed = pressed_.has_value() && pressed_ == released
        && navigation_.Active() != *pressed_ && navigation_.Select(*pressed_);
    const auto released_mode = ModeOptionAt(x, y);
    const auto released_recent_tab = navigation_.Active() == MainPage::RecentScans
        ? pages_.RecentTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
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
    if (pressed_recent_tab_ && pressed_recent_tab_ == released_recent_tab
        && recent_filter_ != *pressed_recent_tab_) {
        recent_transition_.outgoingMode = recent_filter_;
        recent_transition_.outgoingScroll = recent_scroll_;
        recent_underline_from_ = recent_transition_.underlineIndex;
        recent_filter_ = *pressed_recent_tab_;
        recent_transition_.progress = 0.0F;
        recent_tab_started_ = std::chrono::steady_clock::now();
        recent_scroll_ = 0.0F;
        recent_scroll_target_ = 0.0F;
    }
    if (page_changed) {
        recent_transition_.progress = 1.0F;
        recent_transition_.underlineIndex = static_cast<float>(recent_filter_);
        recent_scroll_ = recent_scroll_target_;
    }
    pressed_.reset();
    mode_pressed_ = false;
    pressed_mode_.reset();
    hovered_mode_.reset();
    pressed_recent_tab_.reset();
    Invalidate();
    return selection;
}

void MainWindowUi::SetScannerState(ScannerPageState state) {
    scanner_ = state;
    Invalidate();
}

void MainWindowUi::SetRecentScans(std::vector<data::RecentScanEntry> entries) {
    CancelScrollDrag();
    const bool new_selected_entry = !entries.empty()
        && (recent_.empty() || entries.front().scanId != recent_.front().scanId)
        && entries.front().gameMode == recent_filter_;
    recent_ = std::move(entries);
    for (auto it = image_pixels_.begin(); it != image_pixels_.end();) {
        if (std::none_of(recent_.begin(), recent_.end(), [&](const auto& entry) {
                return entry.stableItemId == it->first;
            })) {
            image_cache_.Forget(it->first);
            item_bitmaps_.erase(it->first);
            it = image_pixels_.erase(it);
        } else ++it;
    }
    for (const auto& entry : recent_) image_cache_.Request(entry.stableItemId);
    if (new_selected_entry) {
        recent_scroll_ = 0.0F;
        recent_scroll_target_ = 0.0F;
    } else {
        const float maximum = PageHost::RecentMaxScroll(DipHeight(),
            PageHost::RecentFilteredCount(recent_, recent_filter_));
        recent_scroll_ = (std::min)(recent_scroll_, maximum);
        recent_scroll_target_ = (std::min)(recent_scroll_target_, maximum);
    }
    Invalidate();
}

bool MainWindowUi::MouseWheel(int x, int y, int delta) {
    if (recent_scroll_grab_) return false;
    if (navigation_.Active() != MainPage::RecentScans
        || static_cast<float>(x) / Scale() < theme_.sidebarWidth
        || y < 0) return false;
    const float maximum = PageHost::RecentMaxScroll(DipHeight(),
        PageHost::RecentFilteredCount(recent_, recent_filter_));
    recent_scroll_target_ = std::clamp(recent_scroll_target_
        - static_cast<float>(delta) / WHEEL_DELTA * 66.0F, 0.0F, maximum);
    if (recent_scroll_ != recent_scroll_target_
        && recent_scroll_tick_ == std::chrono::steady_clock::time_point{})
        recent_scroll_tick_ = std::chrono::steady_clock::now();
    return recent_scroll_ != recent_scroll_target_;
}

bool MainWindowUi::AnimationActive() const noexcept {
    return recent_transition_.progress < 1.0F
        || std::abs(recent_scroll_target_ - recent_scroll_) >= 0.75F;
}

bool MainWindowUi::AnimationTick() {
    const auto now = std::chrono::steady_clock::now();
    const float elapsed = recent_scroll_tick_ == std::chrono::steady_clock::time_point{}
        ? 0.016F
        : std::chrono::duration<float>(now - recent_scroll_tick_).count();
    recent_scroll_tick_ = now;
    const float remaining = recent_scroll_target_ - recent_scroll_;
    if (std::abs(remaining) < 0.75F) {
        recent_scroll_ = recent_scroll_target_;
    } else {
        recent_scroll_ += remaining * (1.0F - std::exp(-24.0F * std::clamp(elapsed, 0.0F, 0.05F)));
    }
    if (recent_transition_.progress < 1.0F) {
        constexpr float kTabDurationSeconds = 0.36F;
        recent_transition_.progress = std::clamp(
            std::chrono::duration<float>(now - recent_tab_started_).count()
                / kTabDurationSeconds, 0.0F, 1.0F);
        const float targetIndex = static_cast<float>(recent_filter_);
        const float lineProgress = SampleTabTransition(
            recent_transition_.progress).underlineProgress;
        recent_transition_.underlineIndex = recent_underline_from_
            + (targetIndex - recent_underline_from_) * lineProgress;
        if (recent_transition_.progress >= 1.0F)
            recent_transition_.underlineIndex = targetIndex;
    }
    if (!AnimationActive()) recent_scroll_tick_ = {};
    Invalidate();
    return AnimationActive();
}

} // namespace noven::ui
