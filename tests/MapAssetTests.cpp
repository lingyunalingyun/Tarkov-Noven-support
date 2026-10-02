#include "data/MapAssetUpdater.h"

#include <windows.h>
#include <winioctl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <latch>
#include <sstream>
#include <stdexcept>

using namespace noven::data;
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
std::vector<std::uint8_t> Png(BYTE red) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))) && SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "PNG fixture factory");
    Require(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr))
        && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(1, 1)), "PNG fixture encoder");
    auto format = GUID_WICPixelFormat24bppBGR;
    std::array<BYTE, 3> pixels{0, 0, red};
    Require(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat24bppBGR
        && SUCCEEDED(frame->WritePixels(1, 3, 3, pixels.data())) && SUCCEEDED(frame->Commit())
        && SUCCEEDED(encoder->Commit()), "PNG fixture encoding");
    STATSTG stat{};
    LARGE_INTEGER beginning{};
    Require(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && SUCCEEDED(stream->Seek(beginning, STREAM_SEEK_SET, nullptr)),
        "PNG fixture stream");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
    ULONG read{};
    Require(SUCCEEDED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) && read == bytes.size(), "PNG fixture read");
    return bytes;
}
void Write(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(output.good(), "fixture file write");
}
void WriteText(const std::filesystem::path& path, const std::string& text) {
    Write(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
std::uint32_t Be(const std::uint8_t* bytes) {
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) | (std::uint32_t(bytes[2]) << 8) | bytes[3];
}
void Put(std::uint8_t* bytes, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) { bytes[i] = static_cast<std::uint8_t>(value); value >>= 8; }
}
void FixCrc(std::vector<std::uint8_t>& bytes, std::size_t offset) {
    const auto length = Be(bytes.data() + offset);
    std::uint32_t crc = 0xffffffff;
    for (std::size_t i = offset + 4; i < offset + 8 + length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    Put(bytes.data() + offset + 8 + length, ~crc);
}
void Junction(const std::filesystem::path& link, const std::filesystem::path& target) {
    Require(CreateDirectoryW(link.c_str(), nullptr) != FALSE, "junction fixture directory");
    const auto printable = std::filesystem::absolute(target).wstring();
    const auto substitute = L"\\??\\" + printable;
    const WORD substituteBytes = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    const WORD printableBytes = static_cast<WORD>(printable.size() * sizeof(wchar_t));
    const WORD printableOffset = substituteBytes + sizeof(wchar_t);
    const WORD payloadBytes = 8 + printableOffset + printableBytes + sizeof(wchar_t);
    std::vector<BYTE> data(8 + payloadBytes);
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(data.data(), &tag, sizeof(tag));
    std::memcpy(data.data() + 4, &payloadBytes, sizeof(payloadBytes));
    std::memcpy(data.data() + 10, &substituteBytes, sizeof(substituteBytes));
    std::memcpy(data.data() + 12, &printableOffset, sizeof(printableOffset));
    std::memcpy(data.data() + 14, &printableBytes, sizeof(printableBytes));
    std::memcpy(data.data() + 16, substitute.c_str(), substituteBytes);
    std::memcpy(data.data() + 16 + printableOffset, printable.c_str(), printableBytes);
    const HANDLE directory = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    Require(directory != INVALID_HANDLE_VALUE, "junction fixture handle");
    DWORD returned{};
    const bool ok = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, data.data(), static_cast<DWORD>(data.size()),
        nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(directory);
    Require(ok, "junction fixture creation without symbolic-link privilege");
}
} // namespace

