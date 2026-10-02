#include "data/MapAssetComposer.h"
#include "data/MapAssetUpdater.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace noven::data;
using Microsoft::WRL::ComPtr;
namespace {
constexpr const char* Header = "outputPath\tsourcePath\twidth\theight\ttileSize\ttileX\ttileY\tleft\ttop\tright\tbottom\tblend\n";
void Require(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
std::vector<BYTE> Encode(unsigned size, std::array<BYTE, 4> color) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    auto format = GUID_WICPixelFormat32bppBGRA;
    std::vector<BYTE> pixels(size * size * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = color[i % 4];
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        && SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))
        && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr))
        && SUCCEEDED(frame->SetSize(size, size)) && SUCCEEDED(frame->SetPixelFormat(&format))
        && format == GUID_WICPixelFormat32bppBGRA && SUCCEEDED(frame->WritePixels(size, size * 4, static_cast<UINT>(pixels.size()), pixels.data()))
        && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "source PNG fixture encoding");
    STATSTG stat{}; LARGE_INTEGER zero{};
    Require(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)), "fixture stream");
    std::vector<BYTE> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart)); ULONG read{};
    Require(SUCCEEDED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) && read == bytes.size(), "fixture encoded bytes");
    return bytes;
}
void Write(const std::filesystem::path& path, std::span<const BYTE> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(file.good(), "fixture write");
}
void Text(const std::filesystem::path& path, const std::string& text) {
    Write(path, std::span(reinterpret_cast<const BYTE*>(text.data()), text.size()));
}
std::vector<BYTE> Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg(); Require(file && size > 0, "artifact exists");
    std::vector<BYTE> result(static_cast<std::size_t>(size)); file.seekg(0);
    Require(bool(file.read(reinterpret_cast<char*>(result.data()), size)), "artifact complete");
    return result;
}
struct Decoded { unsigned width{}, height{}; std::vector<BYTE> pixels; };
Decoded Decode(std::vector<BYTE> bytes) {
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
    Decoded result;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        && SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size())))
        && SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))
        && SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&result.width, &result.height))
        && SUCCEEDED(factory->CreateFormatConverter(&converter))
        && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)),
        "derived PNG decoding");
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    Require(SUCCEEDED(converter->CopyPixels(nullptr, result.width * 4, static_cast<UINT>(result.pixels.size()), result.pixels.data())), "derived pixels");
    return result;
}
std::array<BYTE, 4> Pixel(const Decoded& image, unsigned x, unsigned y) {
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2], image.pixels[offset + 3]};
}
std::string Row(std::string output, std::string source, int tileX, std::string blend = "replace", unsigned width = 513, unsigned height = 256) {
    return output + "\t" + source + "\t" + std::to_string(width) + "\t" + std::to_string(height)
        + "\t2\t" + std::to_string(tileX) + "\t0\t-2\t0\t2\t2\t" + blend + "\n";
}
std::uint32_t Le(const BYTE* bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
void CheckPack(const std::filesystem::path& path, unsigned width, unsigned height) {
    const auto bytes = Read(path);
    Require(bytes.size() >= 24 && std::string_view(reinterpret_cast<const char*>(bytes.data()), 8) == "NVTILES1", "compatible tile pack magic");
    Require(Le(bytes.data() + 8) == width && Le(bytes.data() + 12) == height && Le(bytes.data() + 16) == 512, "pack geometry");
    const auto count = Le(bytes.data() + 20);
    Require(count == ((width + 511) / 512) * ((height + 511) / 512), "pack tile count");
    for (unsigned index = 0; index < count; ++index) {
        const auto offset = Le(bytes.data() + 24 + index * 8), length = Le(bytes.data() + 28 + index * 8);
        Require(offset >= 24 + count * 8 && std::uint64_t(offset) + length <= bytes.size(), "pack offsets bounded");
        const auto png = std::vector<BYTE>(bytes.begin() + offset, bytes.begin() + offset + length);
        Require(MapAssetStore::ValidatePng(png), "each output tile is a valid independent PNG");
        const auto decoded = Decode(png);
        Require(decoded.width == (std::min)(512U, width - (index % ((width + 511) / 512)) * 512)
            && decoded.height == (std::min)(512U, height - (index / ((width + 511) / 512)) * 512), "edge tile exact dimensions");
    }
}
} // namespace

