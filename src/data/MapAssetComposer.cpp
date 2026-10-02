#include "data/MapAssetComposer.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace noven::data {
namespace {
using Microsoft::WRL::ComPtr;
constexpr std::size_t MaxSourceBytes = 2 * 1024 * 1024;
constexpr std::uint64_t MaxPackBytes = 512ULL * 1024 * 1024;
constexpr std::string_view Header = "outputPath\tsourcePath\twidth\theight\ttileSize\ttileX\ttileY\tleft\ttop\tright\tbottom\tblend";
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Cancelled {};
void CheckStop(std::stop_token stop) { if (stop.stop_requested()) throw Cancelled{}; }
std::string Fold(std::string path) {
    for (auto& c : path) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return path;
}
bool SameGeometry(const MapAssetCompositionTile& a, const MapAssetCompositionTile& b) {
    return a.width == b.width && a.height == b.height && a.tileSize == b.tileSize
        && a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
template<class T> bool Number(std::string_view text, T& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
std::optional<std::filesystem::path> SafePath(const std::filesystem::path& root, const std::filesystem::path& relative = {}) {
    if (root.empty()) return {};
    std::error_code error;
    auto path = std::filesystem::absolute(root, error);
    if (!relative.empty()) path /= relative;
    path = path.lexically_normal();
    if (error) return {};
    auto current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return {};
        } else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return {};
    }
    return path;
}
std::vector<BYTE> Read(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    Check(file && length > 0 && length <= static_cast<std::streamoff>(limit), "Unreadable/oversized composition input");
    std::vector<BYTE> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    Check(bool(file.read(reinterpret_cast<char*>(bytes.data()), length)), "Incomplete composition input");
    return bytes;
}
void Write(const std::filesystem::path& path, std::span<const BYTE> bytes) {
    Check(bool(SafePath(path)), "Unsafe generation output");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    Check(bool(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "Generation write failed");
    file.close();
    Check(!file.fail(), "Generation close failed");
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(handle != INVALID_HANDLE_VALUE, "Generation flush open failed");
    const bool flushed = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    Check(flushed, "Generation flush failed");
}
void WriteText(const std::filesystem::path& path, const std::string& text) {
    Write(path, std::span(reinterpret_cast<const BYTE*>(text.data()), text.size()));
}
std::uint32_t Be(const BYTE* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
struct Apartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    Apartment() { Check(SUCCEEDED(result) || result == RPC_E_CHANGED_MODE, "WIC apartment initialization failed"); }
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Source {
    std::filesystem::path path;
    std::string digest;
    std::uint32_t size{}, logicalSize{};
};
struct Image { std::size_t source{}, stamp{}; std::vector<BYTE> pixels; };
class Pixels final {
public:
    Pixels(IWICImagingFactory& factory, const std::vector<Source>& sources) : factory_(factory), sources_(sources) {}
    std::array<double, 4> At(std::size_t source, int x, int y) {
        auto found = std::find_if(cache_.begin(), cache_.end(), [&](const Image& image) { return image.source == source; });
        if (found == cache_.end()) {
            if (cache_.size() < 64) { cache_.push_back({}); found = cache_.end() - 1; }
            else found = std::min_element(cache_.begin(), cache_.end(), [](const auto& a, const auto& b) { return a.stamp < b.stamp; });
            const auto& record = sources_[source];
            auto bytes = Read(record.path, MaxSourceBytes);
            Check(MapAssetStore::Sha256(bytes) == record.digest, "Source changed during composition");
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> converter;
            Check(SUCCEEDED(factory_.CreateStream(&stream))
                && SUCCEEDED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size())))
                && SUCCEEDED(factory_.CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))
                && SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory_.CreateFormatConverter(&converter))
                && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)), "Source WIC decode failed");
            found->source = source;
            found->pixels.resize(static_cast<std::size_t>(record.size) * record.size * 4);
            Check(SUCCEEDED(converter->CopyPixels(nullptr, record.size * 4,
                static_cast<UINT>(found->pixels.size()), found->pixels.data())), "Source pixels failed");
        }
        found->stamp = ++stamp_;
        const auto& record = sources_[source];
        auto pixel = [&](int px, int py) {
            px = std::clamp(px, 0, int(record.size) - 1);
            py = std::clamp(py, 0, int(record.size) - 1);
            const auto offset = (static_cast<std::size_t>(py) * record.size + px) * 4;
            return std::array<double, 4>{double(found->pixels[offset]), double(found->pixels[offset + 1]),
                double(found->pixels[offset + 2]), double(found->pixels[offset + 3])};
        };
        if (record.size == record.logicalSize) return pixel(x, y);
        // DEV tileSize 是逻辑网格；Lab 的 175 网格实际对应 256px PNG。
        // DEV tileSize is the logical grid; Lab's 175 grid uses intrinsic 256px PNGs.
        const double sx = (x + 0.5) * record.size / record.logicalSize - 0.5;
        const double sy = (y + 0.5) * record.size / record.logicalSize - 0.5;
        const int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
        std::array<double, 4> result{};
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
            const double weight = (dx ? sx - ix : 1 - sx + ix) * (dy ? sy - iy : 1 - sy + iy);
            const auto value = pixel(ix + dx, iy + dy);
            for (std::size_t c = 0; c < 4; ++c) result[c] += value[c] * weight;
        }
        return result;
    }
