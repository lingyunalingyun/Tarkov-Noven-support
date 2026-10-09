#include "plugins/PluginNativeHost.h"
#include <shellapi.h>
using namespace noven::plugins;
// 独立第一方测试可执行文件；生产 Host 没有切换后端的参数/环境入口。
// Separate first-party test executable; production Host has no backend-switch argument/environment entry.
struct OfflineBackend final:IPluginHttpBackend {
    std::vector<std::string> Resolve(std::string_view,std::stop_token,ipc::Deadline) override{return {"93.184.216.34"};}
    HttpResult Exchange(const HttpRequest& request,const HttpUrl&,std::stop_token stop,ipc::Deadline deadline) override {
        while(request.url.ends_with("/pending")&&!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline)Sleep(5);
        return {request.requestId,HttpStatus::Ok,request.method==HttpMethod::Post?201u:200u,request.url,request.method==HttpMethod::Head?"":"offline result",{{"content-type","text/plain"}}};
    }
};
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int count{};auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)return 2;
    try{
        const auto parsed=ipc::ParseHostArguments(count,arguments);LocalFree(arguments);arguments=nullptr;
        wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768))return 2;
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
        NativePluginHost plugin(std::filesystem::path(executable).parent_path(),std::make_unique<OfflineBackend>());
        return ipc::RunHost(parsed,{[&](const auto& message,auto& channel,HANDLE parent){return plugin.Message(message,channel,parent);},[&]{plugin.Shutdown();},[&]{return plugin.Wake();},[&]{return plugin.Pump();}});
    }catch(...){if(arguments)LocalFree(arguments);return 2;}
}
