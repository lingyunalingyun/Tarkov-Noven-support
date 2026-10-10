#include "updates/VersionStore.h"
#include "updates/UpdateTrust.h"
#include "common/AppPaths.h"
#include "plugins/PluginPipe.h"
#include "ui/MessageDialog.h"
#include <windows.h>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)try {
    noven::plugins::ipc::Handle ownership(CreateMutexW(nullptr,FALSE,L"Local\\NovenTarkovSupport.Launcher.70C934D2"));
    if(!ownership||GetLastError()==ERROR_ALREADY_EXISTS)return 0;
    const auto root=noven::common::ProgramDirectory();noven::updates::VersionStore versions(root,noven::updates::InstallerVersion(),noven::updates::CompiledReleaseKeys());
    const auto active=versions.BeginLaunch();const auto versionRoot=versions.Resolve(active.active),exe=versionRoot/L"NovenTarkovSupport.exe";
    std::wstring command=L"\""+exe.wstring()+L"\"";if(active.pending)command+=L" --noven-boot-token "+std::wstring(active.token.begin(),active.token.end());
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,versionRoot.c_str(),&startup,&process)){
        if(active.pending){versions.Rollback();ownership.Reset();const auto launcher=root/L"NovenLauncher.exe";STARTUPINFOW next{sizeof(next)};PROCESS_INFORMATION retry{};
            if(CreateProcessW(launcher.c_str(),nullptr,nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&next,&retry)){CloseHandle(retry.hThread);CloseHandle(retry.hProcess);}}
        return 1;}
    CloseHandle(process.hThread);noven::plugins::ipc::Handle app(process.hProcess);if(!active.pending)return 0;
    // 有界监控启动健康，不杀进程；正常退出不被当作崩溃回退。
    // Bounded boot-health observation, never kill the process or mistake normal exit for a crash.
    const auto deadline=GetTickCount64()+30000;
    while(GetTickCount64()<deadline){if(WaitForSingleObject(app.Get(),250)==WAIT_OBJECT_0){DWORD exit{};GetExitCodeProcess(app.Get(),&exit);
            if(exit==0)versions.DeferUnconfirmedBoot(active.active,active.token);return static_cast<int>(exit);}
        const auto state=versions.Read();if(state.active==active.active&&!state.pending)return 0;}return 0;
}catch(...){(void)noven::ui::ShowMessageDialog(nullptr,L"Noven Launcher",L"无法验证已安装版本。请使用原安装器修复。\nCannot validate installed version. Repair using the installer.",noven::ui::MessageKind::Error,L"OK");return 1;}
