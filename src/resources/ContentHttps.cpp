#include "resources/ContentHttps.h"
#include "plugins/PluginHttpOrigin.h"
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <charconv>
namespace noven::resources {
namespace {
[[noreturn]] void Failed(){throw std::runtime_error("invalid or unavailable HTTPS content");}
void CheckStop(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("cancelled HTTPS content");}
std::uint64_t Number(std::string_view value){std::uint64_t n{};const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),n);
    if(value.empty()||error!=std::errc{}||end!=value.data()+value.size())Failed();return n;}
std::string Url(const ResourceSourcePolicy& policy,std::string_view name){
    if(policy.baseUrl.size()>512||!policy.Enabled()||name.empty()||name.size()>128||name.front()=='.'||name.find("..")!=name.npos||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._")!=name.npos)Failed();return policy.baseUrl+std::string(name);}
std::uint64_t ValidateRange(const ContentResponse& response,ContentRange range){
    if(response.status!=206||!response.body||!range.count||range.offset>MaximumContentBytes||range.count>MaximumContentBytes-range.offset||response.range.size()>128||!response.range.starts_with("bytes "))Failed();
    const auto value=std::string_view(response.range).substr(6);const auto dash=value.find('-'),slash=value.find('/');
    if(dash==value.npos||slash==value.npos||slash<=dash)Failed();const auto first=Number(value.substr(0,dash)),last=Number(value.substr(dash+1,slash-dash-1)),total=Number(value.substr(slash+1));
    if(first!=range.offset||last!=range.offset+range.count-1||total<=last||total>MaximumContentBytes||(!response.length.empty()&&Number(response.length)!=range.count))Failed();return total;
}
struct Handle final {HINTERNET value{};~Handle(){if(value)WinHttpCloseHandle(value);}Handle()=default;Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
std::wstring Wide(std::string_view value){return {value.begin(),value.end()};}
class WindowsBody final:public ContentBody {
public:
    Handle session,connection,request;std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
    void Ready(std::stop_token stop){CheckStop(stop);if(std::chrono::steady_clock::now()>=deadline)Failed();}
    std::size_t Read(std::span<char> out,std::stop_token stop)override{Ready(stop);DWORD read{};const auto n=static_cast<DWORD>(std::min<std::size_t>(out.size(),65536));
        if(!n)return 0;if(!WinHttpReadData(request.value,out.data(),n,&read))Failed();Ready(stop);return read;}
};
std::string Header(HINTERNET request,DWORD query){std::array<wchar_t,128> text{};DWORD bytes=static_cast<DWORD>(sizeof(text));
    if(!WinHttpQueryHeaders(request,query,WINHTTP_HEADER_NAME_BY_INDEX,text.data(),&bytes,WINHTTP_NO_HEADER_INDEX)){
        if(GetLastError()==ERROR_WINHTTP_HEADER_NOT_FOUND)return {};Failed();}
    std::string result;for(auto c:std::wstring_view(text.data(),bytes/sizeof(wchar_t))){if(!c)break;if(c<32||c>126)Failed();result+=static_cast<char>(c);}return result;
}
class WindowsBackend final:public ContentBackend {
public:
    ContentResponse Open(std::string_view url,std::optional<ContentRange> range,std::stop_token stop)override{
        CheckStop(stop);const auto parsed=plugins::ParseHttpUrl(url);if(!parsed||parsed->port!=443)Failed();
        auto body=std::make_unique<WindowsBody>();
        // 同步操作只在服务工作线程运行；有限超时之间检查取消，不跨线程关闭同步句柄。
        // Worker-only synchronous calls: bounded timeouts/checks, never cross-thread close a synchronous handle.
        body->session.value=WinHttpOpen(L"Noven Content/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        if(!body->session.value||!WinHttpSetTimeouts(body->session.value,1000,2000,2000,2000))Failed();
        body->connection.value=WinHttpConnect(body->session.value,Wide(parsed->host).c_str(),443,0);if(!body->connection.value)Failed();
        body->request.value=WinHttpOpenRequest(body->connection.value,L"GET",Wide(parsed->target).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
        if(!body->request.value)Failed();DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION,auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH,headers=16*1024;
        if(!WinHttpSetOption(body->request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled))||
            !WinHttpSetOption(body->request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof(auth))||
            !WinHttpSetOption(body->request.value,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&headers,sizeof(headers)))Failed();
        std::wstring requestHeaders=L"Accept-Encoding: identity\r\n";
        if(range){if(!range->count||range->count>ContentRangeBytes||range->offset>MaximumContentBytes||range->count>MaximumContentBytes-range->offset)Failed();
            requestHeaders+=L"Range: bytes="+std::to_wstring(range->offset)+L"-"+std::to_wstring(range->offset+range->count-1)+L"\r\n";}
        if(!WinHttpSendRequest(body->request.value,requestHeaders.c_str(),static_cast<DWORD>(requestHeaders.size()),WINHTTP_NO_REQUEST_DATA,0,0,0))Failed();body->Ready(stop);
        if(!WinHttpReceiveResponse(body->request.value,nullptr))Failed();body->Ready(stop);DWORD status{},length=sizeof(status);
        if(!WinHttpQueryHeaders(body->request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX))Failed();
        const auto encoding=Header(body->request.value,WINHTTP_QUERY_CONTENT_ENCODING);if(!encoding.empty()&&encoding!="identity")Failed();
        return {status,Header(body->request.value,WINHTTP_QUERY_CONTENT_RANGE),Header(body->request.value,WINHTTP_QUERY_CONTENT_LENGTH),std::move(body)};
    }
};
class Stream final:public ResourceStream {
    ResourceSourcePolicy policy_;std::string name_,identity_;std::shared_ptr<ContentBackend> backend_;ContentResponse response_;
    std::uint64_t offset_,count_,read_{},segmentLeft_{},total_{};
    void Next(std::stop_token stop){const ContentRange range{offset_+read_,std::min<std::uint64_t>(ContentRangeBytes,count_-read_)};
        response_=backend_->Open(Url(policy_,name_),range,stop);const auto total=ValidateRange(response_,range);
        if((total_&&total!=total_)||offset_>total||count_>total-offset_)Failed();total_=total;segmentLeft_=range.count;}
public:
    Stream(ResourceSourcePolicy policy,std::string name,ContentRange range,std::string identity,std::uint64_t total,std::shared_ptr<ContentBackend> backend,std::stop_token stop):
        policy_(std::move(policy)),name_(std::move(name)),identity_(std::move(identity)),backend_(std::move(backend)),offset_(range.offset),count_(range.count),total_(total){
        if(!backend_||!count_||offset_>MaximumContentBytes||count_>MaximumContentBytes-offset_)Failed();Next(stop);}
    std::uint64_t Offset()const override{return offset_;}std::uint64_t TotalSize()const override{return total_;}std::string Identity()const override{return identity_;}
    std::size_t Read(std::span<char> out,std::stop_token stop)override{CheckStop(stop);if(out.empty()||read_==count_)return 0;if(!segmentLeft_)Next(stop);
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(segmentLeft_,std::min<std::size_t>(out.size(),65536)));
        const auto got=response_.body->Read(out.first(n),stop);if(!got||got>n)Failed();read_+=got;segmentLeft_-=got;
        if(!segmentLeft_){std::array<char,1> extra{};if(response_.body->Read(extra,stop))Failed();}return got;}
};
class ResourceHttps final:public ResourceTransport {
    ResourceSourcePolicy policy_;std::shared_ptr<ContentBackend> backend_;
public:
    ResourceHttps(ResourceSourcePolicy policy,std::shared_ptr<ContentBackend> backend):policy_(std::move(policy)),backend_(std::move(backend)){}
    std::unique_ptr<ResourceStream> Open(const ResourceRecord& record,std::uint64_t offset,std::stop_token stop)override{
        if(!ValidArtifactName(record.artifact)||!record.downloadSize||record.downloadSize>MaximumPackageBytes||offset>=record.downloadSize)Failed();
        return OpenContentRange(policy_,record.artifact,{offset,record.downloadSize-offset},record.sha256,record.downloadSize,backend_,stop);}
};
}
std::shared_ptr<ContentBackend> WindowsContentBackend(){return std::make_shared<WindowsBackend>();}
std::unique_ptr<ResourceStream> OpenContentRange(const ResourceSourcePolicy& policy,std::string_view name,ContentRange range,std::string identity,std::uint64_t total,std::shared_ptr<ContentBackend> backend,std::stop_token stop){
    CheckStop(stop);(void)Url(policy,name);return std::make_unique<Stream>(policy,std::string(name),range,std::move(identity),total,std::move(backend),stop);}
std::string FetchContentText(const ResourceSourcePolicy& policy,std::string_view name,std::size_t maximum,std::shared_ptr<ContentBackend> backend,std::stop_token stop){
    CheckStop(stop);if(!backend||!maximum||maximum>8*1024*1024+2048)Failed();auto response=backend->Open(Url(policy,name),{},stop);
    if(response.status!=200||!response.body||!response.range.empty()||(!response.length.empty()&&Number(response.length)>maximum))Failed();
    std::array<char,65536> buffer{};std::string result;for(;;){CheckStop(stop);const auto n=response.body->Read(buffer,stop);if(n>buffer.size()||n>maximum-result.size())Failed();if(!n)break;result.append(buffer.data(),n);}
    if(!response.length.empty()&&Number(response.length)!=result.size())Failed();return result;
}
std::shared_ptr<ResourceTransport> HttpsResourceTransport(ResourceSourcePolicy policy,std::shared_ptr<ContentBackend> backend){
    if(policy.baseUrl.empty())return {};if(policy.baseUrl.size()>512||!policy.Enabled())Failed();return std::make_shared<ResourceHttps>(std::move(policy),backend?std::move(backend):WindowsContentBackend());}
}
