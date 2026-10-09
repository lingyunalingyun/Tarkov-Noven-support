#pragma once
#include "plugins/PluginHttpOrigin.h"
#include "plugins/PluginPipe.h"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <stop_token>
#include <thread>

namespace noven::plugins {
inline constexpr unsigned HttpSchemaVersion=1;
inline constexpr std::size_t MaximumHttpBody=32*1024,MaximumHttpHeaders=16;
enum class HttpMethod {Get=1,Head=2,Post=3};
enum class HttpStatus {Ok,PermissionDenied,InvalidRequest,BlockedDestination,TooLarge,Timeout,Cancelled,TransportError,RedirectDenied};
struct HttpHeader final {std::string name,value;};
struct HttpRequest final {std::uint64_t requestId{};HttpMethod method{HttpMethod::Get};std::string url,body;std::vector<HttpHeader> headers;};
struct HttpResult final {std::uint64_t requestId{};HttpStatus status{HttpStatus::Ok};unsigned httpStatus{};std::string finalUrl,body;std::vector<HttpHeader> headers;};
bool ValidHttpRequest(const HttpRequest& request);
bool PublicHttpAddress(std::string_view numericAddress);
// 仅第一方测试可注入传输；生产始终用 Windows 后端，所有后端共用来源/地址/重定向策略。
// First-party tests inject transport; production always uses Windows, sharing origin/address/redirect policy.
class IPluginHttpBackend {
public:
    virtual ~IPluginHttpBackend()=default;
    virtual std::vector<std::string> Resolve(std::string_view host,std::stop_token stop,ipc::Deadline deadline)=0;
    virtual HttpResult Exchange(const HttpRequest& request,const HttpUrl& url,std::stop_token stop,ipc::Deadline deadline)=0;
};
std::unique_ptr<IPluginHttpBackend> MakePluginHttpBackend();
HttpResult ExecuteHttp(const HttpRequest& request,const std::vector<std::string>& origins,IPluginHttpBackend& backend,std::stop_token stop,ipc::Deadline deadline);
// 一个 Host 一套授权/预算/线程，HTTP 不触碰 Core 数据，不与其他插件共享 Cookie 或结果。
// One Host owns its grants/budget/worker; HTTP never reads Core data or shares cookies/results with another plugin.
class PluginHttpService final {
public:
    explicit PluginHttpService(std::vector<std::string> origins,std::unique_ptr<IPluginHttpBackend> backend=MakePluginHttpBackend());
    ~PluginHttpService();
    int Submit(HttpRequest request);
    std::optional<HttpResult> Pop();
    void Stop();
    HANDLE Wake() const {return wake_.Get();}
private:
    void Run(std::stop_token stop);
    struct Pending final {HttpRequest request;ipc::Deadline deadline;};
    const std::vector<std::string> origins_;
    std::unique_ptr<IPluginHttpBackend> backend_;
    ipc::Handle wake_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::mutex mutex_;std::condition_variable_any changed_;
    std::deque<Pending> queue_;std::deque<HttpResult> results_;
    std::set<std::uint64_t> pending_;
    std::deque<ipc::Deadline> admissions_;
    bool stopped_{};
    std::jthread worker_;
};
}
