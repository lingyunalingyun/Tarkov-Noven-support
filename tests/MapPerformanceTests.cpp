#include "ui/MapPage.h"
#include "ui/localization/LocalizationService.h"
#include "data/InterchangeReference.h"
#include <psapi.h>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace noven::ui;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
constexpr float Width = 1280, Height = 800;
constexpr int Frames = 120;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void CheckHr(HRESULT result, const char* message) { Check(SUCCEEDED(result), message); }
double CpuMilliseconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    Check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE,
        "GetProcessTimes failed");
    const auto ticks = [](FILETIME value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000;
}
void Memory(const char* stage) {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    Check(GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != FALSE,
        "GetProcessMemoryInfo failed");
    std::cout << "memory " << stage << " private_bytes=" << counters.PrivateUsage
        << " working_set_bytes=" << counters.WorkingSetSize << '\n';
}
template<class Work>
void Measure(const char* stage, int count, Work work) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(count));
    const double cpu = CpuMilliseconds();
    for (int frame = 0; frame < count; ++frame) {
        const auto start = Clock::now();
        work(frame);
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    const double cpuUsed = CpuMilliseconds() - cpu;
    const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double fraction) {
        return samples[static_cast<std::size_t>(std::ceil(fraction * count)) - 1];
    };
    std::cout << "timing " << stage << " samples=" << count << " total_ms=" << total
        << " mean_ms=" << total / count << " p50_ms=" << percentile(.50)
        << " p95_ms=" << percentile(.95) << " max_ms=" << samples.back()
        << " process_cpu_ms=" << cpuUsed << '\n';
    Memory(stage);
}
struct Apartment {
    Apartment() { CheckHr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "COM initialization failed"); }
    ~Apartment() { CoUninitialize(); }
};
struct Surface {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> dwrite;
    std::array<ComPtr<IDWriteTextFormat>, 5> formats;

    Surface() {
        CheckHr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wic)), "WIC factory failed");
        CheckHr(wic->CreateBitmap(static_cast<UINT>(Width), static_cast<UINT>(Height),
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap), "WIC bitmap failed");
        CheckHr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()), "D2D factory failed");
        CheckHr(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96, 96), &target), "Software WIC render target failed");
        CheckHr(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush), "Brush failed");
        CheckHr(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwrite.GetAddressOf())), "DirectWrite factory failed");
        const std::array<float, 5> sizes{25, 36, 16, 15, 12};
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            CheckHr(dwrite->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                i < 3 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sizes[i], L"zh-CN",
                &formats[i]), "Text format failed");
            formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            formats[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    void Draw(MapPage& page, const UiTheme& theme) {
        // 完整页面使用软件 WIC target；EndDraw 纳入计时，没有窗口呈现或 GPU FPS。
        // Time the full page through EndDraw on a software WIC target; no presentation/GPU FPS.
        page.Prepare(Width, Height, theme);
        const UiCanvas canvas{*target.Get(), *brush.Get(), *formats[0].Get(), *formats[1].Get(),
            *formats[2].Get(), *formats[3].Get(), *formats[4].Get(), dwrite.Get()};
        target->BeginDraw();
        target->Clear(theme.background);
        page.Draw(canvas, theme);
        CheckHr(target->EndDraw(), "MapPage EndDraw failed");
    }
};
void Settle(MapPage& page, const UiTheme& theme) {
    for (int step = 0; step < 24; ++step) {
        page.Tick(1.0F / 60);
        page.Prepare(Width, Height, theme);
    }
}
D2D1_POINT_2F Center(const MapPage& page) {
    const auto bounds = page.Layout().viewport;
    return {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
}
bool Pan(MapPage& page, int frame) {
    const auto bounds = page.Layout().viewport;
    // 从真实标记之间找拖动起点，不直接改 viewport 或绕过页面输入路由。
    // Find a drag origin between real markers, retaining the page input route.
    for (float y = bounds.top + 40; y < bounds.bottom - 70; y += 32)
        for (float x = bounds.left + 40; x < bounds.right - 70; x += 32) {
            bool occupied = false;
            for (const auto& point : page.Points()) if (page.MarkerOpacity(point) > 0) {
                const auto position = page.Viewport().ToScreen(point.coordinate);
                if (std::hypot(position.x - x, position.y - y) < 16) { occupied = true; break; }
            }
            if (occupied) continue;
            const auto before = page.Viewport().ToMap(Center(page));
            const float delta = (frame / 30) % 2 == 0 ? 12.0F : -12.0F;
            page.MouseDown(x, y);
            page.MouseMove(x + delta, y + delta / 2);
            page.MouseUp(x + delta, y + delta / 2);
            const auto after = page.Viewport().ToMap(Center(page));
            if (before.x != after.x || before.y != after.y) return true;
        }
    return false;
}
}

