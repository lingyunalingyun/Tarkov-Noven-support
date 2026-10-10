#pragma once
#include "updates/VersionStore.h"
#include "common/AppPaths.h"
namespace noven::updates {
enum class UpdateState {Unconfigured,Idle,Checking,Current,Available,Downloading,Paused,Staged,Error};
enum class UpdateAction {Check,Download,Pause,Resume,Cancel};
struct UpdateSnapshot final {
    UpdateState state{UpdateState::Unconfigured};std::string current,target,previous,notes,error;
    std::uint64_t fullBytes{},reusedBytes{},downloadBytes{},received{};bool constructing{};
};
class UpdateSource:public UpdateTransport {
public:
    virtual std::string FetchManifest(std::stop_token)=0;
};
class UpdateService final {
public:
    UpdateService(common::AppPaths,std::filesystem::path bootstrap,std::string current,
        std::vector<ReleasePublicKey>,std::shared_ptr<UpdateSource> source={},std::function<void()> changed={});
    ~UpdateService();
    UpdateSnapshot Snapshot() const;
    bool Act(UpdateAction);
    bool StartupCheck(std::int64_t utcSeconds);
    bool LaunchApply(bool rollback=false);
    void Shutdown();
    bool WaitIdle(std::chrono::milliseconds timeout);
private:
    void Worker(std::stop_token);
    void Notify() const;
    common::AppPaths paths_;std::filesystem::path bootstrap_;std::vector<ReleasePublicKey> keys_;std::shared_ptr<UpdateSource> source_;
    std::function<void()> changed_;mutable std::mutex mutex_;std::condition_variable condition_,idle_;
    UpdateSnapshot view_;std::optional<AuthenticatedRelease> release_;std::optional<UpdatePlan> plan_;
    std::optional<UpdateAction> pending_;std::stop_source operation_;std::jthread worker_;bool stopping_{},busy_{},paused_{};
};
}
