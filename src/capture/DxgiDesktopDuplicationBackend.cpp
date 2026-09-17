#include "capture/DxgiDesktopDuplicationBackend.h"

#include "common/DebugLog.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace noven::capture {

using Microsoft::WRL::ComPtr;

namespace {

struct OutputCapture final {
    RECT desktop_coordinates{};
    DXGI_FORMAT format{};
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging_texture;
};

struct FrameGuard final {
    IDXGIOutputDuplication* duplication{};

    ~FrameGuard() {
        if (duplication != nullptr) {
            duplication->ReleaseFrame();
        }
    }
};

std::wstring HResultMessage(const wchar_t* operation, HRESULT result) {
    std::wostringstream stream;
    stream << operation << L" failed (HRESULT=0x" << std::hex
           << static_cast<unsigned long>(result) << L")";
    return stream.str();
}

bool Intersect(const Rect& first, const RECT& second, Rect& result) {
    result.left = std::max(first.left, static_cast<long>(second.left));
    result.top = std::max(first.top, static_cast<long>(second.top));
    result.right = std::min(first.right, static_cast<long>(second.right));
    result.bottom = std::min(first.bottom, static_cast<long>(second.bottom));
    return !result.Empty();
}

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count();
}

CaptureResult CaptureWithGdi(const Rect& roi) {
    CaptureResult result;
    const auto capture_start = std::chrono::steady_clock::now();
    const int width = static_cast<int>(roi.Width());
    const int height = static_cast<int>(roi.Height());
    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        result.error = L"GetDC failed for compatibility capture";
        return result;
    }

    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    if (memory_dc == nullptr || bitmap == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory_dc != nullptr) {
            DeleteDC(memory_dc);
        }
        ReleaseDC(nullptr, screen_dc);
        result.error = L"Could not create compatibility capture surface";
        return result;
    }

    HGDIOBJ previous_bitmap = SelectObject(memory_dc, bitmap);
    const BOOL copied = BitBlt(
        memory_dc,
        0,
        0,
        width,
        height,
        screen_dc,
        static_cast<int>(roi.left),
        static_cast<int>(roi.top),
        SRCCOPY | CAPTUREBLT
    );
    result.timings.acquire_ms = ElapsedMilliseconds(capture_start);
    SelectObject(memory_dc, previous_bitmap);

    result.frame.width = static_cast<std::uint32_t>(width);
    result.frame.height = static_cast<std::uint32_t>(height);
    result.frame.stride = static_cast<std::uint32_t>(width * 4);
    result.frame.bgra.resize(static_cast<std::size_t>(result.frame.stride) * result.frame.height);

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    const auto roi_copy_start = std::chrono::steady_clock::now();
    const int rows = copied == FALSE
        ? 0
        : GetDIBits(
            memory_dc,
            bitmap,
            0,
            static_cast<UINT>(height),
            result.frame.bgra.data(),
            &bitmap_info,
            DIB_RGB_COLORS
        );
    result.timings.roi_copy_ms = ElapsedMilliseconds(roi_copy_start);
    result.timings.capture_to_memory_ms = ElapsedMilliseconds(capture_start);

    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);

    if (rows != height) {
        result.frame = {};
        result.error = L"Compatibility capture did not return all bitmap rows";
    }
    return result;
}

} // namespace

struct DxgiDesktopDuplicationBackend::State final {
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::vector<OutputCapture> outputs;
    bool initialized{};
    bool access_lost{};
    std::wstring initialization_error;
};

DxgiDesktopDuplicationBackend::DxgiDesktopDuplicationBackend()
    : state_(std::make_unique<State>()) {}

DxgiDesktopDuplicationBackend::~DxgiDesktopDuplicationBackend() = default;

const wchar_t* DxgiDesktopDuplicationBackend::Name() const noexcept {
    return L"DXGI Desktop Duplication";
}

bool DxgiDesktopDuplicationBackend::Initialize() {
    if (state_->initialized) {
        return true;
    }
    return InitializeSession();
}

