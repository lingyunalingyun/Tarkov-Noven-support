#include "updates/VersionStore.h"
#include "updates/UpdateTrust.h"
#include "common/AppPaths.h"
#include "resources/ResourceFiles.h"
#include "plugins/PluginPipe.h"
#include "ui/MessageDialog.h"
#include <shellapi.h>
#include <charconv>
namespace {
std::string Ascii(std::wstring_view value){std::string text;for(auto c:value){if(c>127)throw std::runtime_error("non-ASCII updater argument");text+=static_cast<char>(c);}return text;}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)try {
    const auto root=noven::common::ProgramDirectory();noven::updates::VersionStore versions(root,noven::updates::InstallerVersion(),noven::updates::CompiledReleaseKeys());
    int count{};LPWSTR* raw=CommandLineToArgvW(GetCommandLineW(),&count);if(!raw)return 2;
    std::vector<std::wstring> args;for(int i=1;i<count;++i)args.emplace_back(raw[i]);LocalFree(raw);
    if(args.size()==1&&args[0]==L"--seed"){versions.SeedInitial();return 0;}
    if(args.size()!=2&&args.size()!=4)return 2;
    const bool rollback=args[0]==L"--rollback";if(!rollback&&args[0]!=L"--apply")return 2;
    const auto version=Ascii(args[1]);if(!noven::updates::ValidReleaseVersion(version))return 2;
    if(args.size()==4){if(args[2]!=L"--wait-pid")return 2;const auto pidText=Ascii(args[3]);DWORD pid{};
        const auto [end,error]=std::from_chars(pidText.data(),pidText.data()+pidText.size(),pid);if(error!=std::errc{}||end!=pidText.data()+pidText.size()||!pid)return 2;
        noven::plugins::ipc::Handle app(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(app){wchar_t path[32768]{};DWORD n=32768;
            const auto expected=versions.Resolve(versions.Read().active)/L"NovenTarkovSupport.exe";
            if(!QueryFullProcessImageNameW(app.Get(),0,path,&n)||CompareStringOrdinal(path,-1,expected.c_str(),-1,TRUE)!=CSTR_EQUAL||WaitForSingleObject(app.Get(),30000)!=WAIT_OBJECT_0)return 3;}}
    noven::plugins::ipc::Handle ownership(CreateMutexW(nullptr,FALSE,L"Local\\NovenTarkovSupport.Updater.70C934D2"));if(!ownership||GetLastError()==ERROR_ALREADY_EXISTS)return 3;
    if(rollback){if(versions.Read().previous!=version)return 2;versions.Rollback();}
    else{const auto text=noven::resources::ReadResourceText(root/L"releases"/(version+".json"),2*noven::updates::MaximumManifestBytes+2048);
        const auto release=noven::updates::VerifyRelease(text,noven::updates::CompiledReleaseKeys());if(release.Manifest().version!=version)return 2;versions.Activate(release);}
    const auto launcher=root/L"NovenLauncher.exe";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(launcher.c_str(),nullptr,nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&startup,&process))return 4;CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
}catch(...){(void)noven::ui::ShowMessageDialog(nullptr,L"Noven Updater",L"更新未激活；原版本与用户数据保持不变。\nUpdate not activated; previous version and user data remain intact.",noven::ui::MessageKind::Error,L"OK");return 1;}
