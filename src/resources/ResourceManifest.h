#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace noven::resources {
inline constexpr std::size_t MaximumManifestBytes=256*1024;
inline constexpr std::size_t MaximumComponents=64;
inline constexpr std::uint64_t MaximumPackageBytes=2ULL*1024*1024*1024;
struct ResourceRecord final {
    std::string resourceId,stableMapId,titleZh,titleEn,version,sha256,artifact;
    std::uint64_t downloadSize{},installedSize{};
};
struct ResourceManifest final {std::vector<ResourceRecord> components;};
// 来源只由第一方调用者提供；空配置禁止下载，清单不能选择主机或命令。
// Only first-party callers supply source policy; empty policy denies downloads, never manifest-selected hosts/commands.
struct ResourceSourcePolicy final {
    std::string baseUrl;
    bool Enabled() const;
    std::string DownloadUrl(const ResourceRecord& resource) const;
};
bool ValidMapIdentity(std::string_view value);
bool ValidArtifactName(std::string_view value);
ResourceManifest ParseResourceManifest(std::string_view utf8,const std::vector<std::string>& knownMapIds);
std::string EncodeResourceManifest(const ResourceManifest& manifest);
}
