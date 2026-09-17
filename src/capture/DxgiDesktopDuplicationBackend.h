#pragma once

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

    bool InitializeSession();
    void ResetSession();
    CaptureResult CaptureInitialized(const Rect& roi);

    std::unique_ptr<State> state_;
};

} // namespace noven::capture
