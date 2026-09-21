#include "capture/DxgiDesktopDuplicationBackend.h"

#include "common/DebugLog.h"

#include <d3d11.h>
#include <d3dcompiler.h>
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
    ComPtr<ID3D11Texture2D> cached_frame_texture;
    bool cached_frame_valid{};
    UINT cached_width{};
    UINT cached_height{};
    DXGI_FORMAT cached_format{};
    ComPtr<ID3D11Texture2D> staging_texture;
    ComPtr<ID3D11Texture2D> conversion_source_texture;
    ComPtr<ID3D11ShaderResourceView> conversion_source_view;
    ComPtr<ID3D11Texture2D> conversion_target_texture;
    ComPtr<ID3D11RenderTargetView> conversion_target_view;
    ComPtr<ID3D11Texture2D> conversion_staging_texture;
    bool frame_format_logged{};
    DXGI_FORMAT last_frame_format{};
    UINT staging_width{};
    UINT staging_height{};
    DXGI_FORMAT staging_format{};
    UINT conversion_width{};
    UINT conversion_height{};
    DXGI_FORMAT conversion_format{};
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

std::wstring ShaderErrorMessage(const wchar_t* operation, ID3DBlob* error) {
    if (error == nullptr) {
        return operation;
    }
    const auto* message = static_cast<const char*>(error->GetBufferPointer());
    const std::string text(message, error->GetBufferSize());
    return std::wstring(operation) + L": "
        + std::wstring(text.begin(), text.end());
}

const wchar_t* DxgiFormatName(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return L"DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return L"DXGI_FORMAT_R10G10B10A2_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return L"DXGI_FORMAT_R16G16B16A16_FLOAT";
    default:
        return L"DXGI_FORMAT_UNKNOWN";
    }
}

