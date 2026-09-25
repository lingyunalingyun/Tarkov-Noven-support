#pragma once

// DXGI Desktop Duplication 会话在启动时建立，并跨 F2 扫描复用。
// The DXGI Desktop Duplication session is initialized at startup and reused across F2 scans.

#include "capture/ICaptureBackend.h"

#include <memory>

namespace noven::capture {

class DxgiDesktopDuplicationBackend final : public ICaptureBackend {
public:
    DxgiDesktopDuplicationBackend();
    ~DxgiDesktopDuplicationBackend() override;

    DxgiDesktopDuplicationBackend(const DxgiDesktopDuplicationBackend&) = delete;
    DxgiDesktopDuplicationBackend& operator=(const DxgiDesktopDuplicationBackend&) = delete;

    bool Initialize() override;
    CaptureResult Capture(const Rect& roi) override;
    [[nodiscard]] const wchar_t* Name() const noexcept override;

private:
    struct State;

    // 仅初始化或失去访问权限后重建；空闲时不持续抓屏。
    // Initialize once or rebuild after access loss; never capture continuously while idle.
    bool InitializeSession();
    void ResetSession();
    CaptureResult CaptureInitialized(const Rect& roi);

    std::unique_ptr<State> state_;
};

} // namespace noven::capture
