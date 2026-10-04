#include "events/EventHttp.h"
#include "events/WikiEventSource.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace noven::events {
namespace {
struct Handle {
    HINTERNET value{};
    ~Handle(){if(value)WinHttpCloseHandle(value);}
};
std::string Header(HINTERNET request,DWORD header) {
    DWORD bytes{};WinHttpQueryHeaders(request,header,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&bytes,WINHTTP_NO_HEADER_INDEX);
    if(bytes==0 || bytes>4096)return {};
    std::wstring value(bytes/sizeof(wchar_t),L'\0');
    if(!WinHttpQueryHeaders(request,header,WINHTTP_HEADER_NAME_BY_INDEX,value.data(),&bytes,WINHTTP_NO_HEADER_INDEX))return {};
    value.resize(bytes/sizeof(wchar_t));while(!value.empty() && value.back()==L'\0')value.pop_back();
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string out(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),size,nullptr,nullptr);return out;
}
std::wstring Wide(std::string_view text) {
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),n);return result;
}
}
HttpResponse WinHttpEventClient::Get(std::wstring_view host,std::wstring_view path,std::string_view etag,std::string_view modified) {
    HttpResponse result;
    // 固定公开来源白名单，不允许公告中的 URL 驱动任意网络请求。
    // Fixed public-source allowlist; announcement URLs never drive arbitrary network requests.
    const bool wiki=host==L"escapefromtarkov.fandom.com" && path==kWikiEventPath;
    if((host!=L"t.me" && host!=L"changes.tarkov-changes.com" && !wiki) || path.empty() || path.front()!=L'/'
        || etag.size()>1024 || modified.size()>128 || etag.find_first_of("\r\n")!=etag.npos
        || modified.find_first_of("\r\n")!=modified.npos) {result.error="invalid event request";return result;}
    Handle session{WinHttpOpen(L"NovenTarkovSupport/0.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.value){result.error="event HTTP session failed";return result;}
    WinHttpSetTimeouts(session.value,5000,5000,5000,5000);
    Handle connection{WinHttpConnect(session.value,std::wstring(host).c_str(),INTERNET_DEFAULT_HTTPS_PORT,0)};
    if(!connection.value){result.error="event HTTP connection failed";return result;}
    Handle request{WinHttpOpenRequest(connection.value,L"GET",std::wstring(path).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
    if(!request.value){result.error="event HTTP request failed";return result;}
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))){result.error="event redirect policy failed";return result;}
    std::wstring headers=wiki?L"Accept: application/json\r\n":L"Accept: text/html\r\n";
    if(!etag.empty())headers+=L"If-None-Match: "+Wide(etag)+L"\r\n";
    if(!modified.empty())headers+=L"If-Modified-Since: "+Wide(modified)+L"\r\n";
    if(!WinHttpSendRequest(request.value,headers.c_str(),static_cast<DWORD>(headers.size()),WINHTTP_NO_REQUEST_DATA,0,0,0)
        || !WinHttpReceiveResponse(request.value,nullptr)){result.error="event HTTP fetch failed";return result;}
    DWORD bytes=sizeof(result.status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&result.status,&bytes,WINHTTP_NO_HEADER_INDEX)){
        result.error="event HTTP status missing";return result;
    }
    result.contentType=Header(request.value,WINHTTP_QUERY_CONTENT_TYPE);result.etag=Header(request.value,WINHTTP_QUERY_ETAG);
    result.lastModified=Header(request.value,WINHTTP_QUERY_LAST_MODIFIED);
    if(result.status==304)return result;
    if(result.status!=200){result.error="event HTTP status "+std::to_string(result.status);return result;}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    char buffer[16384];DWORD read{};
    while(true) {
        if(std::chrono::steady_clock::now()>deadline){result.error="event response deadline exceeded";break;}
        if(!WinHttpReadData(request.value,buffer,sizeof(buffer),&read)){result.error="event response read failed";break;}
        if(!read)break;
        if(result.body.size()+read>kMaximumResponseBytes){result.error="event response exceeds capacity";break;}
        result.body.append(buffer,read);
    }
    if(!result.error.empty())result.body.clear();return result;
}
bool ValidateHtml(const HttpResponse& response,std::string& error) {
    error.clear();auto type=response.contentType;
    std::ranges::transform(type,type.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(!response.error.empty()){error=response.error;return false;}
    if(response.status!=200 || (type!="text/html" && !type.starts_with("text/html;"))
        || response.body.empty() || response.body.size()>kMaximumResponseBytes
        || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,response.body.data(),static_cast<int>(response.body.size()),nullptr,0)) {
        error="invalid event HTML response";return false;
    }
    for(auto marker:{"cf-chl-","challenge-platform","<title>Just a moment","<title>Login","g-recaptcha"})
        if(response.body.find(marker)!=response.body.npos){error="event source interstitial rejected";return false;}
    return true;
}
}
