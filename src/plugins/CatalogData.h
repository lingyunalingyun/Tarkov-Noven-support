#pragma once
#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace noven::plugins {
inline constexpr unsigned CatalogSchemaVersion=1,MaximumCatalogRecords=64,DefaultCatalogLimit=32,MaximumDataRequests=16;
// JSON 切片再转义成传输字符串仍留有余量，不扩张既有 64 KiB 帧。
// Re-escaping the JSON slice still fits comfortably within the existing 64 KiB frame.
inline constexpr std::size_t MaximumCatalogPayloadBytes=24*1024;
enum class CatalogKind {Items=1,Tasks=2,Maps=3,RaidHistory=4,Events=5,RecentScans=6};
enum class DataOperation {List=1,Get=2};
enum class DataStatus {Ok,PermissionDenied,NotFound,InvalidRequest,Unavailable,TooLarge,Limited};
struct DataRequest final {
    std::uint64_t requestId{};
    CatalogKind catalog{CatalogKind::Items};
    DataOperation operation{DataOperation::List};
    std::string stableId;
    std::uint32_t offset{},limit{DefaultCatalogLimit};
};
struct DataResult final {std::uint64_t requestId{};DataStatus status{DataStatus::Ok};std::string payload;};
std::string_view CatalogName(CatalogKind kind);
std::string_view CatalogPermission(CatalogKind kind);
std::string_view DataStatusName(DataStatus status);
bool ValidDataRequest(const DataRequest& request);
std::size_t MaximumDataIdBytes(CatalogKind kind);
struct CatalogGrants final {
    bool valid{};
    std::vector<std::string> declared,granted;
    bool Allows(CatalogKind kind) const;
    bool AllowsPermission(std::string_view permission) const;
};
enum class RequestAdmission {Accepted,Duplicate,Limited};
// 每个认证会话各自拥有队列与窗口；完成/停止释放 ID，不共享插件之间的请求。
// Each authenticated session owns its IDs/window; completion/stop releases IDs, never shared between plugins.
class DataRequestBudget final {
public:
    explicit DataRequestBudget(unsigned rate=16):rate_(rate){}
    RequestAdmission Begin(std::uint64_t id,std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now());
    bool Complete(std::uint64_t id){return pending_.erase(id)!=0;}
    void Clear(){pending_.clear();}
    std::size_t Pending() const{return pending_.size();}
private:
    std::set<std::uint64_t> pending_;
    unsigned rate_,count_{};
    std::chrono::steady_clock::time_point window_{};
};
}
