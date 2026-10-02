#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace noven::data {

struct MapAssetSpec final { std::string relativePath, url, sha256; };
struct MapAssetValidators final { std::string etag, lastModified; };
struct MapAssetSnapshot final {
    std::vector<std::uint8_t> bytes;
    MapAssetValidators validators;
};
enum class MapAssetWrite { Failed, Unchanged, Updated, Cancelled };

// 清单是本地生成的受信任输入；仍逐项检查路径和网络边界。
// The manifest is trusted local generated input; paths and network boundaries are still checked.
bool ParseMapAssetManifest(std::istream& input, std::vector<MapAssetSpec>& assets, std::string& error);
bool ValidMapAssetPath(std::string_view path) noexcept;
bool ValidMapAssetUrl(std::string_view url) noexcept;

class MapAssetStore final {
public:
    static constexpr std::size_t MaxBytes = 16 * 1024 * 1024;
    static constexpr std::uint32_t MaxDimension = 8192;
    static constexpr std::uint64_t MaxPixels = 16 * 1024 * 1024;

    explicit MapAssetStore(std::filesystem::path cacheRoot, std::filesystem::path fallbackRoot = {});
    static bool ValidatePng(std::span<const std::uint8_t> bytes);
    static std::string Sha256(std::span<const std::uint8_t> bytes);

    // Read 只读取缓存；Resolve 同时支持打包回退，不依赖网络。
    // Read inspects the cache; Resolve also supports bundled fallback, without network access.
    MapAssetSnapshot Read(const MapAssetSpec& asset) const;
    std::optional<std::filesystem::path> Resolve(std::string_view relativePath,
                                                std::uint32_t maxDimension = MaxDimension) const;
    MapAssetWrite Save(const MapAssetSpec& asset, std::span<const std::uint8_t> bytes,
                      const MapAssetValidators& validators, std::stop_token stop = {}) const;

private:
    std::filesystem::path cacheRoot_, fallbackRoot_;
};

} // namespace noven::data