private:
    IWICImagingFactory& factory_;
    const std::vector<Source>& sources_;
    std::vector<Image> cache_;
    std::size_t stamp_{};
};
using Grid = std::map<std::pair<int, int>, std::size_t>;
std::array<double, 4> Sample(Pixels& pixels, const Grid& grid, unsigned size, double x, double y) {
    // 跨源瓦片取四个邻点，避免分块缩放在接缝处重复边缘像素。
    // Sample four neighbours across source tiles, avoiding clamped scaling seams.
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    std::array<double, 4> result{};
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const int px = ix + dx, py = iy + dy;
        const int tx = static_cast<int>(std::floor(double(px) / size)), ty = static_cast<int>(std::floor(double(py) / size));
        const auto found = grid.find({tx, ty});
        if (found == grid.end()) continue;
        const double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
        if (weight == 0) continue;
        const auto value = pixels.At(found->second, px - tx * int(size), py - ty * int(size));
        for (std::size_t c = 0; c < 4; ++c) result[c] += value[c] * weight;
    }
    return result;
}
std::vector<BYTE> Render(Pixels& pixels, const std::array<Grid, 2>& layers, const MapAssetCompositionTile& geometry,
    unsigned fullWidth, unsigned fullHeight, unsigned x, unsigned y, unsigned width, unsigned height, std::stop_token stop) {
    std::vector<BYTE> result(static_cast<std::size_t>(width) * height * 4);
    for (unsigned row = 0; row < height; ++row) {
        CheckStop(stop);
        const double sy = geometry.top + (y + row + 0.5) * (geometry.bottom - geometry.top) / fullHeight - 0.5;
        for (unsigned column = 0; column < width; ++column) {
            const double sx = geometry.left + (x + column + 0.5) * (geometry.right - geometry.left) / fullWidth - 0.5;
            auto value = Sample(pixels, layers[0], geometry.tileSize, sx, sy);
            if (!layers[1].empty()) {
                const auto over = Sample(pixels, layers[1], geometry.tileSize, sx, sy);
                for (std::size_t c = 0; c < 4; ++c) value[c] = over[c] + value[c] * (1 - over[3] / 255);
            }
            const auto offset = (static_cast<std::size_t>(row) * width + column) * 4;
            for (std::size_t c = 0; c < 4; ++c)
                result[offset + c] = static_cast<BYTE>(std::clamp(std::lround(value[c]), 0L, 255L));
        }
    }
    return result;
}
std::vector<BYTE> Encode(IWICImagingFactory& factory, std::vector<BYTE> pixels, unsigned width, unsigned height) {
    for (std::size_t i = 0; i < pixels.size(); i += 4) for (std::size_t c = 0; c < 3; ++c)
        pixels[i + c] = pixels[i + 3] ? static_cast<BYTE>((std::min)(255U,
            (unsigned(pixels[i + c]) * 255 + pixels[i + 3] / 2) / pixels[i + 3])) : 0;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    auto format = GUID_WICPixelFormat32bppBGRA;
    Check(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))
        && SUCCEEDED(factory.CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr))
        && SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format))
        && format == GUID_WICPixelFormat32bppBGRA
        && SUCCEEDED(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))
        && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "Derived PNG encoding failed");
    STATSTG stat{};
    LARGE_INTEGER zero{};
    Check(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart <= MapAssetStore::MaxBytes
        && SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)), "Encoded PNG size exceeded");
    std::vector<BYTE> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
    ULONG read{};
    Check(SUCCEEDED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) && read == bytes.size(), "Encoded PNG read failed");
    return bytes;
}
void Build(IWICImagingFactory& factory, const MapAssetStore& store, const std::vector<MapAssetCompositionTile>& rows,
    const std::filesystem::path& root, std::stop_token stop) {
    const auto& geometry = rows.front();
    std::vector<Source> sources;
    std::array<Grid, 2> layers;
    for (const auto& row : rows) {
        CheckStop(stop);
        const auto path = store.Resolve(row.sourcePath, 512);
        Check(bool(path), "Missing/invalid composition source PNG");
        const auto bytes = Read(*path, MaxSourceBytes);
        Check(bytes.size() >= 24 && Be(bytes.data() + 16) > 0 && Be(bytes.data() + 16) <= 512
            && Be(bytes.data() + 20) == Be(bytes.data() + 16), "Source PNG must be square and at most 512px");
        layers[row.over ? 1 : 0].emplace(std::pair(row.tileX, row.tileY), sources.size());
        sources.push_back({*path, MapAssetStore::Sha256(bytes), Be(bytes.data() + 16), row.tileSize});
    }
    Pixels pixels(factory, sources);
    auto preview = root / geometry.outputPath;
    auto packPath = preview; packPath.replace_extension(L".tiles");
    Check(bool(SafePath(root, geometry.outputPath)), "Unsafe derived path");
    std::filesystem::create_directories(preview.parent_path());
    std::ofstream pack(packPath, std::ios::binary);
    const auto count = ((geometry.width + 511) / 512) * ((geometry.height + 511) / 512);
    const std::array<std::uint32_t, 4> header{geometry.width, geometry.height, 512, count};
    std::vector<std::array<std::uint32_t, 2>> index(count);
    pack.write("NVTILES1", 8);
    pack.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    pack.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size() * 8));
    unsigned tile{};
    std::uint64_t offset = 24 + index.size() * 8;
    for (unsigned y = 0; y < geometry.height; y += 512) for (unsigned x = 0; x < geometry.width; x += 512) {
        CheckStop(stop);
        const unsigned width = (std::min)(512U, geometry.width - x), height = (std::min)(512U, geometry.height - y);
        auto encoded = Encode(factory, Render(pixels, layers, geometry, geometry.width, geometry.height, x, y, width, height, stop), width, height);
        Check(encoded.size() <= MaxSourceBytes && offset + encoded.size() <= MaxPackBytes, "Derived tile/pack size exceeded");
        index[tile++] = {static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(encoded.size())};
        pack.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
        offset += encoded.size();
    }
    pack.seekp(24);
    pack.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size() * 8));
    pack.close();
    Check(!pack.fail(), "Derived pack write failed");
    const HANDLE handle = CreateFileW(packPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(handle != INVALID_HANDLE_VALUE, "Derived pack flush open failed");
    const bool flushed = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    Check(flushed, "Derived pack flush failed");
    const double ratio = (std::min)(1.0, double(MapAssetComposer::PreviewEdge) / (std::max)(geometry.width, geometry.height));
    const auto pw = (std::max)(1U, static_cast<unsigned>(geometry.width * ratio));
    const auto ph = (std::max)(1U, static_cast<unsigned>(geometry.height * ratio));
    auto encoded = Encode(factory, Render(pixels, layers, geometry, pw, ph, 0, 0, pw, ph, stop), pw, ph);
    Check(MapAssetStore::ValidatePng(encoded), "Derived preview validation failed");
    Write(preview, encoded);
    // 提交前再核对源快照，避免在两块输出之间混入不同源版本。
    // Recheck source snapshots before commit, preventing mixed source versions between output blocks.
    for (const auto& source : sources) {
        CheckStop(stop);
        Check(bool(SafePath(source.path)) && MapAssetStore::Sha256(Read(source.path, MaxSourceBytes)) == source.digest,
            "Source changed before generation publication");
    }
}
} // namespace

