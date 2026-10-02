#include "data/MapAssetStore.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <set>
#include <sstream>

namespace noven::data {
namespace {
constexpr std::string_view HostPrefix = "https://assets.tarkov.dev/";

bool CleanHeader(std::string_view text) {
    return text.size() <= 1024 && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return c >= 32 && c <= 126;
    });
}
bool HexDigest(std::string_view text) {
    return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == text.npos;
}
std::uint32_t BigEndian(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16)
        | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint32_t Crc(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xffffffff;
    for (auto b : bytes) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

// 拒绝缓存根及所有已存在祖先中的 junction/symlink，避免词法检查后落到根外。
// Reject reparse points in the root and existing ancestors, not just lexical traversal.
std::optional<std::filesystem::path> SafePath(const std::filesystem::path& root, std::string_view relative) {
    if (root.empty()) return {};
    std::error_code error;
    auto path = std::filesystem::absolute(root, error);
    if (error) return {};
    path = (path / std::filesystem::path(relative)).lexically_normal();
    auto current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return {};
        } else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
            return {};
        }
    }
    return path;
}
std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (!input || size <= 0 || size > static_cast<std::streamoff>(limit)) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}
bool AtomicWrite(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                 std::stop_token stop) {
    static std::atomic<unsigned long> sequence{};
    auto temporary = path;
    temporary += L".asset-" + std::to_wstring(GetCurrentProcessId()) + L"-"
        + std::to_wstring(sequence.fetch_add(1)) + L".tmp";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written{};
    const bool ok = !stop.stop_requested()
        && WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    const bool moved = ok && !stop.stop_requested() && MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!moved) DeleteFileW(temporary.c_str());
    return moved;
}
struct ComScope final {
    HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
} // namespace

bool ValidMapAssetPath(std::string_view path) noexcept {
    if (path.empty() || path.size() > 240 || !path.ends_with(".png")) return false;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == ".." || part.back() == '.'
            || part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != part.npos)
            return false;
        std::array<char, 240> upper{};
        const auto dot = part.find('.');
        const auto stem = part.substr(0, dot);
        for (std::size_t i = 0; i < stem.size(); ++i)
            upper[i] = stem[i] >= 'a' && stem[i] <= 'z' ? char(stem[i] - 'a' + 'A') : stem[i];
        const std::string_view name(upper.data(), stem.size());
        if (name == "CON" || name == "PRN" || name == "AUX" || name == "NUL"
            || (name.size() == 4 && (name.starts_with("COM") || name.starts_with("LPT"))
                && name[3] >= '1' && name[3] <= '9')) return false;
        if (slash == path.npos) break;
        path.remove_prefix(slash + 1);
    }
    return true;
}
bool ValidMapAssetUrl(std::string_view url) noexcept {
    if (!url.starts_with(HostPrefix) || url.size() > 2048) return false;
    // 只接受生成器给出的规范 PNG 路径，不接受编码后的分隔符、查询、凭据或端口。
    // Accept canonical generated PNG paths only, without encoded separators, queries, credentials or ports.
    return ValidMapAssetPath(url.substr(HostPrefix.size()));
}

bool ParseMapAssetManifest(std::istream& input, std::vector<MapAssetSpec>& assets, std::string& error) {
    assets.clear();
    error.clear();
    std::vector<MapAssetSpec> parsed;
    std::set<std::string> paths;
    std::string line;
    bool header{};
    std::size_t total{}, lineNumber{}, columns{};
    auto fail = [&](std::string_view reason) {
        error = "Map asset manifest line " + std::to_string(lineNumber) + ": " + std::string(reason);
        return false;
    };
    while (std::getline(input, line)) {
        ++lineNumber;
        total += line.size() + 1;
        if (line.size() > 2300 || total > 4 * 1024 * 1024) return fail("size limit");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lineNumber == 1 && line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
        if (line.empty() || line.front() == '#') continue;
        if (!header) {
            if (line == "relativePath\turl") columns = 2;
            else if (line == "relativePath\turl\tsha256") columns = 3;
            else return fail("expected relativePath, url, optional sha256 TSV header");
            header = true;
            continue;
        }
        std::vector<std::string> fields;
        std::size_t begin{};
        for (;;) {
            const auto end = line.find('\t', begin);
            fields.push_back(line.substr(begin, end == line.npos ? end : end - begin));
            if (end == line.npos) break;
            begin = end + 1;
        }
        if (fields.size() != columns) return fail("column count");
        MapAssetSpec asset{fields[0], fields[1], columns == 3 ? fields[2] : ""};
        std::transform(asset.sha256.begin(), asset.sha256.end(), asset.sha256.begin(), [](char c) {
            return c >= 'A' && c <= 'F' ? char(c - 'A' + 'a') : c;
        });
        if (!ValidMapAssetPath(asset.relativePath) || !ValidMapAssetUrl(asset.url)
            || (!asset.sha256.empty() && !HexDigest(asset.sha256))) return fail("invalid path, URL or hash");
        auto key = asset.relativePath;
        std::transform(key.begin(), key.end(), key.begin(), [](char c) {
            return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        });
        if (!paths.insert(key).second) return fail("duplicate Windows path");
        parsed.push_back(std::move(asset));
        if (parsed.size() > 20000) return fail("entry limit");
    }
    if (input.bad() || !header) return fail("unreadable or missing header");
    assets = std::move(parsed);
    return true;
}

