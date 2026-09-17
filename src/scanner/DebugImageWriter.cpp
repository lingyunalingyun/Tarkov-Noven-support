#include "scanner/DebugImageWriter.h"

#include <windows.h>

#include <fstream>

namespace noven::scanner {

bool WriteDebugBmp(
    const std::filesystem::path& path,
    const capture::CapturedFrame& frame,
    std::wstring& error
) {
    const std::size_t expected_size = static_cast<std::size_t>(frame.stride) * frame.height;
    if (frame.width == 0 || frame.height == 0 || frame.stride < frame.width * 4
        || frame.bgra.size() < expected_size) {
        error = L"Captured frame has invalid dimensions or pixel data";
        return false;
    }

    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    if (directory_error) {
        error = L"Could not create debug output directory";
        return false;
    }

    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits + static_cast<DWORD>(expected_size);

    BITMAPINFOHEADER info_header{};
    info_header.biSize = sizeof(BITMAPINFOHEADER);
    info_header.biWidth = static_cast<LONG>(frame.width);
    info_header.biHeight = -static_cast<LONG>(frame.height);
    info_header.biPlanes = 1;
    info_header.biBitCount = 32;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = static_cast<DWORD>(expected_size);

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = L"Could not open debug image file";
        return false;
    }

    output.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    output.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));
    output.write(
        reinterpret_cast<const char*>(frame.bgra.data()),
        static_cast<std::streamsize>(expected_size)
    );
    if (!output) {
        error = L"Could not write debug image file";
        return false;
    }

    return true;
}

} // namespace noven::scanner