bool ParseMapAssetComposition(std::istream& input, std::vector<MapAssetCompositionTile>& rows, std::string& error) {
    rows.clear(); error.clear();
    try {
        std::string line;
        std::size_t total{};
        Check(bool(std::getline(input, line)), "Composition header missing");
        if (line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Check(line == Header, "Unexpected composition TSV header");
        std::map<std::string, std::vector<MapAssetCompositionTile>> grouped;
        std::set<std::tuple<std::string, bool, int, int>> occupied;
        std::vector<MapAssetCompositionTile> parsed;
        while (std::getline(input, line)) {
            total += line.size() + 1;
            Check(line.size() <= 2048 && total <= MapAssetComposer::MaxManifestBytes, "Composition manifest size exceeded");
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            std::vector<std::string_view> fields;
            std::size_t begin{};
            for (;;) {
                const auto end = line.find('\t', begin);
                fields.push_back(std::string_view(line).substr(begin, end == line.npos ? end : end - begin));
                if (end == line.npos) break;
                begin = end + 1;
            }
            Check(fields.size() == 12, "Composition column count");
            MapAssetCompositionTile row;
            row.outputPath = fields[0]; row.sourcePath = fields[1];
            Check(ValidMapAssetPath(row.outputPath) && ValidMapAssetPath(row.sourcePath), "Unsafe composition path");
            Check(Number(fields[2], row.width) && Number(fields[3], row.height) && Number(fields[4], row.tileSize)
                && Number(fields[5], row.tileX) && Number(fields[6], row.tileY) && Number(fields[7], row.left)
                && Number(fields[8], row.top) && Number(fields[9], row.right) && Number(fields[10], row.bottom), "Invalid composition number");
            Check(row.width && row.height && row.width <= MapAssetComposer::MaxDimension && row.height <= MapAssetComposer::MaxDimension
                && std::uint64_t(row.width) * row.height <= MapAssetComposer::MaxPixels && row.tileSize && row.tileSize <= 512
                && std::abs(double(row.tileX)) <= 1000000 && std::abs(double(row.tileY)) <= 1000000,
                "Composition dimension/tile bound exceeded");
            Check(std::isfinite(row.left) && std::isfinite(row.top) && std::isfinite(row.right) && std::isfinite(row.bottom)
                && std::abs(row.left) <= 512000000 && std::abs(row.top) <= 512000000
                && std::abs(row.right) <= 512000000 && std::abs(row.bottom) <= 512000000
                && row.right > row.left && row.bottom > row.top, "Invalid composition crop");
            Check(fields[11] == "replace" || fields[11] == "over", "Invalid composition blend");
            row.over = fields[11] == "over";
            const auto key = Fold(row.outputPath);
            auto& group = grouped[key];
            Check(group.size() < MapAssetComposer::MaxSources, "More than 4096 sources for one output");
            if (!group.empty()) {
                Check(group.front().outputPath == row.outputPath && SameGeometry(group.front(), row), "Inconsistent output geometry/case");
                Check(!group.back().over || row.over, "Base rows must precede overlay rows");
            }
            Check(!group.empty() || !row.over, "Composition needs a base layer");
            Check(occupied.emplace(key, row.over, row.tileX, row.tileY).second, "Duplicate source tile in one layer");
            group.push_back(row); parsed.push_back(std::move(row));
            Check(grouped.size() <= 256 && parsed.size() <= MapAssetComposer::MaxRows, "Composition output/row bound exceeded");
        }
        Check(!input.bad(), "Composition read failed");
        rows = std::move(parsed);
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

MapAssetComposer::MapAssetComposer(MapAssetStore sourceStore, std::filesystem::path generationsRoot)
    : sources_(std::move(sourceStore)), generationsRoot_(std::move(generationsRoot)) {}

std::optional<std::filesystem::path> MapAssetComposer::CurrentGeneration() const {
    try {
        const auto pointer = SafePath(generationsRoot_, L"current.txt");
        if (!pointer) return {};
        const auto bytes = Read(*pointer, 128);
        std::string name(bytes.begin(), bytes.end());
        if (!name.empty() && name.back() == '\n') name.pop_back();
        if (!name.starts_with("generation-") || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != name.npos) return {};
        const auto generation = SafePath(generationsRoot_, name);
        if (!generation) return {};
        const auto marker = SafePath(*generation, L".complete");
        if (!marker) return {};
        const auto content = Read(*marker, 64);
        if (std::string(content.begin(), content.end()) != "NVMAPGEN1\n") return {};
        return generation;
    } catch (...) { return {}; }
}

MapAssetCompositionReport MapAssetComposer::Rebuild(const std::filesystem::path& manifest,
    std::span<const std::string> changedSources, std::stop_token stop) const {
    MapAssetCompositionReport report;
    const auto previous = CurrentGeneration();
    if (previous) report.generationRoot = *previous;
    std::filesystem::path staging;
    try {
        std::set<std::string> changed;
        for (const auto& path : changedSources) { Check(ValidMapAssetPath(path), "Unsafe changed source path"); changed.insert(Fold(path)); }
        const auto root = SafePath(generationsRoot_);
        Check(bool(root), "Unsafe generation root");
        const auto pending = SafePath(*root, L"pending.txt");
        Check(bool(pending), "Unsafe pending source ledger");
        if (std::filesystem::exists(*pending)) {
            const auto bytes = Read(*pending, 4 * 1024 * 1024);
            std::istringstream ledger(std::string(bytes.begin(), bytes.end()));
            std::string path;
            while (std::getline(ledger, path)) {
                Check(ValidMapAssetPath(path), "Invalid pending source path");
                changed.insert(Fold(path));
                Check(changed.size() <= MaxRows, "Pending source count exceeded");
            }
        }
        if (changed.empty()) return report;
        // 在响应取消前持久化待重拼源，下一次启动即使 HTTP 304 也不会漏更新。
        // Persist dirty sources before cancellation; a later HTTP 304 cannot lose a rebuild.
        std::filesystem::create_directories(*root);
        std::string ledger;
        for (const auto& path : changed) ledger += path + '\n';
        const auto temporaryPending = *root / L"pending.tmp";
        WriteText(temporaryPending, ledger);
        const bool saved = MoveFileExW(temporaryPending.c_str(), pending->c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!saved) DeleteFileW(temporaryPending.c_str());
        Check(saved, "Pending source ledger publication failed");
        CheckStop(stop);
        std::error_code error;
        Check(std::filesystem::file_size(manifest, error) <= MaxManifestBytes && !error, "Composition TSV unreadable/oversized");
        std::ifstream input(manifest, std::ios::binary);
        std::vector<MapAssetCompositionTile> rows;
        std::string parseError;
        const bool parsed = ParseMapAssetComposition(input, rows, parseError);
        Check(parsed, parseError.c_str());
        std::map<std::string, std::vector<MapAssetCompositionTile>> outputs;
        std::set<std::string> affected;
        for (const auto& row : rows) {
            outputs[row.outputPath].push_back(row);
            if (changed.contains(Fold(row.sourcePath))) affected.insert(row.outputPath);
        }
        if (affected.empty()) { DeleteFileW(pending->c_str()); return report; }
        std::filesystem::create_directories(*root);
        Check(bool(SafePath(*root)), "Unsafe created generation root");
        static std::atomic<unsigned long> sequence{};
        const auto name = "generation-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64())
            + "-" + std::to_string(sequence.fetch_add(1));
        const auto candidate = *root / ("staging-" + name);
        Check(std::filesystem::create_directory(candidate), "Generation staging already exists");
        staging = candidate;
        // 拷贝上一代未变化产物；永不修改上一代或打包回退文件。
        // Carry forward unchanged products, never mutating the previous generation or bundled fallback.
        if (previous) for (const auto& entry : std::filesystem::recursive_directory_iterator(*previous)) {
            CheckStop(stop);
            Check(bool(SafePath(entry.path())), "Unsafe previous generation entry");
            const auto relative = entry.path().lexically_relative(*previous);
            if (relative == L".complete") continue;
            const auto target = SafePath(staging, relative);
            Check(bool(target), "Unsafe copied generation path");
            if (entry.is_directory()) std::filesystem::create_directories(*target);
            else if (entry.is_regular_file()) {
                Check(entry.file_size() <= MaxPackBytes, "Previous generation file bound exceeded");
                std::filesystem::create_directories(target->parent_path());
                std::filesystem::copy_file(entry.path(), *target);
            } else Check(false, "Unsupported previous generation entry");
        }
        Apartment apartment;
        ComPtr<IWICImagingFactory> factory;
        Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "WIC factory failed");
        for (const auto& path : affected) { CheckStop(stop); Build(*factory.Get(), sources_, outputs.at(path), staging, stop); }
        CheckStop(stop);
        WriteText(staging / L".complete", "NVMAPGEN1\n");
        const auto generation = *root / name;
        Check(MoveFileExW(staging.c_str(), generation.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE, "Generation directory publication failed");
        staging = generation;
        CheckStop(stop);
        const auto temporary = *root / ("current-" + name + ".tmp");
        WriteText(temporary, name + "\n");
        if (stop.stop_requested()) { DeleteFileW(temporary.c_str()); throw Cancelled{}; }
        const auto pointer = SafePath(*root, L"current.txt");
        const bool published = pointer && MoveFileExW(temporary.c_str(), pointer->c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!published) DeleteFileW(temporary.c_str());
        Check(published, "Current generation pointer publication failed");
        staging.clear();
        report.generationRoot = generation;
        report.rebuiltPaths.assign(affected.begin(), affected.end());
        DeleteFileW(pending->c_str());
    } catch (const Cancelled&) { report.cancelled = true; }
    catch (const std::exception& exception) { report.error = exception.what(); }
    if (!staging.empty() && SafePath(staging)) {
        // 只清理由本调用创建且仍在 generation 根内的 staging。
        // Remove only this invocation's staging, verified within its generation root.
        const auto root = SafePath(generationsRoot_);
        if (root && staging.parent_path() == *root) { std::error_code ignored; std::filesystem::remove_all(staging, ignored); }
    }
    return report;
}

} // namespace noven::data