MapAssetStore::MapAssetStore(std::filesystem::path cacheRoot, std::filesystem::path fallbackRoot)
    : cacheRoot_(std::move(cacheRoot)), fallbackRoot_(std::move(fallbackRoot)) {}

std::string MapAssetStore::Sha256(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > MaxBytes) return {};
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    std::array<unsigned char, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return {};
    std::string result;
    for (auto b : digest) { result += "0123456789abcdef"[b >> 4]; result += "0123456789abcdef"[b & 15]; }
    return result;
}

bool MapAssetStore::ValidatePng(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 57 || bytes.size() > MaxBytes
        || !std::equal(signature.begin(), signature.end(), bytes.begin())) return false;
    std::size_t offset = 8;
    bool header{}, data{}, end{}, afterData{};
    std::uint32_t width{}, height{};
    std::array<std::uint8_t, 2> zlibHeader{};
    std::size_t compressedBytes{};
    while (offset + 12 <= bytes.size()) {
        const auto length = BigEndian(bytes.data() + offset);
        if (length > bytes.size() - offset - 12) return false;
        const std::string_view type(reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
        if (Crc(bytes.subspan(offset + 4, length + 4)) != BigEndian(bytes.data() + offset + 8 + length)) return false;
        if (!header && type != "IHDR") return false;
        if (type == "IHDR") {
            if (header || length != 13) return false;
            width = BigEndian(bytes.data() + offset + 8);
            height = BigEndian(bytes.data() + offset + 12);
            if (!width || !height || width > MaxDimension || height > MaxDimension
                || std::uint64_t(width) * height > MaxPixels) return false;
            header = true;
        } else if (type == "IDAT") {
            if (afterData) return false;
            for (std::size_t i = 0; i < length && compressedBytes + i < zlibHeader.size(); ++i)
                zlibHeader[compressedBytes + i] = bytes[offset + 8 + i];
            compressedBytes += length;
            data = true;
        } else if (type == "IEND") {
            if (length || !data) return false;
            end = true;
        } else {
            if (type == "acTL" || type == "fcTL" || type == "fdAT") return false;
            if (type == "PLTE") { if (data || !length || length > 768 || length % 3) return false; }
            else if (!(bytes[offset + 4] & 32)) return false;
            if (data) afterData = true;
        }
        offset += length + 12;
        if (end) break;
    }
    if (!end || offset != bytes.size() || compressedBytes < 6
        || (zlibHeader[0] & 15) != 8 || (zlibHeader[0] >> 4) > 7
        || ((unsigned(zlibHeader[0]) << 8) | zlibHeader[1]) % 31 != 0
        || (zlibHeader[1] & 32)) return false;
    ComScope com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) return false;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT frames{}, decodedWidth{}, decodedHeight{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        || FAILED(factory->CreateStream(&stream))
        || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))
        || FAILED(factory->CreateDecoder(GUID_ContainerFormatPng, nullptr, &decoder))
        || FAILED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnLoad))
        || FAILED(decoder->GetFrameCount(&frames)) || frames != 1
        || FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&decodedWidth, &decodedHeight))
        || decodedWidth != width || decodedHeight != height
        || FAILED(factory->CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    // 真正解压全部像素，避免仅凭 PNG 头接受截断或损坏的压缩数据。
    // Decode all pixels rather than accepting headers over truncated/corrupt compressed data.
    std::vector<BYTE> pixels(static_cast<std::size_t>(width) * height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
}

MapAssetSnapshot MapAssetStore::Read(const MapAssetSpec& asset) const {
    MapAssetSnapshot result;
    if (!ValidMapAssetPath(asset.relativePath) || !ValidMapAssetUrl(asset.url)) return result;
    const auto path = SafePath(cacheRoot_, asset.relativePath);
    if (!path) return result;
    result.bytes = ReadBytes(*path, MaxBytes);
    if (!ValidatePng(result.bytes)) { result.bytes.clear(); return result; }
    const auto metadata = SafePath(cacheRoot_, asset.relativePath + ".http");
    if (!metadata) return result;
    const auto bytes = ReadBytes(*metadata, 8192);
    std::istringstream input(std::string(bytes.begin(), bytes.end()));
    std::string url, digest, etag, modified, extra;
    if (std::getline(input, url) && std::getline(input, digest) && std::getline(input, etag)
        && std::getline(input, modified) && !std::getline(input, extra)
        && url == asset.url && HexDigest(digest) && digest == Sha256(result.bytes)
        && CleanHeader(etag) && CleanHeader(modified)) result.validators = {etag, modified};
    return result;
}

std::optional<std::filesystem::path> MapAssetStore::Resolve(std::string_view relativePath, std::uint32_t maxDimension) const {
    if (!ValidMapAssetPath(relativePath)) return {};
    for (const auto* root : {&cacheRoot_, &fallbackRoot_}) {
        const auto path = SafePath(*root, relativePath);
        if (path) {
            const auto bytes = ReadBytes(*path, MaxBytes);
            if (bytes.size() >= 24 && BigEndian(bytes.data() + 16) <= maxDimension
                && BigEndian(bytes.data() + 20) <= maxDimension && ValidatePng(bytes)) return path;
        }
    }
    return {};
}

MapAssetWrite MapAssetStore::Save(const MapAssetSpec& asset, std::span<const std::uint8_t> bytes,
                                 const MapAssetValidators& validators, std::stop_token stop) const {
    if (stop.stop_requested()) return MapAssetWrite::Cancelled;
    if (!ValidMapAssetPath(asset.relativePath) || !ValidMapAssetUrl(asset.url)
        || (!asset.sha256.empty() && !HexDigest(asset.sha256)) || !ValidatePng(bytes)) return MapAssetWrite::Failed;
    const auto digest = Sha256(bytes);
    if (digest.empty() || (!asset.sha256.empty() && digest != asset.sha256)) return MapAssetWrite::Failed;
    auto path = SafePath(cacheRoot_, asset.relativePath);
    if (!path) return MapAssetWrite::Failed;
    std::error_code error;
    std::filesystem::create_directories(path->parent_path(), error);
    if (error || !SafePath(cacheRoot_, asset.relativePath)) return MapAssetWrite::Failed;
    const auto previous = ReadBytes(*path, MaxBytes);
    const bool same = previous.size() == bytes.size() && std::equal(previous.begin(), previous.end(), bytes.begin());
    bool displaySame = same;
    if (!same && !ValidatePng(previous)) {
        const auto fallback = SafePath(fallbackRoot_, asset.relativePath);
        if (fallback) {
            const auto bundled = ReadBytes(*fallback, MaxBytes);
            displaySame = bundled.size() == bytes.size() && std::equal(bundled.begin(), bundled.end(), bytes.begin());
        }
    }
    if (stop.stop_requested()) return MapAssetWrite::Cancelled;
    if (!same && !AtomicWrite(*path, bytes, stop))
        return stop.stop_requested() ? MapAssetWrite::Cancelled : MapAssetWrite::Failed;
    // PNG 是事务提交点；旁车失败不破坏有效 PNG。摘要绑定可防止旧条件头配到新文件。
    // The PNG rename is the commit point; failed sidecars cannot invalidate PNGs. Digest binds validators to bytes.
    const auto metadata = SafePath(cacheRoot_, asset.relativePath + ".http");
    const std::string text = asset.url + "\n" + digest + "\n"
        + (CleanHeader(validators.etag) ? validators.etag : "") + "\n"
        + (CleanHeader(validators.lastModified) ? validators.lastModified : "") + "\n";
    if (metadata) {
        const auto previousMetadata = ReadBytes(*metadata, 8192);
        if (std::string(previousMetadata.begin(), previousMetadata.end()) != text)
            AtomicWrite(*metadata, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), stop);
    }
    return displaySame ? MapAssetWrite::Unchanged : MapAssetWrite::Updated;
}

} // namespace noven::data
