#pragma once
#include "resources/ResourceService.h"
namespace noven::resources {
inline constexpr std::uint64_t ContentRangeBytes=4*1024*1024;
inline constexpr std::uint64_t MaximumContentBytes=8ULL*1024*1024*1024;
struct ContentRange final {std::uint64_t offset{},count{};};
class ContentBody {
public:
    virtual ~ContentBody()=default;
    virtual std::size_t Read(std::span<char>,std::stop_token)=0;
};
struct ContentResponse final {unsigned status{};std::string range,length;std::unique_ptr<ContentBody> body;};
// 只接受第一方固定根及逻辑文件名；测试注入后端，不放宽生产 TLS/范围策略。
// Fixed first-party root and basenames only; tests inject a backend, never weaken TLS/Range policy.
class ContentBackend {
public:
    virtual ~ContentBackend()=default;
    virtual ContentResponse Open(std::string_view url,std::optional<ContentRange>,std::stop_token)=0;
};
std::shared_ptr<ContentBackend> WindowsContentBackend(bool systemStaticProxy=false);
std::unique_ptr<ResourceStream> OpenContentRange(const ResourceSourcePolicy&,std::string_view name,
    ContentRange,std::string identity,std::uint64_t expectedTotal,std::shared_ptr<ContentBackend>,std::stop_token);
std::string FetchContentText(const ResourceSourcePolicy&,std::string_view name,std::size_t maximum,
    std::shared_ptr<ContentBackend>,std::stop_token);
std::shared_ptr<ResourceTransport> HttpsResourceTransport(ResourceSourcePolicy,std::shared_ptr<ContentBackend> = {});
}
