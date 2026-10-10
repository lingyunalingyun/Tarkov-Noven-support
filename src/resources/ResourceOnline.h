#pragma once
#include "resources/ContentHttps.h"
namespace noven::resources {
struct ResourcePublicKey final {std::string id;std::vector<unsigned char> blob;};
std::vector<unsigned char> ResourceManifestDigest(std::string_view payload);
std::string ResourceHex(std::span<const unsigned char> bytes);
std::vector<unsigned char> ResourceUnhex(std::string_view hex,std::size_t maximum);
ResourceManifest VerifyResourceManifest(std::string_view envelope,std::span<const ResourcePublicKey> keys,
    const std::vector<std::string>& knownMaps,bool synthetic=false);
// 仅此适配器可跟随固定仓库 Releases 的单次 CDN 跳转；更新传输不启用重定向。
// Only this adapter follows a fixed repository's single CDN hop; update transport stays redirect-free.
bool TrustedGithubAssetRedirect(std::string_view location);
std::shared_ptr<ContentBackend> GithubReleaseBackend(std::string fixedRoot,std::shared_ptr<ContentBackend> backend={},std::span<const ResourceRecord> assets={});
struct ResourceRemotePolicy final {ResourceSourcePolicy metadata,assets;std::vector<ResourcePublicKey> keys;};
ResourceRemotePolicy CompiledResourcePolicy();
void RefreshRemoteResources(ResourceService& service,const common::AppPaths& paths,
    const ResourceRemotePolicy& policy,const std::vector<std::string>& maps,std::stop_token stop,
    std::shared_ptr<ContentBackend> backend={},bool synthetic=false);
}
