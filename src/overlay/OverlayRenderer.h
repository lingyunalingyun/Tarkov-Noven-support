#pragma once

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