int main(int argc, char** argv) {
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM initialization");
    if (argc == 5) {
        std::ifstream input(std::filesystem::path(argv[2]), std::ios::binary);
        std::vector<MapAssetCompositionTile> rows; std::string parseError;
        const bool validRecipe = ParseMapAssetComposition(input, rows, parseError);
        Require(validRecipe, parseError.c_str());
        std::ostringstream recipe; recipe << Header << std::setprecision(17);
        std::vector<std::string> changes;
        unsigned width{}, height{};
        for (const auto& row : rows) if (row.outputPath == argv[3]) {
            recipe << row.outputPath << '\t' << row.sourcePath << '\t' << row.width << '\t' << row.height << '\t'
                << row.tileSize << '\t' << row.tileX << '\t' << row.tileY << '\t' << row.left << '\t' << row.top << '\t'
                << row.right << '\t' << row.bottom << '\t' << (row.over ? "over" : "replace") << '\n';
            changes.push_back(row.sourcePath); width = row.width; height = row.height;
        }
        Require(!changes.empty(), "selected real output has recipes");
        const auto generationRoot = std::filesystem::absolute(std::filesystem::path(argv[4]));
        const auto manifest = generationRoot / "offline-recipe.tsv";
        Text(manifest, recipe.str());
        MapAssetComposer composer(MapAssetStore({}, std::filesystem::path(argv[1])), generationRoot);
        const auto result = composer.Rebuild(manifest, changes);
        if (!result.error.empty()) std::cerr << result.error << '\n';
        Require(result.error.empty() && result.rebuiltPaths == std::vector<std::string>{argv[3]}, "real source PNGs rebuild selected native floor");
        auto previewPath = result.generationRoot / argv[3];
        const auto preview = Decode(Read(previewPath));
        Require((std::max)(preview.width, preview.height) <= MapAssetComposer::PreviewEdge, "real preview bounded");
        previewPath.replace_extension(L".tiles"); CheckPack(previewPath, width, height);
        std::cout << "Real offline composition passed: " << changes.size() << " rows, " << width << 'x' << height
            << ", generation=" << result.generationRoot.string() << '\n';
        CoUninitialize();
        return 0;
    }
    std::vector<MapAssetCompositionTile> parsed; std::string error;
    auto parse = [&](std::string text) { std::istringstream input(text); return ParseMapAssetComposition(input, parsed, error); };
    const std::string a = "maps/sources/a.png", b = "maps/sources/b.png", upper = "maps/test/composition_sources/upper.png";
    const std::string output = "maps/test/floor.satellite.png", other = "maps/test/other.satellite.png";
    const auto baseRows = Row(output, a, -1) + Row(output, b, 0);
    Require(parse(std::string(Header) + baseRows + Row(output, upper, 0, "over")) && parsed.size() == 3, "agreed 12-column TSV accepts negative global tiles");
    Require(!parse(std::string(Header) + Row("../escape.png", a, 0)) && parsed.empty(), "unsafe output rejected atomically");
    Require(!parse(std::string(Header) + Row(output, "../escape.png", 0)), "unsafe source rejected");
    Require(!parse(std::string(Header) + Row(output, a, 0) + Row(output, b, 0)), "duplicate grid cell rejected");
    Require(!parse(std::string(Header) + Row(output, a, 0, "replace", 8193)), "output edge bounded");
    Require(!parse(std::string(Header) + Row(output, a, 0, "over")), "overlay cannot precede base");
    Require(!parse(std::string(Header) + Row(output, a, -1) + Row(output, upper, 0, "over") + Row(output, b, 0)), "base cannot follow overlay");
    auto nonfinite = Row(output, a, 0); nonfinite.replace(nonfinite.find("\t-2\t"), 4, "\tnan\t");
    Require(!parse(std::string(Header) + nonfinite), "nonfinite crop rejected");
    Require(!parse(std::string(Header) + Row(output, a, 0) + Row(output, b, 1, "replace", 514)), "mixed output geometry rejected");
    std::string many = Header;
    for (unsigned i = 0; i <= MapAssetComposer::MaxSources; ++i) many += Row(output, a, static_cast<int>(i));
    Require(!parse(many), "bounded source row count");
    many = Header;
    for (std::size_t i = 0; i <= MapAssetComposer::MaxRows; ++i)
        many += Row("maps/test/output-" + std::to_string(i / MapAssetComposer::MaxSources) + ".png", a,
            static_cast<int>(i % MapAssetComposer::MaxSources));
    Require(!parse(many), "100000 global recipe row bound enforced");

    const auto root = std::filesystem::temp_directory_path() / (L"NovenComposer-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    const auto cache = root / "cache", bundle = root / "bundle", generations = root / "generations", manifest = root / "composition.tsv";
    const auto red = Encode(2, {0, 0, 255, 255}), blue = Encode(2, {255, 0, 0, 255}), green = Encode(2, {0, 255, 0, 128});
    Write(bundle / a, red); Write(bundle / b, blue); Write(bundle / upper, green);
    MapAssetStore store(cache, bundle); MapAssetComposer composer(store, generations);
    Require(!composer.CurrentGeneration(), "no initial cached generation");
    Require(composer.Rebuild(root / "missing.tsv", {}).error.empty(), "no changed sources means no recipe IO/rebuild");
    Text(manifest, std::string(Header) + baseRows + Row(output, upper, 0, "over") + Row(other, a, -1) + Row(other, b, 0));
    const std::array<std::string, 1> changedA{a};
    auto built = composer.Rebuild(manifest, changedA);
    if (!built.error.empty()) std::cerr << "Initial composition: " << built.error << '\n';
    Require(built.error.empty() && built.rebuiltPaths.size() == 2 && composer.CurrentGeneration() == built.generationRoot, "complete generation atomically published");
    auto preview = Decode(Read(built.generationRoot / output));
    Require(preview.width == 513 && preview.height == 256, "preview maps the same output projection");
    Require(Pixel(preview, 100, 128) == std::array<BYTE, 4>{0,0,255,255}, "negative tile coordinates preserve base pixels");
    const auto blended = Pixel(preview, 400, 128);
    Require(blended[0] >= 126 && blended[0] <= 128 && blended[1] >= 127 && blended[1] <= 129 && blended[2] == 0 && blended[3] == 255,
        "static SVG-derived overlay is composited using premultiplied alpha");
    auto pack = built.generationRoot / output; pack.replace_extension(L".tiles"); CheckPack(pack, 513, 256);
    const auto firstGeneration = built.generationRoot;
    const auto firstPointer = Read(generations / "current.txt"), firstImage = Read(firstGeneration / output), otherImage = Read(firstGeneration / other);
    Require(composer.Rebuild(manifest, {}).generationRoot == firstGeneration && Read(generations / "current.txt") == firstPointer,
        "unchanged sources do not publish another generation");
    const std::array<std::string, 1> unrelated{"maps/sources/unrelated.png"};
    Require(composer.Rebuild(manifest, unrelated).rebuiltPaths.empty() && Read(generations / "current.txt") == firstPointer, "unreferenced source changes ignored");
    std::stop_source cancellation; cancellation.request_stop();
    Require(composer.Rebuild(manifest, changedA, cancellation.get_token()).cancelled && composer.CurrentGeneration() == firstGeneration, "cancelled rebuild retains prior generation");
    Text(manifest, std::string(Header) + Row(output, "maps/sources/missing.png", -1));
    const std::array<std::string, 1> missing{"maps/sources/missing.png"};
    Require(!composer.Rebuild(manifest, missing).error.empty() && composer.CurrentGeneration() == firstGeneration && Read(firstGeneration / output) == firstImage,
        "missing source preserves last valid generation");
    Text(manifest, std::string(Header) + baseRows + Row(output, upper, 0, "over") + Row(other, a, -1) + Row(other, b, 0));
    Write(cache / a, Encode(513, {255,255,255,255}));
    std::filesystem::remove(bundle / a);
    Require(!composer.Rebuild(manifest, changedA).error.empty() && composer.CurrentGeneration() == firstGeneration, "source intrinsic PNG dimension bounded to 512px");
    Write(bundle / a, red);
    std::filesystem::remove(cache / a);
    Write(cache / a, Encode(3, {0,0,255,255}));
    const auto logical = composer.Rebuild(manifest, changedA);
    Require(logical.error.empty() && Pixel(Decode(Read(logical.generationRoot / other)), 100, 128) == std::array<BYTE, 4>{0,0,255,255},
        "logical grid size differs from intrinsic source PNG size, preserving projection");
    std::filesystem::remove(cache / a);
    // 完整测试 CheckOnce -> 已变更源 -> 合成 -> 显示产物；所有网络响应均为离线夹具。
    // Exercise CheckOnce -> changed source -> composition -> display product, using offline HTTP fixtures.
    const auto updateManifest = root / "updates.tsv";
    Text(updateManifest, "relativePath\turl\n" + b + "\thttps://assets.tarkov.dev/maps/interchange/main/4/0/0.png\n");
    const auto white = Encode(2, {255,255,255,255});
    MapAssetUpdater updater(store, [&](const auto&, auto) { return MapAssetHttpResponse{200, white, {}}; });
    const auto updated = updater.CheckOnce(updateManifest);
    Require(updated.updatedPaths == std::vector<std::string>{b}, "updater reports committed changed PNG");
    built = composer.Rebuild(manifest, updated.updatedPaths);
    Require(built.error.empty() && built.generationRoot != firstGeneration && composer.CurrentGeneration() == built.generationRoot
        && Read(firstGeneration / output) == firstImage, "source change rebuilds derived files without mutating old generation");
    preview = Decode(Read(built.generationRoot / other));
    Require(Pixel(preview, 400, 128) == std::array<BYTE, 4>{255,255,255,255}, "derived detail/preview reflects downloaded source change");
    // 只改变 overlay 所依赖输出，上一代其他输出逐字节保留。
    // Changing the overlay rebuilds its dependent output and carries other products forward byte-for-byte.
    const auto beforeSelective = built.generationRoot;
    const auto unmodified = Read(beforeSelective / other);
    Write(cache / upper, Encode(2, {0,0,0,128}));
    const std::array<std::string, 1> changedOverlay{upper};
    built = composer.Rebuild(manifest, changedOverlay);
    Require(built.error.empty() && built.rebuiltPaths == std::vector<std::string>{output} && Read(built.generationRoot / other) == unmodified,
        "only outputs depending on changed source rebuilt");
    const auto beforeFailure = built.generationRoot;
    HANDLE locked = CreateFileW((generations / "current.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Require(locked != INVALID_HANDLE_VALUE, "lock generation pointer");
    Require(!composer.Rebuild(manifest, changedOverlay).error.empty(), "pointer publication failure reported");
    CloseHandle(locked);
    Require(composer.CurrentGeneration() == beforeFailure, "publication failure retains old pointer/generation");
    Require(std::filesystem::exists(generations / "pending.txt"), "failed composition retains dirty source ledger");
    built = composer.Rebuild(manifest, {});
    Require(built.error.empty() && built.rebuiltPaths == std::vector<std::string>{output}
        && !std::filesystem::exists(generations / "pending.txt"),
        "next startup retries pending sources even when HTTP reports no new changes");
    Text(manifest, std::string(Header) + Row(output, a, -1, "replace", 2049, 1025) + Row(output, b, 0, "replace", 2049, 1025));
    built = composer.Rebuild(manifest, changedA);
    Require(built.error.empty(), "larger source-to-derived projection rebuild");
    preview = Decode(Read(built.generationRoot / output));
    Require((std::max)(preview.width, preview.height) <= MapAssetComposer::PreviewEdge, "bounded preview avoids full-atlas decode");
    pack = built.generationRoot / output; pack.replace_extension(L".tiles"); CheckPack(pack, 2049, 1025);
    // 同一源快照和配方产生确定性 PNG/tiles，不在产物中写时间戳。
    // Identical source snapshots and recipes produce deterministic PNG/tiles, without embedded timestamps.
    const auto deterministicPng = Read(built.generationRoot / output), deterministicPack = Read(pack);
    built = composer.Rebuild(manifest, changedA);
    pack = built.generationRoot / output; pack.replace_extension(L".tiles");
    Require(built.error.empty() && Read(built.generationRoot / output) == deterministicPng && Read(pack) == deterministicPack, "native composition deterministic");
    for (const auto& entry : std::filesystem::directory_iterator(generations))
        Require(!entry.path().filename().wstring().starts_with(L"staging-") && entry.path().extension() != L".tmp", "no staging/pointer temp after failures");
    Text(generations / "current.txt", "../escape\n");
    Require(!composer.CurrentGeneration(), "pointer traversal rejected");
    Require(Read(firstGeneration / other) == otherImage, "original generation still valid");
    std::filesystem::remove_all(root);
    CoUninitialize();
    std::cout << "Map asset composer offline tests passed\n";
}
