#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace noven::updates {
inline constexpr std::uint64_t ChunkBytes=4ULL*1024*1024,ChunkThreshold=8ULL*1024*1024;
inline constexpr std::uint64_t MaximumReleaseBytes=8ULL*1024*1024*1024;
inline constexpr std::size_t MaximumReleaseFiles=4096,MaximumManifestBytes=4*1024*1024;
struct ContentRange final {std::uint64_t fileOffset{},size{},packOffset{};std::string sha256,pack;};
struct ReleaseFile final {std::string path,sha256;std::uint64_t size{};std::vector<ContentRange> content;};
struct ReleaseManifest final {std::string version,publishedAt,notes;std::vector<ReleaseFile> files;};
struct ReleasePublicKey final {std::string id;std::vector<unsigned char> cngPublicBlob;};
// 清单签名覆盖原始字节及独立信任域；远程内容不能提供自己的信任密钥。
// Sign exact payload bytes in a separate trust domain; remote content never supplies trusted keys.
class AuthenticatedRelease final {
public:
    const ReleaseManifest& Manifest() const noexcept {return manifest_;}
    const std::string& Identity() const noexcept {return identity_;}
private:
    AuthenticatedRelease()=default;
    ReleaseManifest manifest_;std::string identity_;
    friend AuthenticatedRelease VerifyRelease(std::string_view,std::span<const ReleasePublicKey>);
};
bool ValidReleasePath(std::string_view path);
bool ValidReleaseVersion(std::string_view version);
int CompareReleaseVersions(std::string_view left,std::string_view right);
ReleaseManifest ParseReleaseManifest(std::string_view utf8);
std::string EncodeReleaseManifest(const ReleaseManifest& manifest);
AuthenticatedRelease VerifyRelease(std::string_view envelope,std::span<const ReleasePublicKey> keys);
std::vector<unsigned char> ReleaseDigest(std::string_view payload);
std::string Hex(std::span<const unsigned char> bytes);
std::vector<unsigned char> Unhex(std::string_view text,std::size_t maximum);
}
