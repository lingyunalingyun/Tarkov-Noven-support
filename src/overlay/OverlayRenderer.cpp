#include "overlay/OverlayRenderer.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace noven::overlay {

namespace {

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return L"<invalid UTF-8>";
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring ModeText(data::GameMode mode) {
    return data::GameModeName(mode);
}

std::wstring TraderText(const std::optional<data::TraderSellValue>& trader) {
    if (!trader.has_value()) {
        return L"未知";
    }
    const std::wstring name = Utf8ToWide(trader->traderName);
    return (name.empty() ? L"未知" : name) + L" " + FormatRoubles(trader->priceRoubles);
}

} // namespace

bool OverlayRenderer::Initialize(std::wstring& error) {
    HRESULT result = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        IID_PPV_ARGS(&d2d_factory_)
    );
    if (FAILED(result)) {
        error = L"D2D1CreateFactory failed";
        return false;
    }
    result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf())
    );
    if (FAILED(result)) {
        error = L"DWriteCreateFactory failed";
        return false;
    }

    result = write_factory_->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 21.0F,
        L"", &title_format_
    );
    if (FAILED(result)) {
        error = L"Could not create overlay title format";
        return false;
    }
    result = write_factory_->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0F,
        L"", &body_format_
    );
    if (FAILED(result)) {
        error = L"Could not create overlay body format";
        return false;
    }
    result = write_factory_->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0F,
        L"", &mode_format_
    );
    if (FAILED(result)) {
        error = L"Could not create overlay mode format";
        return false;
    }
    title_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    body_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    mode_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

void OverlayRenderer::Shutdown() noexcept {
    render_target_.Reset();
    background_brush_.Reset();
    border_brush_.Reset();
    title_brush_.Reset();
    label_brush_.Reset();
    value_brush_.Reset();
    title_format_.Reset();
    body_format_.Reset();
    mode_format_.Reset();
    write_factory_.Reset();
    d2d_factory_.Reset();
}

SIZE OverlayRenderer::Measure(const ScanDisplayResult& result) const {
    const std::wstring name = Utf8ToWide(result.displayName);
    float name_width = 0.0F;
    if (write_factory_ != nullptr && !name.empty()) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(write_factory_->CreateTextLayout(
            name.c_str(), static_cast<UINT32>(name.size()), title_format_.Get(),
            1000.0F, 40.0F, &layout))) {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                name_width = metrics.width;
            }
        }
    }
    const long width = std::clamp(static_cast<long>(name_width) + 36L, 340L, 600L);
    return SIZE{width, 170};
}

bool OverlayRenderer::EnsureRenderTarget(
    HWND window,
    UINT width,
    UINT height,
    std::wstring& error
) {
    if (render_target_ != nullptr) {
        const D2D1_SIZE_U current_size = render_target_->GetPixelSize();
        if (current_size.width != width || current_size.height != height) {
            const HRESULT resize_result = render_target_->Resize(D2D1::SizeU(width, height));
            if (FAILED(resize_result)) {
                render_target_.Reset();
            }
        }
    }
    if (render_target_ != nullptr) {
        return true;
    }
    const HRESULT result = d2d_factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0F,
            96.0F,
            D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT
        ),
        D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(width, height)),
        &render_target_
    );
    if (FAILED(result)) {
        error = L"Could not create Direct2D overlay render target";
        return false;
    }
    const auto create_brush = [&](D2D1::ColorF color,
                                  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush) {
        return render_target_->CreateSolidColorBrush(color, &brush);
    };
    if (FAILED(create_brush(D2D1::ColorF(0.035F, 0.045F, 0.06F, 0.96F), background_brush_))
        || FAILED(create_brush(D2D1::ColorF(0.25F, 0.32F, 0.40F, 0.95F), border_brush_))
        || FAILED(create_brush(D2D1::ColorF(0.98F, 0.98F, 1.0F, 1.0F), title_brush_))
        || FAILED(create_brush(D2D1::ColorF(0.60F, 0.66F, 0.74F, 1.0F), label_brush_))
        || FAILED(create_brush(D2D1::ColorF(0.92F, 0.94F, 0.98F, 1.0F), value_brush_))) {
        error = L"Could not create Direct2D overlay brushes";
        return false;
    }
    return true;
}

