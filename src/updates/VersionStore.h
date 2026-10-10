#pragma once
#include "updates/UpdateEngine.h"
#include <optional>
namespace noven::updates {
struct Activation final {std::string active,previous,token;bool pending{};unsigned attempts{};};
class VersionStore final {
public:
    VersionStore(std::filesystem::path program,std::string installerVersion,std::vector<ReleasePublicKey> keys={});
    Activation Read() const;
    bool Validate(std::string_view version) const;
    std::filesystem::path Resolve(std::string_view version) const;
    // 仅安装器/离线评审调用一次；不从远程内容建立初始信任。
    // Installer/offline review seeds baseline once; remote content cannot establish initial trust.
    void SeedInitial();
    void RemoveInstalledVersions();
    void Activate(const AuthenticatedRelease&);
    void Rollback();
    Activation BeginLaunch();
    void ConfirmHealthy(std::string_view version,std::string_view token);
    void DeferUnconfirmedBoot(std::string_view version,std::string_view token);
private:
    void Write(const Activation&) const;
    std::filesystem::path root_;std::string initial_;std::vector<ReleasePublicKey> keys_;
};
}
