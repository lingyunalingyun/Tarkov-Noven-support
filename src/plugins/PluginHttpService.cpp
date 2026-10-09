#include <winsock2.h>
#include <ws2tcpip.h>
#include "plugins/PluginHttpService.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace noven::plugins {
namespace {
std::string Lower(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
bool SafeHeader(const HttpHeader& header){
    if(header.name.empty()||header.name.size()>64||header.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")!=header.name.npos||header.value.size()>1024)return false;
    for(unsigned char c:header.value)if(c<32||c>126)return false;
    const auto name=Lower(header.name);
    for(const auto denied:{"host","connection","content-length","transfer-encoding","proxy-authorization","proxy-connection","cookie","set-cookie","expect","upgrade","te","trailer"})if(name==denied)return false;
    return true;
}
bool Allowed(const HttpUrl& url,const std::vector<std::string>& origins){return std::binary_search(origins.begin(),origins.end(),url.origin);}
HttpResult Error(const HttpRequest& request,HttpStatus status){return {request.requestId,status};}
}
bool ValidHttpRequest(const HttpRequest& request){
    if(!request.requestId||request.requestId>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())||!ParseHttpUrl(request.url)||request.body.size()>MaximumHttpBody||request.headers.size()>MaximumHttpHeaders)return false;
    if(request.method!=HttpMethod::Get&&request.method!=HttpMethod::Head&&request.method!=HttpMethod::Post)return false;
    if(request.method!=HttpMethod::Post&&!request.body.empty())return false;
    std::size_t bytes=0;std::set<std::string> names;for(const auto& header:request.headers){if(!SafeHeader(header)||!names.insert(Lower(header.name)).second)return false;bytes+=header.name.size()+header.value.size();}return bytes<=4096;
}
bool PublicHttpAddress(std::string_view value){
    const std::string text(value);IN_ADDR v4{};IN6_ADDR v6{};
    if(InetPtonA(AF_INET,text.c_str(),&v4)==1){const auto n=ntohl(v4.S_un.S_addr);const unsigned a=n>>24,b=(n>>16)&255,c=(n>>8)&255;
        return a!=0&&a!=10&&a!=127&&a<224&&!(a==100&&b>=64&&b<=127)&&!(a==169&&b==254)&&!(a==172&&b>=16&&b<=31)
            &&!(a==192&&(b==168||b==0||(b==88&&c==99)))&&!(a==198&&(b==18||b==19||(b==51&&c==100)))&&!(a==203&&b==0&&c==113);
    }
    if(InetPtonA(AF_INET6,text.c_str(),&v6)==1){const auto* b=v6.u.Byte;
        // 仅全球单播，排除 ULA/link-local/multicast/mapped/NAT64/6to4/Teredo 等绕行地址。
        // Global unicast only, excluding ULA/link-local/multicast/mapped/NAT64/6to4/Teredo tunnels.
        return (b[0]&0xe0)==0x20&&!(b[0]==0x20&&b[1]==0x02)&&!(b[0]==0x20&&b[1]==0x01&&b[2]==0&&b[3]==0)
            &&!(b[0]==0x20&&b[1]==0x01&&b[2]==0x0d&&b[3]==0xb8);
    }return false;
}
HttpResult ExecuteHttp(const HttpRequest& original,const std::vector<std::string>& origins,IPluginHttpBackend& backend,std::stop_token stop,ipc::Deadline deadline){
    if(!ValidHttpRequest(original))return Error(original,HttpStatus::InvalidRequest);
    if(!ValidHttpOrigins(origins))return Error(original,HttpStatus::PermissionDenied);
    auto request=original;std::set<std::string> visited;
    for(unsigned redirects=0;;++redirects){
        if(stop.stop_requested())return Error(original,HttpStatus::Cancelled);if(std::chrono::steady_clock::now()>=deadline)return Error(original,HttpStatus::Timeout);
        const auto url=ParseHttpUrl(request.url);if(!url||!Allowed(*url,origins))return Error(original,redirects?HttpStatus::RedirectDenied:HttpStatus::PermissionDenied);
        if(!visited.insert(url->origin+url->target).second)return Error(original,HttpStatus::RedirectDenied);
        const auto addresses=backend.Resolve(url->host,stop,deadline);
        if(stop.stop_requested())return Error(original,HttpStatus::Cancelled);if(std::chrono::steady_clock::now()>=deadline)return Error(original,HttpStatus::Timeout);
        if(addresses.empty()||addresses.size()>64||!std::all_of(addresses.begin(),addresses.end(),PublicHttpAddress))return Error(original,HttpStatus::BlockedDestination);
        auto result=backend.Exchange(request,*url,stop,deadline);result.requestId=original.requestId;
        if(stop.stop_requested())return Error(original,HttpStatus::Cancelled);if(std::chrono::steady_clock::now()>=deadline)return Error(original,HttpStatus::Timeout);
        if(result.status!=HttpStatus::Ok){result.body.clear();result.headers.clear();return result;}
        if(result.body.size()>MaximumHttpBody||result.headers.size()>MaximumHttpHeaders)return Error(original,HttpStatus::TooLarge);
        std::size_t headerBytes=0;for(const auto& header:result.headers){if(!SafeHeader(header))return Error(original,HttpStatus::TransportError);headerBytes+=header.name.size()+header.value.size();}if(headerBytes>4096)return Error(original,HttpStatus::TooLarge);
        result.finalUrl=url->origin+url->target;
        if(result.httpStatus!=301&&result.httpStatus!=302&&result.httpStatus!=303&&result.httpStatus!=307&&result.httpStatus!=308)return result;
        if(redirects==5)return Error(original,HttpStatus::RedirectDenied);
        auto location=std::find_if(result.headers.begin(),result.headers.end(),[](const auto& h){return Lower(h.name)=="location";});if(location==result.headers.end())return Error(original,HttpStatus::RedirectDenied);
        auto destination=location->value;if(destination.starts_with('/')&&!destination.starts_with("//"))destination=url->origin+destination;
        const auto next=ParseHttpUrl(destination);if(!next||!Allowed(*next,origins))return Error(original,HttpStatus::RedirectDenied);
        if(next->origin!=url->origin){request.headers.clear();if(request.method==HttpMethod::Post&&(result.httpStatus==307||result.httpStatus==308))return Error(original,HttpStatus::RedirectDenied);}
        if(result.httpStatus==303||(request.method==HttpMethod::Post&&(result.httpStatus==301||result.httpStatus==302))){request.method=HttpMethod::Get;request.body.clear();request.headers.clear();}
        request.url=std::move(destination);
    }
}
PluginHttpService::PluginHttpService(std::vector<std::string> origins,std::unique_ptr<IPluginHttpBackend> backend):origins_(std::move(origins)),backend_(std::move(backend)){
    if(!ValidHttpOrigins(origins_)||!backend_||!wake_)throw std::runtime_error("HTTP configuration");worker_=std::jthread([this](std::stop_token stop){Run(stop);});
}
PluginHttpService::~PluginHttpService(){Stop();}
int PluginHttpService::Submit(HttpRequest request){
    std::lock_guard lock(mutex_);if(stopped_)return -4;if(!ValidHttpRequest(request))return -1;
    if(!Allowed(*ParseHttpUrl(request.url),origins_))return -2;if(pending_.contains(request.requestId))return -4;
    const auto now=std::chrono::steady_clock::now();while(!admissions_.empty()&&now-admissions_.front()>=std::chrono::minutes(1))admissions_.pop_front();
    if(pending_.size()>=8||admissions_.size()>=16)return -3;
    admissions_.push_back(now);pending_.insert(request.requestId);queue_.push_back({std::move(request),now+std::chrono::seconds(20)});changed_.notify_one();return 0;
}
void PluginHttpService::Run(std::stop_token stop){
    for(;;){Pending current;{std::unique_lock lock(mutex_);if(!changed_.wait(lock,stop,[&]{return stopped_||!queue_.empty();})||stopped_)return;current=std::move(queue_.front());queue_.pop_front();}
        HttpResult result;try{result=ExecuteHttp(current.request,origins_,*backend_,stop,current.deadline);}catch(const std::exception&){result=Error(current.request,HttpStatus::TransportError);}
        {std::lock_guard lock(mutex_);if(stopped_||stop.stop_requested())return;results_.push_back(std::move(result));SetEvent(wake_.Get());}
    }
}
std::optional<HttpResult> PluginHttpService::Pop(){std::lock_guard lock(mutex_);if(stopped_||results_.empty())return {};auto result=std::move(results_.front());results_.pop_front();pending_.erase(result.requestId);if(results_.empty())ResetEvent(wake_.Get());return result;}
void PluginHttpService::Stop(){std::lock_guard lock(mutex_);stopped_=true;queue_.clear();results_.clear();pending_.clear();ResetEvent(wake_.Get());worker_.request_stop();changed_.notify_all();}
}