bool SupportsNativeConversion(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R10G10B10A2_UNORM
        || format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

bool EnsureCachedFrameTexture(
    ID3D11Device* device,
    OutputCapture& output,
    const D3D11_TEXTURE2D_DESC& source_desc,
    std::wstring& error
) {
    if (output.cached_frame_texture != nullptr
        && output.cached_width == source_desc.Width
        && output.cached_height == source_desc.Height
        && output.cached_format == source_desc.Format) {
        return true;
    }

    output.cached_frame_texture.Reset();
    D3D11_TEXTURE2D_DESC cached_desc = source_desc;
    cached_desc.Usage = D3D11_USAGE_DEFAULT;
    cached_desc.BindFlags = 0;
    cached_desc.CPUAccessFlags = 0;
    cached_desc.MiscFlags = 0;
    const HRESULT hr = device->CreateTexture2D(
        &cached_desc,
        nullptr,
        &output.cached_frame_texture
    );
    if (FAILED(hr)) {
        error = HResultMessage(L"Create cached desktop texture", hr);
        return false;
    }

    output.cached_frame_valid = false;
    output.cached_width = source_desc.Width;
    output.cached_height = source_desc.Height;
    output.cached_format = source_desc.Format;
    return true;
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
    ComPtr<ID3D11VertexShader> conversion_vertex_shader;
    ComPtr<ID3D11PixelShader> conversion_pixel_shader;
    ComPtr<ID3D11Buffer> conversion_parameters;
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

    constexpr char conversion_vertex_shader_source[] = R"(
        struct VSOutput {
            float4 position : SV_Position;
        };

        VSOutput main(uint vertex_id : SV_VertexID) {
            float2 positions[3] = {
                float2(-1.0, -1.0),
                float2(-1.0,  3.0),
                float2( 3.0, -1.0)
            };
            VSOutput output;
            output.position = float4(positions[vertex_id], 0.0, 1.0);
            return output;
        }
    )";
    constexpr char conversion_pixel_shader_source[] = R"(
        Texture2D source_texture : register(t0);
        cbuffer ConversionParameters : register(b0) {
            uint tone_map;
            uint3 padding;
        };

        struct VSOutput {
            float4 position : SV_Position;
        };

        float4 main(VSOutput input) : SV_Target {
            uint2 pixel = uint2(input.position.xy);
            float3 color = source_texture.Load(int3(pixel, 0)).rgb;
            if (tone_map != 0) {
                color = max(color, 0.0);
                color = color / (1.0 + color);
            }
            return float4(saturate(color), 1.0);
        }
    )";
    ComPtr<ID3DBlob> vertex_shader_blob;
    ComPtr<ID3DBlob> pixel_shader_blob;
    ComPtr<ID3DBlob> shader_error;
    hr = D3DCompile(
        conversion_vertex_shader_source,
        sizeof(conversion_vertex_shader_source) - 1,
        "noven_conversion_vs",
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        0,
        0,
        &vertex_shader_blob,
        &shader_error
    );
    if (FAILED(hr)) {
        return fail(
            shader_error != nullptr
                ? ShaderErrorMessage(L"D3DCompile vertex shader failed", shader_error.Get())
                : HResultMessage(L"D3DCompile vertex shader", hr)
        );
    }
    hr = state_->device->CreateVertexShader(
        vertex_shader_blob->GetBufferPointer(),
        vertex_shader_blob->GetBufferSize(),
        nullptr,
        &state_->conversion_vertex_shader
    );
    if (FAILED(hr)) {
        return fail(HResultMessage(L"Create conversion vertex shader", hr));
    }

    shader_error.Reset();
    hr = D3DCompile(
        conversion_pixel_shader_source,
        sizeof(conversion_pixel_shader_source) - 1,
        "noven_conversion_ps",
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        0,
        0,
        &pixel_shader_blob,
        &shader_error
    );
    if (FAILED(hr)) {
        return fail(
            shader_error != nullptr
                ? ShaderErrorMessage(L"D3DCompile pixel shader failed", shader_error.Get())
                : HResultMessage(L"D3DCompile pixel shader", hr)
        );
    }
    hr = state_->device->CreatePixelShader(
        pixel_shader_blob->GetBufferPointer(),
        pixel_shader_blob->GetBufferSize(),
        nullptr,
        &state_->conversion_pixel_shader
    );
    if (FAILED(hr)) {
        return fail(HResultMessage(L"Create conversion pixel shader", hr));
    }

    D3D11_BUFFER_DESC parameter_desc{};
    parameter_desc.ByteWidth = 16;
    parameter_desc.Usage = D3D11_USAGE_DYNAMIC;
    parameter_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    parameter_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = state_->device->CreateBuffer(
        &parameter_desc,
        nullptr,
        &state_->conversion_parameters
    );
    if (FAILED(hr)) {
        return fail(HResultMessage(L"Create conversion parameter buffer", hr));
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
        common::DebugLog(
            L"[capture] output " + std::to_wstring(output_index)
            + L" desktop format=" + DxgiFormatName(capture.format)
            + L" (" + std::to_wstring(static_cast<int>(capture.format)) + L")"
        );

        state_->outputs.push_back(std::move(capture));
    }

    if (state_->outputs.empty()) {
        return fail(L"No desktop output could be duplicated");
    }

    for (std::size_t output_index = 0; output_index < state_->outputs.size(); ++output_index) {
        OutputCapture& output = state_->outputs[output_index];
        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> resource;
        const HRESULT acquire_result = output.duplication->AcquireNextFrame(
            50,
            &frame_info,
            &resource
        );
        if (acquire_result == DXGI_ERROR_WAIT_TIMEOUT) {
            common::DebugLog(
                L"[capture] output " + std::to_wstring(output_index)
                + L" had no initial frame; cache will prime on first capture"
            );
            continue;
        }
        if (FAILED(acquire_result)) {
            common::DebugLog(
                HResultMessage(L"Prime cached desktop frame", acquire_result)
            );
            continue;
        }

        bool cached = false;
        {
            FrameGuard frame_guard{output.duplication.Get()};
            ComPtr<ID3D11Texture2D> source_texture;
            hr = resource.As(&source_texture);
            if (FAILED(hr)) {
                common::DebugLog(HResultMessage(L"Query initial desktop texture", hr));
                continue;
            }

            D3D11_TEXTURE2D_DESC source_desc{};
            source_texture->GetDesc(&source_desc);
            common::DebugLog(
                L"[capture] output " + std::to_wstring(output_index)
                + L" initial frame texture format=" + DxgiFormatName(source_desc.Format)
                + L" (" + std::to_wstring(static_cast<int>(source_desc.Format)) + L")"
            );
            std::wstring cache_error;
            if (!EnsureCachedFrameTexture(
                    state_->device.Get(),
                    output,
                    source_desc,
                    cache_error
                )) {
                common::DebugLog(cache_error);
                continue;
            }
            state_->context->CopyResource(
                output.cached_frame_texture.Get(),
                source_texture.Get()
            );
            state_->context->Flush();
            output.cached_frame_valid = true;
            cached = true;
        }
        if (cached) {
            common::DebugLog(
                L"[capture] output " + std::to_wstring(output_index)
                + L" initial DXGI frame cached"
            );
        }
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
    state_->conversion_pixel_shader.Reset();
    state_->conversion_vertex_shader.Reset();
    state_->conversion_parameters.Reset();
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
        CaptureResult compatibility_result = CaptureWithGdi(roi);
        compatibility_result.timings.acquire_ms += result.timings.acquire_ms;
        compatibility_result.timings.capture_to_memory_ms = ElapsedMilliseconds(capture_start);
        compatibility_result.source = CaptureSource::GdiEmergency;
        compatibility_result.gdi_emergency_count = 1;
        return compatibility_result;
    };

    auto prepare_conversion_resources = [this](
        OutputCapture& output,
        DXGI_FORMAT source_format,
        UINT width,
        UINT height
    ) {
        if (output.conversion_width == width
            && output.conversion_height == height
            && output.conversion_format == source_format
            && output.conversion_source_texture != nullptr
            && output.conversion_source_view != nullptr
            && output.conversion_target_view != nullptr
            && output.conversion_staging_texture != nullptr) {
            return true;
        }

        output.conversion_source_texture.Reset();
        output.conversion_source_view.Reset();
        output.conversion_target_texture.Reset();
        output.conversion_target_view.Reset();
        output.conversion_staging_texture.Reset();

        D3D11_TEXTURE2D_DESC source_desc{};
        source_desc.Width = width;
        source_desc.Height = height;
        source_desc.MipLevels = 1;
        source_desc.ArraySize = 1;
        source_desc.Format = source_format;
        source_desc.SampleDesc.Count = 1;
        source_desc.Usage = D3D11_USAGE_DEFAULT;
        source_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = state_->device->CreateTexture2D(
            &source_desc,
            nullptr,
            &output.conversion_source_texture
        );
        if (FAILED(hr)) {
            common::DebugLog(HResultMessage(L"Create conversion source texture", hr));
            return false;
        }

        hr = state_->device->CreateShaderResourceView(
            output.conversion_source_texture.Get(),
            nullptr,
            &output.conversion_source_view
        );
        if (FAILED(hr)) {
            common::DebugLog(HResultMessage(L"Create conversion source view", hr));
            return false;
        }

        D3D11_TEXTURE2D_DESC target_desc = source_desc;
        target_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = state_->device->CreateTexture2D(
            &target_desc,
            nullptr,
            &output.conversion_target_texture
        );
        if (FAILED(hr)) {
            common::DebugLog(HResultMessage(L"Create conversion target texture", hr));
            return false;
        }

        hr = state_->device->CreateRenderTargetView(
            output.conversion_target_texture.Get(),
            nullptr,
            &output.conversion_target_view
        );
        if (FAILED(hr)) {
            common::DebugLog(HResultMessage(L"Create conversion target view", hr));
            return false;
        }

        D3D11_TEXTURE2D_DESC staging_desc = target_desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;
        hr = state_->device->CreateTexture2D(
            &staging_desc,
            nullptr,
            &output.conversion_staging_texture
        );
        if (FAILED(hr)) {
            common::DebugLog(HResultMessage(L"Create conversion staging texture", hr));
            return false;
        }

        output.conversion_width = width;
        output.conversion_height = height;
        output.conversion_format = source_format;
        return true;
    };

    auto prepare_direct_staging = [this](
        OutputCapture& output,
        const D3D11_TEXTURE2D_DESC& source_desc,
        UINT width,
        UINT height
    ) {
        if (output.staging_width == width
            && output.staging_height == height
            && output.staging_format == source_desc.Format
            && output.staging_texture != nullptr) {
            return true;
        }

        output.staging_texture.Reset();
        D3D11_TEXTURE2D_DESC staging_desc = source_desc;
        staging_desc.Width = width;
        staging_desc.Height = height;
        staging_desc.MipLevels = 1;
        staging_desc.ArraySize = 1;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;
        HRESULT hr = state_->device->CreateTexture2D(
            &staging_desc,
            nullptr,
            &output.staging_texture
        );
        if (FAILED(hr)) {
            common::DebugLog(HResultMessage(L"Create ROI staging texture", hr));
            return false;
        }

        output.staging_width = width;
        output.staging_height = height;
        output.staging_format = source_desc.Format;
        return true;
    };

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
        const HRESULT acquire_result = output.duplication->AcquireNextFrame(0, &frame_info, &resource);
        result.timings.acquire_ms += ElapsedMilliseconds(acquire_start);
        DXGI_FORMAT source_format = output.cached_format;
        if (acquire_result == DXGI_ERROR_WAIT_TIMEOUT) {
            if (!output.cached_frame_valid) {
                common::DebugLog(
                    L"[capture] no new DXGI frame and no cache; using emergency GDI compatibility capture"
                );
                return capture_with_compatibility();
            }
            ++result.dxgi_cached_frame_count;
        } else if (acquire_result == DXGI_ERROR_ACCESS_LOST) {
            state_->access_lost = true;
            result.error = L"DXGI desktop duplication access was lost; retry is required";
            return result;
        } else if (FAILED(acquire_result)) {
            result.error = HResultMessage(L"AcquireNextFrame", acquire_result);
            return result;
        } else {
            {
                FrameGuard frame_guard{output.duplication.Get()};
                ComPtr<ID3D11Texture2D> acquired_texture;
                HRESULT hr = resource.As(&acquired_texture);
                if (FAILED(hr)) {
                    result.error = HResultMessage(L"Query desktop texture", hr);
                    return result;
                }

                D3D11_TEXTURE2D_DESC source_desc{};
                acquired_texture->GetDesc(&source_desc);
                source_format = source_desc.Format;
                if (!output.frame_format_logged || output.last_frame_format != source_format) {
                    common::DebugLog(
                        L"[capture] output "
                        + std::to_wstring(static_cast<std::size_t>(
                            &output - state_->outputs.data()
                        ))
                        + L" frame texture format=" + DxgiFormatName(source_format)
                        + L" (" + std::to_wstring(static_cast<int>(source_format)) + L")"
                    );
                    output.frame_format_logged = true;
                    output.last_frame_format = source_format;
                }

                std::wstring cache_error;
                if (!EnsureCachedFrameTexture(
                        state_->device.Get(),
                        output,
                        source_desc,
                        cache_error
                    )) {
                    result.error = cache_error;
                    return result;
                }
                state_->context->CopyResource(
                    output.cached_frame_texture.Get(),
                    acquired_texture.Get()
                );
                state_->context->Flush();
                output.cached_frame_valid = true;
            }
            ++result.dxgi_new_frame_count;
        }

        ComPtr<ID3D11Texture2D> source_texture = output.cached_frame_texture;
        D3D11_TEXTURE2D_DESC source_desc{};
        source_texture->GetDesc(&source_desc);
        source_format = source_desc.Format;
        HRESULT hr{};
        if (source_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
            if (!prepare_direct_staging(
                    output,
                    source_desc,
                    static_cast<UINT>(intersection.Width()),
                    static_cast<UINT>(intersection.Height())
                )) {
                result.error = L"Could not initialize ROI staging texture";
                return result;
            }

            const std::size_t source_x = static_cast<std::size_t>(
                intersection.left - output.desktop_coordinates.left
            );
            const std::size_t source_y = static_cast<std::size_t>(
                intersection.top - output.desktop_coordinates.top
            );
            const D3D11_BOX source_box{
                static_cast<UINT>(source_x),
                static_cast<UINT>(source_y),
                0,
                static_cast<UINT>(source_x + static_cast<std::size_t>(intersection.Width())),
                static_cast<UINT>(source_y + static_cast<std::size_t>(intersection.Height())),
                1,
            };
            const auto roi_copy_start = std::chrono::steady_clock::now();
            state_->context->CopySubresourceRegion(
                output.staging_texture.Get(),
                0,
                0,
                0,
                0,
                source_texture.Get(),
                0,
                &source_box
            );
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
            const std::size_t copy_bytes = static_cast<std::size_t>(intersection.Width()) * 4;
            for (long row = 0; row < intersection.Height(); ++row) {
                auto* destination = result.frame.bgra.data()
                    + (destination_y + static_cast<std::size_t>(row)) * result.frame.stride
                    + destination_x * 4;
                const auto* source = static_cast<const std::uint8_t*>(mapped.pData)
                    + static_cast<std::size_t>(row) * mapped.RowPitch;
                std::memcpy(destination, source, copy_bytes);
            }
            state_->context->Unmap(output.staging_texture.Get(), 0);
            result.timings.roi_copy_ms += ElapsedMilliseconds(roi_copy_start);
            captured_any_output = true;
            continue;
        }

        if (!SupportsNativeConversion(source_desc.Format)) {
            common::DebugLog(
                L"[capture] unsupported desktop format "
                + std::wstring(DxgiFormatName(source_desc.Format))
                + L" (" + std::to_wstring(static_cast<int>(source_desc.Format))
                + L"); using emergency GDI compatibility capture"
            );
            return capture_with_compatibility();
        }

        const auto conversion_start = std::chrono::steady_clock::now();
        if (!prepare_conversion_resources(
                output,
                source_format,
                static_cast<UINT>(intersection.Width()),
                static_cast<UINT>(intersection.Height())
            )) {
            result.error = L"Could not initialize native HDR conversion resources";
            return result;
        }

        const std::size_t source_x = static_cast<std::size_t>(
            intersection.left - output.desktop_coordinates.left
        );
        const std::size_t source_y = static_cast<std::size_t>(
            intersection.top - output.desktop_coordinates.top
        );
        const D3D11_BOX source_box{
            static_cast<UINT>(source_x),
            static_cast<UINT>(source_y),
            0,
            static_cast<UINT>(source_x + static_cast<std::size_t>(intersection.Width())),
            static_cast<UINT>(source_y + static_cast<std::size_t>(intersection.Height())),
            1,
        };
        state_->context->CopySubresourceRegion(
            output.conversion_source_texture.Get(),
            0,
            0,
            0,
            0,
            source_texture.Get(),
            0,
            &source_box
        );
        result.timings.roi_copy_ms += ElapsedMilliseconds(conversion_start);

        const auto shader_start = std::chrono::steady_clock::now();
        struct ConversionParameters final {
            UINT tone_map;
            UINT padding[3];
        } parameters{
            source_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1U : 0U,
            {0U, 0U, 0U},
        };
        D3D11_MAPPED_SUBRESOURCE mapped_parameters{};
        hr = state_->context->Map(
            state_->conversion_parameters.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped_parameters
        );
        if (FAILED(hr)) {
            result.error = HResultMessage(L"Map conversion parameter buffer", hr);
            return result;
        }
        std::memcpy(mapped_parameters.pData, &parameters, sizeof(parameters));
        state_->context->Unmap(state_->conversion_parameters.Get(), 0);

        ID3D11RenderTargetView* target_view = output.conversion_target_view.Get();
        state_->context->OMSetRenderTargets(1, &target_view, nullptr);
        const D3D11_VIEWPORT viewport{
            0.0F,
            0.0F,
            static_cast<float>(intersection.Width()),
            static_cast<float>(intersection.Height()),
            0.0F,
            1.0F,
        };
        state_->context->RSSetViewports(1, &viewport);
        state_->context->IASetInputLayout(nullptr);
        state_->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        state_->context->VSSetShader(state_->conversion_vertex_shader.Get(), nullptr, 0);
        state_->context->PSSetShader(state_->conversion_pixel_shader.Get(), nullptr, 0);
        ID3D11ShaderResourceView* source_view = output.conversion_source_view.Get();
        state_->context->PSSetShaderResources(0, 1, &source_view);
        ID3D11Buffer* parameter_buffer = state_->conversion_parameters.Get();
        state_->context->PSSetConstantBuffers(0, 1, &parameter_buffer);
        state_->context->Draw(3, 0);

        ID3D11ShaderResourceView* null_source_view = nullptr;
        state_->context->PSSetShaderResources(0, 1, &null_source_view);
        state_->context->OMSetRenderTargets(0, nullptr, nullptr);
        state_->context->CopyResource(
            output.conversion_staging_texture.Get(),
            output.conversion_target_texture.Get()
        );
        state_->context->Flush();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = state_->context->Map(
            output.conversion_staging_texture.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped
        );
        if (FAILED(hr)) {
            result.error = HResultMessage(L"Map converted staging texture", hr);
            return result;
        }

        const std::size_t destination_x = static_cast<std::size_t>(intersection.left - roi.left);
        const std::size_t destination_y = static_cast<std::size_t>(intersection.top - roi.top);
        const std::size_t copy_bytes = static_cast<std::size_t>(intersection.Width()) * 4;
        for (long row = 0; row < intersection.Height(); ++row) {
            auto* destination = result.frame.bgra.data()
                + (destination_y + static_cast<std::size_t>(row)) * result.frame.stride
                + destination_x * 4;
            const auto* source = static_cast<const std::uint8_t*>(mapped.pData)
                + static_cast<std::size_t>(row) * mapped.RowPitch;
            std::memcpy(destination, source, copy_bytes);
        }
        state_->context->Unmap(output.conversion_staging_texture.Get(), 0);
        result.timings.format_conversion_ms += ElapsedMilliseconds(shader_start);
        captured_any_output = true;
    }

    if (!captured_any_output) {
        result.error = L"The ROI does not intersect an attached desktop output";
        result.frame = {};
    }
    if (result.gdi_emergency_count != 0) {
        result.source = CaptureSource::GdiEmergency;
    } else if (result.dxgi_new_frame_count != 0) {
        result.source = CaptureSource::DxgiNewFrame;
    } else if (result.dxgi_cached_frame_count != 0) {
        result.source = CaptureSource::DxgiCachedFrame;
    }
    result.timings.capture_to_memory_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - capture_start
    ).count();
    return result;
}

} // namespace noven::capture
