#pragma once
#include "updates/UpdatePlanner.h"
#include "resources/ResourceService.h"
namespace noven::updates {
// 与资源服务共享有界字节流，但应用更新使用独立的签名身份和范围授权。
// Share bounded streams with ResourceService, not manifests or authorization.
class UpdateTransport {
public:
    virtual ~UpdateTransport()=default;
    virtual std::unique_ptr<resources::ResourceStream> Open(std::string_view pack,
        std::uint64_t offset,std::uint64_t count,std::string_view manifestIdentity,std::stop_token)=0;
};
struct UpdateProgress final {std::uint64_t downloaded{},total{};std::string file;bool constructing{};};
class UpdateEngine final {
public:
    UpdateEngine(std::filesystem::path programRoot,std::filesystem::path cacheRoot);
    std::filesystem::path Stage(const AuthenticatedRelease&,const UpdatePlan&,
        UpdateTransport&,std::stop_token={},std::function<void(UpdateProgress)> progress={},
        std::function<std::uint64_t()> diskSpace={});
    bool ValidateVersion(const AuthenticatedRelease&) const;
private:
    std::filesystem::path program_,cache_;
};
}
