#include "plugins/MarketplaceService.h"
#include "plugins/PluginHttpOrigin.h"
#include "plugins/PluginPipe.h"
#include <winhttp.h>
#include <atomic>
#include <array>
namespace noven::plugins {
bool ValidRegistryEndpoint(std::string_view url){
    const auto parsed=ParseHttpUrl(url);
    if(!parsed||parsed->host!="raw.githubusercontent.com"||parsed->port!=443||parsed->target.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/")!=std::string::npos
        ||parsed->target.find("..")!=std::string::npos||!parsed->target.ends_with("/registry.json"))return false;
    return std::count(parsed->target.begin(),parsed->target.end(),'/')>=4&&parsed->target.find("//")==std::string::npos;
}
namespace {
struct Internet {HINTERNET value{};~Internet(){if(value)WinHttpCloseHandle(value);}};
std::wstring Wide(std::string_view text){return {text.begin(),text.end()};}
struct Completion {
    std::atomic<unsigned> references{1};ipc::Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::atomic<DWORD> status{},error{},bytes{};std::array<char,8192> buffer{};
    void Release(){if(references.fetch_sub(1)==1)delete this;}
    static void CALLBACK Callback(HINTERNET,DWORD_PTR context,DWORD status,void* info,DWORD size){
        auto* self=reinterpret_cast<Completion*>(context);if(!self)return;
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){self->Release();return;}
        if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)self->error=size>=sizeof(WINHTTP_ASYNC_RESULT)?static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError:ERROR_INVALID_DATA;
        else if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE)self->bytes=size;
        else if(status!=WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE&&status!=WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)return;
        self->status=status;SetEvent(self->event.Get());
    }
    void Prepare(){status=error=bytes=0;ResetEvent(event.Get());}
    bool Wait(DWORD expected,std::stop_token stop,ipc::Deadline deadline){
        while(!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline)if(WaitForSingleObject(event.Get(),50)==WAIT_OBJECT_0)return status==expected&&!error;
        return false;
    }
};
// 取消后回调仍可到达，缓冲区保留到 HANDLE_CLOSING；UI 只接收不可变快照。
// Callbacks can arrive after cancellation; buffers survive until HANDLE_CLOSING, UI receives only immutable snapshots.
struct Request {HINTERNET value{};Completion* completion{};~Request(){if(value)WinHttpCloseHandle(value);if(completion)completion->Release();}};
struct RegistryHttp final:IRegistryTransport {
    explicit RegistryHttp(std::string value):url(std::move(value)){}
    std::string url;
    std::string Fetch(std::stop_token stop) override {
        const auto parsed=ParseHttpUrl(url);if(!ValidRegistryEndpoint(url)||!parsed)throw std::runtime_error("registry endpoint");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        Internet session{WinHttpOpen(L"Noven Registry/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC)};
        if(!session.value||!WinHttpSetTimeouts(session.value,5000,5000,5000,5000))throw std::runtime_error("registry session");
        Internet connection{WinHttpConnect(session.value,Wide(parsed->host).c_str(),443,0)};if(!connection.value)throw std::runtime_error("registry connect");
        Request request{WinHttpOpenRequest(connection.value,L"GET",Wide(parsed->target).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE),nullptr};
        if(!request.value)throw std::runtime_error("registry request");
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION,auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH,headers=16*1024;
        if(!WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled))||!WinHttpSetOption(request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof(auth))
            ||!WinHttpSetOption(request.value,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&headers,sizeof(headers)))throw std::runtime_error("registry policy");
        auto* completion=new Completion;request.completion=completion;if(!completion->event)throw std::runtime_error("registry event");
        if(WinHttpSetStatusCallback(request.value,&Completion::Callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)throw std::runtime_error("registry callback");
        DWORD_PTR context=reinterpret_cast<DWORD_PTR>(completion);++completion->references;
        if(!WinHttpSetOption(request.value,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context))){completion->Release();throw std::runtime_error("registry context");}
        completion->Prepare();
        if(!WinHttpSendRequest(request.value,L"Accept: application/json\r\n",static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,context)
            ||!completion->Wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,stop,deadline))throw std::runtime_error("registry send");
        completion->Prepare();if(!WinHttpReceiveResponse(request.value,nullptr)||!completion->Wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,stop,deadline))throw std::runtime_error("registry response");
        DWORD status{},size=sizeof(status);if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200)throw std::runtime_error("registry status");
        std::string text;
        for(;;){completion->Prepare();if(!WinHttpReadData(request.value,completion->buffer.data(),static_cast<DWORD>(completion->buffer.size()),nullptr)
            ||!completion->Wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,stop,deadline))throw std::runtime_error("registry read");
            const auto bytes=completion->bytes.load();if(!bytes)return text;if(bytes>MaximumRegistryBytes-text.size())throw std::runtime_error("registry size");text.append(completion->buffer.data(),bytes);
        }
    }
};
}
std::unique_ptr<IRegistryTransport> MakeRegistryTransport(std::string url){if(url.empty())return {};if(!ValidRegistryEndpoint(url))throw std::runtime_error("invalid first-party registry endpoint");return std::make_unique<RegistryHttp>(std::move(url));}
}
