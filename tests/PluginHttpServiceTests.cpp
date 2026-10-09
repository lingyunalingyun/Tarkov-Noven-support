#include "plugins/PluginHttpService.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
using namespace noven::plugins;
void Check(bool value,const char* label){if(!value)throw std::runtime_error(label);}
struct Backend final:IPluginHttpBackend {
    std::vector<std::string> addresses{"93.184.216.34"};std::vector<HttpRequest> requests;std::deque<HttpResult> replies;bool wait{};
    std::vector<std::string> Resolve(std::string_view,std::stop_token,ipc::Deadline) override{return addresses;}
    HttpResult Exchange(const HttpRequest& input,const HttpUrl&,std::stop_token stop,ipc::Deadline deadline) override{
        while(wait&&!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline)Sleep(5);
        requests.push_back(input);if(replies.empty())return {input.requestId,HttpStatus::Ok,200,input.url,"result"};auto result=std::move(replies.front());replies.pop_front();return result;
    }
};
int wmain(int argc,wchar_t** argv) try {
    if(argc==2){
        const auto read=[&](const char* path){std::ifstream file(std::filesystem::path(argv[1])/path,std::ios::binary);Check(file.good(),"architecture source readable");return std::string(std::istreambuf_iterator<char>(file),{});};
        const auto windows=read("src/plugins/PluginHttpWindows.cpp");
        for(const auto required:{"WINHTTP_ACCESS_TYPE_NO_PROXY","WINHTTP_FLAG_ASYNC","WINHTTP_DISABLE_REDIRECTS","WINHTTP_DISABLE_COOKIES","WINHTTP_DISABLE_AUTHENTICATION","HANDLE_CLOSING","Noven Plugin HTTP/1"})Check(windows.find(required)!=windows.npos,"privacy/cancellation backend guard");
        for(const auto forbidden:{"SECURITY_FLAG_IGNORE","WINHTTP_OPTION_SECURITY_FLAGS","LocalSession","RecentScan","GetUserName","PluginStateStore","PluginStorageService"})Check(windows.find(forbidden)==windows.npos,"no insecure TLS or injected private data");
        const auto sdk=read("sdk/noven_plugin_abi_v1.h");const auto http=sdk.substr(sdk.find("#define NOVEN_HTTP_SCHEMA_VERSION"),sdk.find("#ifndef NOVEN_PLUGIN_OMIT_EXPORT_DECLARATIONS")-sdk.find("#define NOVEN_HTTP_SCHEMA_VERSION"));
        for(const auto forbidden:{"HWND","HANDLE","proxy","plugin_id","password","insecure","scan.trigger","filesystem.arbitrary"})Check(http.find(forbidden)==http.npos,"public HTTP API exposes no native/credential/game control");
        const auto cmake=read("CMakeLists.txt");const auto core=cmake.substr(cmake.find("target_link_libraries(NovenTarkovSupport PRIVATE"),cmake.find("#",cmake.find("target_link_libraries(NovenTarkovSupport PRIVATE"))-cmake.find("target_link_libraries(NovenTarkovSupport PRIVATE"));Check(core.find("NovenPluginHttp")==core.npos,"managed HTTP implementation linked only to Host");
    }
    const std::vector<std::string> grants{"https://api.example.com","https://example.org"};HttpRequest input{1,HttpMethod::Get,"https://api.example.com/items"};Backend backend;
    for(auto method:{HttpMethod::Get,HttpMethod::Head,HttpMethod::Post}){input.method=method;Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::Ok,"allowed methods");}
    Check(backend.requests[0].headers.empty()&&backend.requests[0].body.empty(),"no injected catalog/storage/scan/raid/account content");
    input.method=HttpMethod::Get;input.url="https://api.example.com/other";Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::Ok,"another path same origin");
    for(auto url:{"https://api.example.com.evil.test","https://api.example.com:8443","https://evil.example.com"}){input.url=url;Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::PermissionDenied,"exact origin enforcement");}
    for(auto address:{"127.0.0.1","0.0.0.0","10.1.2.3","172.16.1.1","192.168.1.1","169.254.169.254","100.64.0.1","224.0.0.1","255.255.255.255","::1","::","fc00::1","fe80::1","ff02::1","::ffff:127.0.0.1","2002:7f00:1::","2001:db8::1"})Check(!PublicHttpAddress(address),"nonpublic SSRF destination");
    Check(PublicHttpAddress("8.8.8.8")&&PublicHttpAddress("2606:4700:4700::1111"),"public address decision");
    input.url="https://api.example.com/items";backend.addresses.push_back("10.0.0.1");Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::BlockedDestination,"mixed public/private DNS rejected");backend.addresses={"93.184.216.34"};
    backend.replies.push_back({1,HttpStatus::Ok,302,"","",{{"location","https://example.org/final"}}});auto result=ExecuteHttp(input,grants,backend,{},ipc::After(1000));Check(result.status==HttpStatus::Ok&&result.finalUrl=="https://example.org/final","authorized redirect");
    for(auto destination:{"http://api.example.com","https://evil.example.com","https://localhost","//evil.example.com"}){backend.replies.push_back({1,HttpStatus::Ok,302,"","",{{"location",destination}}});Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::RedirectDenied,"redirect reauthorizes HTTPS/exact origin");}
    backend.replies.push_back({1,HttpStatus::Ok,302,"","",{{"location","/items"}}});Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::RedirectDenied,"redirect loop");
    input.headers={{"Authorization","Bearer explicit"}};backend.replies.push_back({1,HttpStatus::Ok,302,"","",{{"location","https://example.org/final"}}});ExecuteHttp(input,grants,backend,{},ipc::After(1000));Check(backend.requests.back().headers.empty(),"cross-origin strips explicit authorization");
    for(auto name:{"Host","Connection","Content-Length","Transfer-Encoding","Proxy-Authorization","Proxy-Connection","Cookie","Set-Cookie"}){input.headers={{name,"x"}};Check(!ValidHttpRequest(input),"transport/cookie headers rejected");}
    input.headers={{"X-Test","value\r\nInjected: x"}};Check(!ValidHttpRequest(input),"header injection");input.headers.clear();input.body.resize(MaximumHttpBody+1);input.method=HttpMethod::Post;Check(!ValidHttpRequest(input),"body cap");input.body.clear();input.method=static_cast<HttpMethod>(4);Check(!ValidHttpRequest(input),"unsupported method");input.method=HttpMethod::Get;
    input.headers={{"X-Test","one"},{"x-test","two"}};Check(!ValidHttpRequest(input),"case-insensitive duplicate header rejected");
    input.headers.clear();for(unsigned i=0;i<17;++i)input.headers.push_back({"X-"+std::to_string(i),"value"});Check(!ValidHttpRequest(input),"header count cap");
    input.headers={{"X-Test",std::string(1025,'x')}};Check(!ValidHttpRequest(input),"individual header cap");input.headers.clear();
    for(unsigned i=0;i<5;++i)input.headers.push_back({"X-"+std::to_string(i),std::string(1000,'x')});Check(!ValidHttpRequest(input),"aggregate header budget");input.headers.clear();
    input.method=HttpMethod::Post;input.body="explicit plugin content";backend.replies.push_back({1,HttpStatus::Ok,307,"","",{{"location","https://example.org/final"}}});Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::RedirectDenied,"POST body cannot silently cross origin");
    backend.replies.push_back({1,HttpStatus::Ok,200,"","ok"});Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::Ok&&backend.requests.back().body=="explicit plugin content","only explicit body is sent");input.method=HttpMethod::Get;input.body.clear();
    for(unsigned i=0;i<6;++i)backend.replies.push_back({1,HttpStatus::Ok,302,"","",{{"location","/hop-"+std::to_string(i)}}});Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::RedirectDenied,"five-hop cap");backend.replies.clear();
    backend.replies.push_back({1,HttpStatus::Ok,200,"",std::string(MaximumHttpBody+1,'x')});Check(ExecuteHttp(input,grants,backend,{},ipc::After(1000)).status==HttpStatus::TooLarge,"response cap");
    Check(ExecuteHttp(input,grants,backend,{},ipc::After(0)).status==HttpStatus::Timeout,"deadline");std::stop_source cancellation;cancellation.request_stop();Check(ExecuteHttp(input,grants,backend,cancellation.get_token(),ipc::After(1000)).status==HttpStatus::Cancelled,"cancel before execution");
    auto slow=std::make_unique<Backend>();slow->wait=true;PluginHttpService service(grants,std::move(slow));for(unsigned i=1;i<=8;++i){input.requestId=i;Check(service.Submit(input)==0,"eight bounded admissions");}Check(service.Submit(input)==-4,"duplicate scoped ID");input.requestId=9;Check(service.Submit(input)==-3,"outstanding cap");service.Stop();Check(!service.Pop()&&service.Submit(input)==-4,"stop discards late delivery");
    auto fast=std::make_unique<Backend>();PluginHttpService rate(grants,std::move(fast));for(unsigned i=1;i<=16;++i){input.requestId=i;Check(rate.Submit(input)==0,"minute budget");Check(WaitForSingleObject(rate.Wake(),1000)==WAIT_OBJECT_0&&rate.Pop()->requestId==i,"async completion ownership");}input.requestId=17;Check(rate.Submit(input)==-3,"sixteen per rolling minute");
    PluginHttpService foreign({"https://example.org"},std::make_unique<Backend>());Check(foreign.Submit(input)==-2,"other session cannot use origins");input.url="https://example.org";input.requestId=1;Check(foreign.Submit(input)==0,"request IDs scoped independently");
    std::cout<<"Managed HTTP offline policy/SSRF/redirect/limits/isolation PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
