#include "plugins/PluginPipe.h"
#include "plugins/PluginNativeHost.h"
#include "common/AppPaths.h"
#include "common/RuntimeOwnership.h"
#include <shellapi.h>
#include <exception>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    noven::common::RuntimeOwnership ownership(true);if(!ownership.handle)return 2;
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)return 2;
    int result=2;
    try{
        const auto parsed=noven::plugins::ipc::ParseHostArguments(count,arguments);LocalFree(arguments);arguments=nullptr;
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
        noven::plugins::NativePluginHost plugin(noven::common::AppPaths::Current());
        result=noven::plugins::ipc::RunHost(parsed,{
            [&](const auto& message,auto& channel,HANDLE parent){return plugin.Message(message,channel,parent);},
            [&]{plugin.Shutdown();},[&]{return plugin.Wake();},[&]{return plugin.Pump();}});
    }
    catch(const std::exception&){result=2;}
    if(arguments)LocalFree(arguments);return result;
}
