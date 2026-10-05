#include "plugins/PluginPipe.h"
#include <shellapi.h>
#include <exception>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)return 2;
    int result=2;
    try{const auto parsed=noven::plugins::ipc::ParseHostArguments(count,arguments);LocalFree(arguments);arguments=nullptr;result=noven::plugins::ipc::RunHost(parsed);}
    catch(const std::exception&){result=2;}
    if(arguments)LocalFree(arguments);return result;
}
