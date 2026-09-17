#pragma once

#include "capture/ICaptureBackend.h"

#include <filesystem>

namespace noven::scanner {

class ScanTrigger final {
public:
    explicit ScanTrigger(capture::ICaptureBackend& capture_backend);

    void SetOutputDirectory(std::filesystem::path output_directory);
    void SetRoiSize(capture::Size roi_size);
    void Trigger();

private:
    capture::ICaptureBackend& capture_backend_;
    std::filesystem::path output_directory_;
    capture::Size roi_size_{800, 600};
    unsigned long capture_number_{};
};

} // namespace noven::scanner
