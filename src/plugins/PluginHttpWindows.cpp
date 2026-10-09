#include <winsock2.h>
#include <ws2tcpip.h>
#include "plugins/PluginHttpService.h"
#include <winhttp.h>
#include <atomic>
#include <array>

namespace noven::plugins {
namespace {
struct Internet final {HINTERNET value{};~Internet(){if(value)WinHttpCloseHandle(value);}};
std::wstring Wide(std::string_view value){return {value.begin(),value.end()};}
// 关闭异步请求后仍可能回调，引用保留到 HANDLE_CLOSING，禁止栈上借用或同步跨线程关闭。
// Async close can still callback; retain context until HANDLE_CLOSING, never borrow stack buffers or cross-close synchronous calls.
struct Completion final {
    std::atomic<unsigned> references{1};ipc::Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::atomic<DWORD> status{},error{},bytes{};
    std::array<char,4096> buffer{};std::string body;
    void Release(){if(references.fetch_sub(1)==1)delete this;}
    static void CALLBACK Callback(HINTERNET,DWORD_PTR context,DWORD status,void* info,DWORD size){
        auto* self=reinterpret_cast<Completion*>(context);if(!self)return;
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){self->Release();return;}
        if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR){if(size>=sizeof(WINHTTP_ASYNC_RESULT))self->error=static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;else self->error=ERROR_INVALID_DATA;}
        else if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE)self->bytes=size;
        else if(status!=WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE&&status!=WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)return;
        self->status=status;SetEvent(self->event.Get());
    }
    bool Wait(DWORD expected,std::stop_token stop,ipc::Deadline deadline){
        while(!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline){if(WaitForSingleObject(event.Get(),50)==WAIT_OBJECT_0)return status==expected&&!error;}
        error=stop.stop_requested()?ERROR_OPERATION_ABORTED:ERROR_WINHTTP_TIMEOUT;return false;
    }
    void Prepare(){status=error=bytes=0;ResetEvent(event.Get());}
};
struct Request final {HINTERNET value{};Completion* completion{};~Request(){if(value)WinHttpCloseHandle(value);if(completion)completion->Release();}};
struct WindowsHttp final:IPluginHttpBackend {
    WSADATA winsock{};bool started{WSAStartup(MAKEWORD(2,2),&winsock)==0};
    ~WindowsHttp(){if(started)WSACleanup();}
    std::vector<std::string> Resolve(std::string_view host,std::stop_token stop,ipc::Deadline deadline) override {
        std::vector<std::string> addresses;if(!started||stop.stop_requested()||std::chrono::steady_clock::now()>=deadline)return addresses;
        ADDRINFOEXW hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;PADDRINFOEXW result{};
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        timeval timeout{};timeout.tv_sec=static_cast<long>((std::min)(remaining,5000LL)/1000);timeout.tv_usec=static_cast<long>((std::min)(remaining,5000LL)%1000*1000);
        if(GetAddrInfoExW(Wide(host).c_str(),nullptr,NS_DNS,nullptr,&hints,&result,&timeout,nullptr,nullptr,nullptr)!=0)return addresses;
        struct Free final {PADDRINFOEXW value;~Free(){FreeAddrInfoExW(value);}} free{result};
        for(auto* item=result;item;item=item->ai_next){char text[INET6_ADDRSTRLEN]{};const void* address=nullptr;
            if(item->ai_family==AF_INET&&item->ai_addrlen>=sizeof(sockaddr_in))address=&reinterpret_cast<sockaddr_in*>(item->ai_addr)->sin_addr;
            if(item->ai_family==AF_INET6&&item->ai_addrlen>=sizeof(sockaddr_in6))address=&reinterpret_cast<sockaddr_in6*>(item->ai_addr)->sin6_addr;
            if(!address||!InetNtopA(item->ai_family,const_cast<void*>(address),text,sizeof(text))||addresses.size()==64)return {};
            addresses.emplace_back(text);
        }return addresses;
    }
    HttpResult Exchange(const HttpRequest& input,const HttpUrl& url,std::stop_token stop,ipc::Deadline deadline) override {
        HttpResult result{input.requestId,HttpStatus::TransportError};
        const auto full=Wide(url.origin+url.target);URL_COMPONENTS parsed{};parsed.dwStructSize=sizeof(parsed);parsed.dwHostNameLength=static_cast<DWORD>(-1);
        if(!WinHttpCrackUrl(full.c_str(),static_cast<DWORD>(full.size()),0,&parsed)||parsed.nScheme!=INTERNET_SCHEME_HTTPS||parsed.nPort!=url.port||std::wstring_view(parsed.lpszHostName,parsed.dwHostNameLength)!=Wide(url.host))return result;
        // 不使用系统代理/PAC，不注入账户凭据/Cookie；新请求保留默认 TLS 证书验证。
        // No system proxy/PAC, implicit account credentials or cookies; retain default TLS certificate validation.
        Internet session{WinHttpOpen(L"Noven Plugin HTTP/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC)};
        if(!session.value||!WinHttpSetTimeouts(session.value,5000,5000,5000,5000))return result;
        Internet connection{WinHttpConnect(session.value,Wide(url.host).c_str(),static_cast<INTERNET_PORT>(url.port),0)};if(!connection.value)return result;
        const wchar_t* method=input.method==HttpMethod::Get?L"GET":input.method==HttpMethod::Head?L"HEAD":L"POST";
        Request request{WinHttpOpenRequest(connection.value,method,Wide(url.target).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE),nullptr};
        if(!request.value)return result;
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION,auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH,maximum=16*1024;
        if(!WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled))||!WinHttpSetOption(request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof(auth))||!WinHttpSetOption(request.value,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&maximum,sizeof(maximum)))return result;
        auto* completion=new Completion;request.completion=completion;if(!completion->event)return result;completion->body=input.body;
        if(WinHttpSetStatusCallback(request.value,&Completion::Callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)return result;
        DWORD_PTR context=reinterpret_cast<DWORD_PTR>(completion);++completion->references;
        if(!WinHttpSetOption(request.value,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context))){completion->Release();return result;}
        std::wstring headers;for(const auto& header:input.headers)headers+=Wide(header.name)+L": "+Wide(header.value)+L"\r\n";
        const auto failure=[&]{result.status=completion->error==ERROR_WINHTTP_TIMEOUT?HttpStatus::Timeout:stop.stop_requested()?HttpStatus::Cancelled:HttpStatus::TransportError;return result;};
        completion->Prepare();
        if(!WinHttpSendRequest(request.value,headers.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:headers.c_str(),static_cast<DWORD>(headers.size()),completion->body.empty()?WINHTTP_NO_REQUEST_DATA:completion->body.data(),static_cast<DWORD>(completion->body.size()),static_cast<DWORD>(completion->body.size()),context)||!completion->Wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,stop,deadline))return failure();
        completion->Prepare();if(!WinHttpReceiveResponse(request.value,nullptr)||!completion->Wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,stop,deadline))return failure();
        DWORD status{},size=sizeof(status);if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))return result;result.httpStatus=status;
        for(const auto& [name,query]:std::array<std::pair<const char*,DWORD>,4>{{{"content-type",WINHTTP_QUERY_CONTENT_TYPE},{"location",WINHTTP_QUERY_LOCATION},{"etag",WINHTTP_QUERY_ETAG},{"last-modified",WINHTTP_QUERY_LAST_MODIFIED}}}){
            wchar_t buffer[1025]{};size=sizeof(buffer);if(WinHttpQueryHeaders(request.value,query,WINHTTP_HEADER_NAME_BY_INDEX,buffer,&size,WINHTTP_NO_HEADER_INDEX)){
                std::string value;for(auto c:std::wstring_view(buffer)){if(c<32||c>126)return result;value+=static_cast<char>(c);}result.headers.push_back({name,std::move(value)});
            }else if(GetLastError()!=ERROR_WINHTTP_HEADER_NOT_FOUND){result.status=HttpStatus::TooLarge;return result;}
        }
        if(input.method==HttpMethod::Head){result.status=HttpStatus::Ok;return result;}
        for(;;){completion->Prepare();if(!WinHttpReadData(request.value,completion->buffer.data(),static_cast<DWORD>(completion->buffer.size()),nullptr)||!completion->Wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,stop,deadline))return failure();
            const auto bytes=completion->bytes.load();if(!bytes){result.status=HttpStatus::Ok;return result;}if(result.body.size()+bytes>MaximumHttpBody){result.status=HttpStatus::TooLarge;result.body.clear();return result;}result.body.append(completion->buffer.data(),bytes);
        }
    }
};
}
std::unique_ptr<IPluginHttpBackend> MakePluginHttpBackend(){return std::make_unique<WindowsHttp>();}
}