bool DxgiDesktopDuplicationBackend::InitializeSession() {
    ResetSession();
    state_->initialization_error.clear();

    auto fail = [this](std::wstring error) {
        state_->initialization_error = std::move(error);
        common::DebugLog(L"[capture] " + state_->initialization_error);
        ResetSession();
        return false;
    };

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return fail(HResultMessage(L"CreateDXGIFactory1", hr));
    }

    ComPtr<IDXGIAdapter1> selected_adapter;
    for (UINT adapter_index = 0; ; ++adapter_index) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(adapter_index, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            return fail(HResultMessage(L"EnumAdapters1", hr));
        }

        DXGI_ADAPTER_DESC1 adapter_desc{};
        if (FAILED(adapter->GetDesc1(&adapter_desc))) {
            continue;
        }
        if ((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        selected_adapter = adapter;
        break;
    }

    if (selected_adapter == nullptr) {
        return fail(L"No hardware DXGI adapter is available");
    }

    const D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL selected_feature_level{};
    constexpr UINT device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = D3D11CreateDevice(
        selected_adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        device_flags,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        &state_->device,
        &selected_feature_level,
        &state_->context
    );
    if (FAILED(hr)) {
        return fail(HResultMessage(L"D3D11CreateDevice", hr));
    }

    state_->factory = factory;
    state_->adapter = selected_adapter;

    for (UINT output_index = 0; ; ++output_index) {
        ComPtr<IDXGIOutput> output;
        hr = selected_adapter->EnumOutputs(output_index, &output);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            return fail(HResultMessage(L"EnumOutputs", hr));
        }

        DXGI_OUTPUT_DESC output_desc{};
        if (FAILED(output->GetDesc(&output_desc)) || !output_desc.AttachedToDesktop) {
            continue;
        }

        ComPtr<IDXGIOutput1> output_v1;
        hr = output.As(&output_v1);
        if (FAILED(hr)) {
            continue;
        }

        OutputCapture capture;
        capture.desktop_coordinates = output_desc.DesktopCoordinates;
        hr = output_v1->DuplicateOutput(state_->device.Get(), &capture.duplication);
        if (FAILED(hr)) {
            common::DebugLog(HResultMessage(L"DuplicateOutput", hr));
            continue;
        }

        DXGI_OUTDUPL_DESC duplication_desc{};
        capture.duplication->GetDesc(&duplication_desc);
        capture.format = duplication_desc.ModeDesc.Format;

        if (capture.format == DXGI_FORMAT_B8G8R8A8_UNORM) {
            D3D11_TEXTURE2D_DESC staging_desc{};
            staging_desc.Width = duplication_desc.ModeDesc.Width;
            staging_desc.Height = duplication_desc.ModeDesc.Height;
            staging_desc.MipLevels = 1;
            staging_desc.ArraySize = 1;
            staging_desc.Format = capture.format;
            staging_desc.SampleDesc.Count = 1;
            staging_desc.Usage = D3D11_USAGE_STAGING;
            staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            hr = state_->device->CreateTexture2D(
                &staging_desc,
                nullptr,
                &capture.staging_texture
            );
            if (FAILED(hr)) {
                return fail(HResultMessage(L"Create staging texture", hr));
            }
        }

        state_->outputs.push_back(std::move(capture));
    }

    if (state_->outputs.empty()) {
        return fail(L"No desktop output could be duplicated");
    }

    state_->initialized = true;
    common::DebugLog(
        L"[capture] initialized DXGI session with "
        + std::to_wstring(state_->outputs.size()) + L" desktop output(s)"
    );
    return true;
}

void DxgiDesktopDuplicationBackend::ResetSession() {
    state_->outputs.clear();
    state_->context.Reset();
    state_->device.Reset();
    state_->adapter.Reset();
    state_->factory.Reset();
    state_->initialized = false;
    state_->access_lost = false;
}

CaptureResult DxgiDesktopDuplicationBackend::Capture(const Rect& roi) {
    CaptureResult result;
    if (roi.Empty()) {
        result.error = L"Capture ROI is empty";
        return result;
    }

    if (!Initialize()) {
        result.error = state_->initialization_error.empty()
            ? L"DXGI capture session initialization failed"
            : state_->initialization_error;
        return result;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        state_->access_lost = false;
        result = CaptureInitialized(roi);
        if (!state_->access_lost) {
            return result;
        }

        common::DebugLog(L"[capture] access lost; rebuilding DXGI session");
        ResetSession();
        if (attempt == 0 && Initialize()) {
            continue;
        }
        return result;
    }

    result.error = L"DXGI capture retry limit reached";
    return result;
}

