#include "plugins/PluginPipe.h"
#include "plugins/PluginNativeHost.h"
#include <shellapi.h>
#include <exception>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)return 2;
    int result=2;
    try{
        const auto parsed=noven::plugins::ipc::ParseHostArguments(count,arguments);LocalFree(arguments);arguments=nullptr;
        wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);if(!length||length>=32768)return 2;
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
        noven::plugins::NativePluginHost plugin(std::filesystem::path(executable).parent_path());
        result=noven::plugins::ipc::RunHost(parsed,{
            [&](const auto& message,auto& channel,HANDLE parent){return plugin.Message(message,channel,parent);},
            [&]{plugin.Shutdown();}});
    }
    catch(const std::exception&){result=2;}
    if(arguments)LocalFree(arguments);return result;
}
