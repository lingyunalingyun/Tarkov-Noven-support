#pragma once

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