CaptureResult DxgiDesktopDuplicationBackend::CaptureInitialized(const Rect& roi) {
    CaptureResult result;
    const auto capture_start = std::chrono::steady_clock::now();
    auto capture_with_compatibility = [&]() {
        return CaptureWithGdi(roi);
    };

    for (const OutputCapture& output : state_->outputs) {
        Rect intersection{};
        if (Intersect(roi, output.desktop_coordinates, intersection)
            && output.format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            common::DebugLog(L"[capture] non-BGRA8 output; using GDI compatibility capture");
            return capture_with_compatibility();
        }
    }

    const long width = roi.Width();
    const long height = roi.Height();
    result.frame.width = static_cast<std::uint32_t>(width);
    result.frame.height = static_cast<std::uint32_t>(height);
    result.frame.stride = static_cast<std::uint32_t>(width * 4);
    result.frame.bgra.resize(static_cast<std::size_t>(result.frame.stride) * result.frame.height);

    bool captured_any_output = false;
    for (OutputCapture& output : state_->outputs) {
        Rect intersection{};
        if (!Intersect(roi, output.desktop_coordinates, intersection)) {
            continue;
        }

        const auto acquire_start = std::chrono::steady_clock::now();
        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> resource;
        const HRESULT acquire_result = output.duplication->AcquireNextFrame(50, &frame_info, &resource);
        result.timings.acquire_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - acquire_start
        ).count();
        if (acquire_result == DXGI_ERROR_WAIT_TIMEOUT) {
            common::DebugLog(L"[capture] no new DXGI frame; using GDI compatibility capture");
            return capture_with_compatibility();
        }
        if (acquire_result == DXGI_ERROR_ACCESS_LOST) {
            state_->access_lost = true;
            result.error = L"DXGI desktop duplication access was lost; retry is required";
            return result;
        }
        if (FAILED(acquire_result)) {
            result.error = HResultMessage(L"AcquireNextFrame", acquire_result);
            return result;
        }
        FrameGuard frame_guard{output.duplication.Get()};

        ComPtr<ID3D11Texture2D> source_texture;
        HRESULT hr = resource.As(&source_texture);
        if (FAILED(hr)) {
            result.error = HResultMessage(L"Query desktop texture", hr);
            return result;
        }

        D3D11_TEXTURE2D_DESC source_desc{};
        source_texture->GetDesc(&source_desc);
        if (source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            common::DebugLog(L"[capture] output format changed; using GDI compatibility capture");
            CaptureResult compatibility_result = capture_with_compatibility();
            if (compatibility_result.Succeeded()) {
                return compatibility_result;
            }
            result.error = compatibility_result.error;
            return result;
        }
        if (output.staging_texture == nullptr) {
            result.error = L"DXGI staging texture is not initialized";
            return result;
        }

        const auto roi_copy_start = std::chrono::steady_clock::now();
        state_->context->CopyResource(output.staging_texture.Get(), source_texture.Get());
        state_->context->Flush();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = state_->context->Map(
            output.staging_texture.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped
        );
        if (FAILED(hr)) {
            result.error = HResultMessage(L"Map staging texture", hr);
            return result;
        }

        const std::size_t destination_x = static_cast<std::size_t>(intersection.left - roi.left);
        const std::size_t destination_y = static_cast<std::size_t>(intersection.top - roi.top);
        const std::size_t source_x = static_cast<std::size_t>(
            intersection.left - output.desktop_coordinates.left
        );
        const std::size_t source_y = static_cast<std::size_t>(
            intersection.top - output.desktop_coordinates.top
        );
        const std::size_t copy_bytes = static_cast<std::size_t>(intersection.Width()) * 4;
        for (long row = 0; row < intersection.Height(); ++row) {
            auto* destination = result.frame.bgra.data()
                + (destination_y + static_cast<std::size_t>(row)) * result.frame.stride
                + destination_x * 4;
            const auto* source = static_cast<const std::uint8_t*>(mapped.pData)
                + (source_y + static_cast<std::size_t>(row)) * mapped.RowPitch
                + source_x * 4;
            std::memcpy(destination, source, copy_bytes);
        }
        state_->context->Unmap(output.staging_texture.Get(), 0);
        result.timings.roi_copy_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - roi_copy_start
        ).count();
        captured_any_output = true;
    }

    if (!captured_any_output) {
        result.error = L"The ROI does not intersect an attached desktop output";
        result.frame = {};
    }
    result.timings.capture_to_memory_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - capture_start
    ).count();
    return result;
}

} // namespace noven::capture