int main() {
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM initialization");
    // URL 来自仓库 DEV tilePath；所有响应均由本地假传输提供，不执行网络请求。
    // URL follows the repository's DEV tilePath; every response uses fake transport, never the network.
    const MapAssetSpec asset{"tiles/4/0/0.png", "https://assets.tarkov.dev/maps/interchange/main/4/0/0.png", ""};
    const auto first = Png(80), second = Png(160);
    Require(MapAssetStore::ValidatePng(first), "valid PNG fully decodes");
    Require(MapAssetStore::Sha256(std::array<std::uint8_t, 3>{'a','b','c'})
        == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "native SHA-256 known vector");
    Require(!MapAssetStore::ValidatePng({}) && !MapAssetStore::ValidatePng(std::array<std::uint8_t, 3>{1,2,3}),
        "empty and non-PNG rejected");
    auto bad = first;
    bad[29] ^= 1;
    Require(!MapAssetStore::ValidatePng(bad), "bad CRC rejected");
    bad = first; bad.pop_back();
    Require(!MapAssetStore::ValidatePng(bad), "truncated PNG rejected");
    bad = first; bad.push_back(0);
    Require(!MapAssetStore::ValidatePng(bad), "trailing payload rejected");
    bad = first; Put(bad.data() + 16, MapAssetStore::MaxDimension + 1); FixCrc(bad, 8);
    Require(!MapAssetStore::ValidatePng(bad), "dimension bomb rejected before decode");
    bad = first; Put(bad.data() + 16, 8192); Put(bad.data() + 20, 8192); FixCrc(bad, 8);
    Require(!MapAssetStore::ValidatePng(bad), "pixel budget enforced");
    bad = first;
    for (std::size_t offset = 8; offset + 12 <= bad.size();) {
        const auto length = Be(bad.data() + offset);
        if (std::string_view(reinterpret_cast<const char*>(bad.data() + offset + 4), 4) == "IDAT") {
            std::fill(bad.begin() + offset + 8, bad.begin() + offset + 8 + length, std::uint8_t{});
            FixCrc(bad, offset);
        }
        offset += length + 12;
    }
    Require(!MapAssetStore::ValidatePng(bad), "invalid zlib header rejected even with correct CRC");
    Require(!MapAssetStore::ValidatePng(std::vector<std::uint8_t>(MapAssetStore::MaxBytes + 1)), "encoded byte limit");
    for (const auto* path : {"../escape.png", "a/../escape.png", "/root.png", "C:/root.png", "a\\b.png", "a//b.png",
                             "a./b.png", "CON.png", "aux/data.png", "Lpt1.png", "a.png:stream", "a%2fb.png", "a.webp"})
        Require(!ValidMapAssetPath(path), "unsafe Windows path rejected");
    Require(ValidMapAssetPath(asset.relativePath) && ValidMapAssetUrl(asset.url), "canonical asset path/URL");
    for (const auto* url : {"http://assets.tarkov.dev/a.png", "https://assets.tarkov.dev.evil/a.png",
        "https://evil/assets.tarkov.dev/a.png", "https://assets.tarkov.dev@evil/a.png", "https://assets.tarkov.dev:443/a.png",
        "https://assets.tarkov.dev/a.png?x=y", "https://assets.tarkov.dev/%2e%2e/a.png", "https://assets.tarkov.dev/a.png#x"})
        Require(!ValidMapAssetUrl(url), "HTTPS host boundary enforced");
    std::vector<MapAssetSpec> entries;
    std::string error;
    auto parse = [&](const std::string& text) { std::istringstream input(text); return ParseMapAssetManifest(input, entries, error); };
    const auto row = asset.relativePath + "\t" + asset.url;
    Require(parse("\xef\xbb\xbfrelativePath\turl\tsha256\r\n" + row + "\t\r\n") && entries.size() == 1 && entries[0].sha256.empty(),
        "BOM/CRLF/empty final SHA column preserved");
    Require(parse("relativePath\turl\n" + row + "\n"), "two-column manifest");
    Require(!parse("relativePath\turl\n" + row + "\nTiles/4/0/0.png\t" + asset.url + "\n") && entries.empty(), "case-insensitive duplicates rejected atomically");
    Require(!parse("relativePath\turl\tsha256\n" + row + "\tbad\n"), "malformed SHA rejected");
    Require(!parse("relativePath\turl\n" + row + "\textra\n"), "extra fields rejected");
    Require(!parse("relativePath\turl\n../a.png\t" + asset.url + "\n"), "manifest traversal rejected");
    Require(!parse("relativePath\turl\n" + std::string(2400, 'a')), "manifest line bounded");
    Require(!parse("wrong\theader\n"), "bad header rejected");

    const auto root = std::filesystem::temp_directory_path() / (L"NovenMapAssets-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64()));
    const auto cache = root / "cache", fallback = root / "fallback", manifest = root / "assets.tsv";
    MapAssetStore store(cache, fallback);
    Write(fallback / asset.relativePath, first);
    Require(store.Resolve(asset.relativePath) == std::filesystem::absolute(fallback / asset.relativePath), "bundled offline fallback");
    WriteText(manifest, "relativePath\turl\tsha256\n" + row + "\t\n");
    const MapAssetValidators validators{"\"v1\"", "Wed, 01 Oct 2025 12:00:00 GMT"};
    int calls{};
    MapAssetUpdater initial(store, [&](const auto& request, auto) {
        ++calls;
        Require(request.validators.etag.empty() && request.validators.lastModified.empty(), "initial request unconditional");
        return MapAssetHttpResponse{200, first, validators};
    });
    const auto installed = initial.CheckOnce(manifest);
    Require(installed.updated == 0 && installed.unchanged == 1 && calls == 1 && installed.updatedPaths.empty(),
        "first cache download matching bundled PNG persists bytes/validators without triggering derived rebuild");
    Require(initial.CheckOnce(manifest).alreadyStarted && !initial.StartOnce(manifest) && calls == 1, "startup check only once");
    Require(store.Read(asset).validators.etag == validators.etag && store.Read(asset).bytes == first, "persistent validators bound to valid local PNG");
    Require(store.Resolve(asset.relativePath) == std::filesystem::absolute(cache / asset.relativePath), "cache overrides fallback");
    const auto stamp = std::filesystem::last_write_time(cache / asset.relativePath);
    MapAssetUpdater conditional(store, [&](const auto& request, auto) {
        Require(request.validators.etag == validators.etag && request.validators.lastModified == validators.lastModified, "ETag and Last-Modified supplied");
        return MapAssetHttpResponse{304, {}, {}};
    });
    Require(conditional.CheckOnce(manifest).unchanged == 1 && std::filesystem::last_write_time(cache / asset.relativePath) == stamp,
        "304 skips replacement");
    MapAssetUpdater equal(store, [&](const auto&, auto) { return MapAssetHttpResponse{200, first, validators}; });
    Require(equal.CheckOnce(manifest).unchanged == 1 && std::filesystem::last_write_time(cache / asset.relativePath) == stamp, "equal 200 skips PNG write");
    const auto digest = MapAssetStore::Sha256(first);
    WriteText(manifest, "relativePath\turl\tsha256\n" + row + "\t" + digest + "\n");
    MapAssetUpdater pinned(store, [&](const auto&, auto) { Require(false, "matching pinned hash must skip network"); return MapAssetHttpResponse{}; });
    Require(pinned.CheckOnce(manifest).unchanged == 1, "matching local pinned hash skips HTTP");
    WriteText(manifest, "relativePath\turl\tsha256\n" + row + "\t" + MapAssetStore::Sha256(second) + "\n");
    MapAssetUpdater mismatch(store, [&](const auto& request, auto) {
        Require(request.validators.etag.empty(), "changed manifest hash clears conditionals");
        return MapAssetHttpResponse{200, first, validators};
    });
    Require(mismatch.CheckOnce(manifest).failed == 1 && store.Read(asset).bytes == first, "hash mismatch retains previous valid file");
    WriteText(manifest, "relativePath\turl\n" + row + "\n");
    for (const auto status : {0U, 301U, 302U, 404U, 500U}) {
        MapAssetUpdater failure(store, [=](const auto&, auto) { return MapAssetHttpResponse{status, second, {}}; });
        Require(failure.CheckOnce(manifest).failed == 1 && store.Read(asset).bytes == first, "HTTP failure/redirect retains previous file");
    }
    MapAssetUpdater broken(store, [&](const auto&, auto) { return MapAssetHttpResponse{200, bad, {}}; });
    Require(broken.CheckOnce(manifest).failed == 1 && store.Read(asset).bytes == first, "invalid remote PNG retains previous file");
    MapAssetUpdater throwing(store, [&](const auto&, auto) -> MapAssetHttpResponse { throw std::runtime_error("offline failure"); });
    Require(throwing.CheckOnce(manifest).failed == 1 && store.Read(asset).bytes == first, "transport exception contained");
    const auto localPath = cache / asset.relativePath;
    HANDLE locked = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Require(locked != INVALID_HANDLE_VALUE, "lock destination");
    Require(store.Save(asset, second, validators) == MapAssetWrite::Failed, "replacement failure reported");
    CloseHandle(locked);
    Require(store.Read(asset).bytes == first, "failed atomic rename retains prior bytes");
    std::stop_source cancel;
    MapAssetUpdater cancelled(store, [&](const auto&, auto) { cancel.request_stop(); return MapAssetHttpResponse{200, second, {}}; });
    Require(cancelled.CheckOnce(manifest, cancel.get_token()).cancelled && store.Read(asset).bytes == first, "cancellation after response blocks commit");
    Require(store.Save(asset, second, validators, cancel.get_token()) == MapAssetWrite::Cancelled, "cancelled store write blocked");
    std::latch entered(1);
    MapAssetUpdater background(store, [&](const auto&, std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any changed;
        std::unique_lock lock(mutex);
        entered.count_down();
        changed.wait(lock, stop, [] { return false; });
        return MapAssetHttpResponse{200, second, {}};
    });
    Require(background.StartOnce(manifest) && !background.StartOnce(manifest), "background startup guarded");
    entered.wait();
    background.Stop();
    Require(background.Report().finished && background.Report().cancelled && store.Read(asset).bytes == first, "Stop cancels and joins safely");
    background.Stop();
    Require(!background.StartOnce(manifest), "Stop does not allow another startup check");
    Write(localPath, second);
    Require(store.Read(asset).validators.etag.empty(), "stale sidecar digest prevents conditionals");
    Write(localPath, bad);
    Require(store.Read(asset).bytes.empty() && store.Resolve(asset.relativePath) == std::filesystem::absolute(fallback / asset.relativePath), "corrupt cache uses valid bundled fallback");
    MapAssetUpdater unexpected304(store, [](const auto& request, auto) {
        Require(request.validators.etag.empty(), "corrupt cache sends no conditional headers");
        return MapAssetHttpResponse{304, {}, {}};
    });
    Require(unexpected304.CheckOnce(manifest).failed == 1, "unsolicited 304 cannot bless corrupt/missing cache");
    Require(store.Save(asset, first, {"injected\r\nHeader: bad", ""}) == MapAssetWrite::Unchanged
        && store.Read(asset).validators.etag.empty(), "unsafe validator is never persisted/sent");
    Require(store.Save(asset, second, validators) == MapAssetWrite::Updated, "valid changed PNG replaces cache");
    const auto sidecar = std::filesystem::path(localPath.wstring() + L".http");
    locked = CreateFileW(sidecar.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Require(locked != INVALID_HANDLE_VALUE, "lock metadata destination");
    Require(store.Save(asset, first, validators) == MapAssetWrite::Updated, "sidecar failure leaves committed PNG usable");
    CloseHandle(locked);
    Require(store.Read(asset).bytes == first && store.Read(asset).validators.etag.empty(), "partial metadata commit cannot associate stale validators");
    auto changedUrl = asset; changedUrl.url = "https://assets.tarkov.dev/maps/interchange/main/4/0/1.png";
    Require(store.Read(changedUrl).validators.etag.empty(), "validators bound to exact source URL");
    WriteText(manifest, "relativePath\turl\n" + row + "\n../escape.png\t" + asset.url + "\n");
    MapAssetUpdater invalidManifest(store, [](const auto&, auto) { Require(false, "invalid manifest must not request any asset"); return MapAssetHttpResponse{}; });
    Require(!invalidManifest.CheckOnce(manifest).error.empty(), "whole manifest validated before network");
    MapAssetUpdater absent(store, [](const auto&, auto) { Require(false, "missing manifest must remain offline"); return MapAssetHttpResponse{}; });
    Require(!absent.CheckOnce(root / "missing.tsv").error.empty(), "missing manifest is an offline-safe error");
    const auto parallelManifest = root / "parallel.tsv";
    std::string parallelText = "relativePath\turl\n";
    std::vector<std::string> parallelPaths;
    for (unsigned i = 0; i < 8; ++i) {
        parallelPaths.push_back("parallel/" + std::to_string(i) + ".png");
        parallelText += parallelPaths.back() + "\t" + asset.url + "\n";
    }
    WriteText(parallelManifest, parallelText);
    std::latch firstWave(MapAssetUpdater::MaxWorkers), releaseWave(1);
    std::atomic<unsigned> active{}, peak{}, parallelCalls{};
    MapAssetUpdater parallel(store, [&](const auto&, auto) {
        const auto concurrent = active.fetch_add(1) + 1;
        auto observed = peak.load();
        while (observed < concurrent && !peak.compare_exchange_weak(observed, concurrent)) {}
        if (parallelCalls.fetch_add(1) < MapAssetUpdater::MaxWorkers) {
            firstWave.count_down();
            releaseWave.wait();
        }
        active.fetch_sub(1);
        return MapAssetHttpResponse{200, first, validators};
    });
    MapAssetUpdateReport parallelReport;
    std::jthread checking([&] { parallelReport = parallel.CheckOnce(parallelManifest); });
    firstWave.wait();
    Require(peak == MapAssetUpdater::MaxWorkers, "four concurrent HTTP workers, with no extra workers");
    releaseWave.count_down(); checking.join();
    Require(parallelCalls == 8 && parallelReport.updated == 8 && parallelReport.updatedPaths == parallelPaths,
        "parallel results aggregate safely in deterministic manifest order");
    std::latch cancelledWave(MapAssetUpdater::MaxWorkers);
    std::atomic<unsigned> cancelledCalls{};
    MapAssetUpdater cancelParallel(store, [&](const auto&, std::stop_token stop) {
        cancelledCalls.fetch_add(1);
        std::mutex waitingMutex; std::condition_variable_any waiting;
        std::unique_lock lock(waitingMutex);
        cancelledWave.count_down();
        waiting.wait(lock, stop, [] { return false; });
        return MapAssetHttpResponse{200, second, {}};
    });
    Require(cancelParallel.StartOnce(parallelManifest), "parallel cancellation starts once");
    cancelledWave.wait(); cancelParallel.Stop();
    Require(cancelledCalls == MapAssetUpdater::MaxWorkers && cancelParallel.Report().cancelled
        && cancelParallel.Report().updatedPaths.empty(), "Stop cancels four active requests and does not claim queued changes");
    for (const auto& relative : parallelPaths) {
        const MapAssetSpec cached{relative, asset.url, ""};
        Require(store.Read(cached).bytes == first, "parallel cancellation retains every prior valid PNG");
    }
    const auto outside = root / "outside";
    Write(outside / "escape.png", first);
    const auto linked = cache / "linked";
    Junction(linked, outside);
    MapAssetSpec escaped{"linked/escape.png", asset.url, ""};
    Require(!store.Resolve(escaped.relativePath) && store.Read(escaped).bytes.empty()
        && store.Save(escaped, second, {}) == MapAssetWrite::Failed, "reparse ancestor rejected for reads and writes");
    MapAssetStore rooted(linked);
    escaped.relativePath = "escape.png";
    Require(!rooted.Resolve(escaped.relativePath) && rooted.Read(escaped).bytes.empty()
        && rooted.Save(escaped, second, {}) == MapAssetWrite::Failed, "reparse cache root rejected");
    Require(RemoveDirectoryW(linked.c_str()) != FALSE, "remove only the test junction, preserving its target");
    for (const auto& file : std::filesystem::recursive_directory_iterator(cache))
        Require(file.path().extension() != ".tmp", "no staging files left after failure/cancellation");
    std::filesystem::remove_all(root);
    CoUninitialize();
    std::cout << "Map asset offline tests passed\n";
}
