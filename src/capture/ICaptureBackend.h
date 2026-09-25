#pragma once

// 扫描器只依赖捕获接口，不依赖 DXGI 设备、纹理或调试图像文件。
// The scanner depends on this capture interface, not DXGI devices, textures, or debug files.

#include "capture/CaptureTypes.h"

namespace noven::capture {

class ICaptureBackend {
public:
    virtual ~ICaptureBackend() = default;

    virtual bool Initialize() = 0;
    virtual CaptureResult Capture(const Rect& roi) = 0;
    [[nodiscard]] virtual const wchar_t* Name() const noexcept = 0;
};

} // namespace noven::capture
