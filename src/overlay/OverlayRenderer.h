#pragma once

// Direct2D/DirectWrite 仅负责测量与绘制 ScanDisplayResult，不参与 OCR 或目录决策。
// Direct2D/DirectWrite measures and paints ScanDisplayResult, never OCR or catalog decisions.

#include "overlay/OverlayTypes.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

namespace noven::overlay {

class OverlayRenderer final {
public:
    bool Initialize(std::wstring& error);
    void Shutdown() noexcept;

    [[nodiscard]] SIZE Measure(const ScanDisplayResult& result) const;
    bool Render(
        HWND window,
        const ScanDisplayResult& result,
        std::wstring& error
    );

private:
    // 渲染目标随窗口尺寸变化复用或重建，结果卡窗口本身保持常驻。
    // Reuse or recreate the render target as needed; the card window stays persistent.
    bool EnsureRenderTarget(HWND window, UINT width, UINT height, std::wstring& error);

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> title_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> body_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> mode_format_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> title_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> label_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> value_brush_;
};

} // namespace noven::overlay