bool OverlayRenderer::Render(
    HWND window,
    const ScanDisplayResult& result,
    std::wstring& error
) {
    RECT client{};
    if (!GetClientRect(window, &client)) {
        error = L"GetClientRect failed for overlay";
        return false;
    }
    if (!EnsureRenderTarget(
        window,
        static_cast<UINT>(std::max(1L, client.right - client.left)),
        static_cast<UINT>(std::max(1L, client.bottom - client.top)),
        error
    )) {
        return false;
    }

    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    const D2D1_ROUNDED_RECT card{
        D2D1::RectF(0.5F, 0.5F, width - 0.5F, height - 0.5F), 12.0F, 12.0F};
    const std::wstring title = Utf8ToWide(result.displayName);
    const std::wstring quality = result.matchQuality == MatchQuality::LowConfidence
        ? L"  ·  可能匹配"
        : (result.matchQuality == MatchQuality::OcrOnly ? L"  ·  OCR 识别文本" : L"");
    const std::wstring mode = L"模式  " + ModeText(result.mode) + quality;
    const std::wstring flea = L"跳蚤市场     " + FormatOptionalRoubles(result.fleaPrice);
    const std::wstring trader = L"商人最高     " + TraderText(result.bestTrader);
    const std::wstring slot = L"单格价值     " + (result.valuePerSlot.has_value()
        ? FormatRoubles(static_cast<std::int64_t>(*result.valuePerSlot)) : L"未知");
    const std::wstring flea_status = L"跳蚤状态     " +
        std::wstring(FleaStatusDisplayName(result.fleaStatus));
    const std::wstring size = result.width > 0 && result.height > 0
        ? L"尺寸 " + std::to_wstring(result.width) + L"×" + std::to_wstring(result.height)
        : L"";

    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
    render_target_->FillRoundedRectangle(card, background_brush_.Get());
    render_target_->DrawRoundedRectangle(card, border_brush_.Get(), 1.0F);
    render_target_->DrawText(
        title.c_str(), static_cast<UINT32>(title.size()), title_format_.Get(),
        D2D1::RectF(16.0F, 12.0F, width - 16.0F, 42.0F), title_brush_.Get());
    render_target_->DrawText(
        mode.c_str(), static_cast<UINT32>(mode.size()), mode_format_.Get(),
        D2D1::RectF(16.0F, 44.0F, width - 16.0F, 64.0F), label_brush_.Get());
    const std::array<std::wstring, 4> rows{flea, trader, slot, flea_status};
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const float top = 68.0F + static_cast<float>(index) * 22.0F;
        render_target_->DrawText(
            rows[index].c_str(), static_cast<UINT32>(rows[index].size()), body_format_.Get(),
            D2D1::RectF(16.0F, top, width - 16.0F, top + 22.0F), value_brush_.Get());
    }
    if (!size.empty()) {
        render_target_->DrawText(
            size.c_str(), static_cast<UINT32>(size.size()), mode_format_.Get(),
            D2D1::RectF(width - 80.0F, 44.0F, width - 16.0F, 64.0F), label_brush_.Get());
    }
    const HRESULT end_result = render_target_->EndDraw();
    if (end_result == D2DERR_RECREATE_TARGET) {
        render_target_.Reset();
        background_brush_.Reset();
        border_brush_.Reset();
        title_brush_.Reset();
        label_brush_.Reset();
        value_brush_.Reset();
        return true;
    }
    if (FAILED(end_result)) {
        error = L"Direct2D overlay drawing failed";
        return false;
    }
    return true;
}

} // namespace noven::overlay