int wmain(int argc, wchar_t** argv) try {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: NovenMapPerformanceTests <assets-directory> [map-id-or-normalized-name]\n";
        return 2;
    }
    const std::filesystem::path assets = std::filesystem::absolute(argv[1]);
    std::cout << std::fixed << std::setprecision(3)
        << "benchmark software_WIC 1280x800_DIP 96_DPI; no GPU FPS or live EFT claims\n";
    Apartment apartment;
    Memory("process_baseline");
    std::wstring error;
    Check(UiLocalization().DiscoverLocales(assets / L"i18n", error), "Localization load failed");
    UiTheme theme;
    Surface surface;
    Memory("render_resources");
    {
        MapPage page;
        Measure("initialize_application_cold", 1, [&](int) {
            if (!page.Initialize(assets, error)) {
                std::wcerr << error << '\n';
                throw std::runtime_error("MapPage initialization failed");
            }
        });
        Check(page.RealData(), "Benchmark requires production map data");
        if (argc == 3) {
            const std::wstring requested = argv[2];
            const auto found = std::find_if(page.Catalog().Maps().begin(), page.Catalog().Maps().end(),
                [&](const auto& map) {
                    return requested == std::wstring(map.id.begin(), map.id.end())
                        || requested == std::wstring(map.normalizedName.begin(), map.normalizedName.end());
                });
            Check(found != page.Catalog().Maps().end(), "Requested map absent from catalog");
            Measure("select_map", 1, [&](int) { page.SelectMap(found->id); });
        }
        const auto* selected = page.Information();
        Check(selected != nullptr, "Selected map information absent");
        std::vector<std::string> floors;
        for (const auto& floor : selected->floors) floors.push_back(floor.id);
        if (floors.empty() && selected->normalizedName == "interchange")
            for (const auto& floor : noven::data::InterchangeReference::Floors) floors.emplace_back(floor.id);
        Check(!floors.empty(), "Selected map has no supported floor/background reference");
        page.SelectFloor(floors.front());
        Settle(page, theme);
        Check(page.Selected() && page.FloorId() == floors.front(), "Floor selection failed");
        std::cout << "catalog maps=" << page.Catalog().Maps().size()
            << " total_points=" << page.Catalog().Points().size() << " map=" << selected->normalizedName
            << " floors=" << floors.size() << " style=abstract_with_upstream_fallback\n";
        Measure("query_cold", 1, [&](int) { Check(!page.Points().empty(), "Selected map has no markers"); });
        const auto queryBuilds = page.PointQueryBuilds();
        Measure("query_cached", Frames, [&](int) { (void)page.Points(); });
        Check(page.PointQueryBuilds() == queryBuilds, "Repeated queries rebuilt the point cache");
        std::cout << "query points=" << page.Points().size() << " builds=" << queryBuilds << '\n';
        // 隔离 CPU 布局开销，涵盖任务侧展及关闭后的静止状态；不混入像素绘制。
        // Isolate CPU layout work with task flyouts and their closed idle state, without pixel rendering.
        const auto taskPanel=page.Layout().filters[2];
        const float panelX=taskPanel.left+8,panelY=taskPanel.top+8;
        page.MouseDown(panelX,panelY);page.MouseUp(panelX,panelY);Settle(page,theme);
        Check(page.Panel()==MapFilterPanel::Tasks,"Task flyout workload opens");
        const auto openBuilds=page.FilterEntryBuilds();
        Measure("prepare_task_flyout",2000,[&](int){page.Prepare(Width,Height,theme);});
        Check(page.FilterEntryBuilds()==openBuilds,"Static task flyout rebuilt its entries");
        page.MouseDown(panelX,panelY);page.MouseUp(panelX,panelY);Settle(page,theme);
        Check(!page.Panel(),"Task flyout workload closes");
        const auto closedBuilds=page.FilterEntryBuilds();
        Measure("prepare_closed_flyout",2000,[&](int){page.Prepare(Width,Height,theme);});
        Check(page.FilterEntryBuilds()==closedBuilds,"Closed flyout rebuilt its entries");
        Measure("draw_first_target_cold", 1, [&](int) { surface.Draw(page, theme); });
        Measure("draw_cached_fit", Frames, [&](int) { surface.Draw(page, theme); });
        Check(page.PointQueryBuilds() == queryBuilds, "Static draws rebuilt the point query");
        const float beforeZoom = page.Viewport().Scale();
        const auto center = Center(page);
        Measure("zoom_cold_first_frame", 1, [&](int) {
            Check(page.Wheel(12 * WHEEL_DELTA, center.x, center.y), "Zoom event not handled");
            surface.Draw(page, theme);
        });
        Check(page.Viewport().Scale() > beforeZoom, "Zoom did not change the viewport scale");
        Measure("draw_cached_zoom", Frames, [&](int) { surface.Draw(page, theme); });
        int moved = 0;
        Measure("pan_and_draw", Frames, [&](int frame) {
            moved += Pan(page, frame) ? 1 : 0;
            surface.Draw(page, theme);
        });
        Check(moved > 0, "Pan workload never moved the viewport");
        std::cout << "interaction pan_moved_frames=" << moved << " zoom_scale=" << page.Viewport().Scale() << '\n';
        if (floors.size() > 1) {
            Measure("floor_switch_and_draw", Frames, [&](int frame) {
                if (frame % 30 == 0) page.SelectFloor(floors[(static_cast<std::size_t>(frame / 30) + 1) % floors.size()]);
                page.Tick(1.0F / 60);
                surface.Draw(page, theme);
            });
        } else std::cout << "skip floor_switch_and_draw: selected map has one floor\n";
        page.ReleaseDetailImages();
        Memory("detail_cache_released");
        Measure("reinitialize_warm_OS_library_caches", 1, [&](int) {
            Check(page.Initialize(assets, error), "Warm reinitialization failed");
        });
    }
    Memory("page_destroyed_render_resources_retained");
    std::cout << "COMPLETE: API/cache/input checks only; no timing limits or pixel assertions\n";
    return 0;
} catch (const std::exception& exception) {
    std::cerr << "ERROR: " << exception.what() << '\n';
    return 1;
}
