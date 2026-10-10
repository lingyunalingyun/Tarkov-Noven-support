#include "common/AppPaths.h"
#include <windows.h>
#include <shlobj.h>
#include <stdexcept>
namespace noven::common {
namespace {
std::filesystem::path Absolute(std::filesystem::path path){
    if(!path.is_absolute())throw std::runtime_error("application root must be absolute");return path.lexically_normal();
}
bool Within(const std::filesystem::path& a,const std::filesystem::path& b){
    auto x=a.begin(),y=b.begin();for(;y!=b.end();++y,++x){if(x==a.end()||CompareStringOrdinal(x->c_str(),-1,y->c_str(),-1,TRUE)!=CSTR_EQUAL)return false;}return true;
}
AppPaths Separate(PathMode mode,std::filesystem::path program,std::filesystem::path user){
    program=Absolute(std::move(program));user=Absolute(std::move(user));
    if(Within(program,user)||Within(user,program))throw std::runtime_error("program and user roots overlap");return {mode,std::move(program),std::move(user)};
}
}
AppPaths AppPaths::Development(std::filesystem::path program){program=Absolute(std::move(program));return {PathMode::Development,program,program};}
AppPaths AppPaths::Installed(std::filesystem::path program,std::filesystem::path local){return Separate(PathMode::Installed,std::move(program),Absolute(std::move(local))/L"Noven Tarkov Support");}
AppPaths AppPaths::Test(std::filesystem::path program,std::filesystem::path root){return Separate(PathMode::Test,std::move(program),Absolute(std::move(root))/L"user");}
AppPaths AppPaths::ResourceReview(std::filesystem::path program){
    program=Absolute(std::move(program));
    const auto root=program.parent_path()/(program.filename().wstring()+L".resource-review-data");
    return Test(std::move(program),root);
}
AppPaths AppPaths::Resolve(std::filesystem::path program,const std::function<std::filesystem::path()>& folder){
    program=Absolute(std::move(program));
    const auto marker=program/L"noven-installed.layout";
    const DWORD attributes=GetFileAttributesW(marker.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("cannot inspect installed layout");return Development(program);}
    if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("unsafe installed layout marker");
    return Installed(program,folder());
}
std::filesystem::path ProgramDirectory(){
    std::wstring path(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!length||length>=path.size())throw std::runtime_error("cannot resolve program root");path.resize(length);return std::filesystem::path(path).parent_path();
}
std::filesystem::path LocalAppDataDirectory(){
    PWSTR folder{};const auto result=SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&folder);
    if(FAILED(result)||!folder){CoTaskMemFree(folder);throw std::runtime_error("LocalAppData known folder unavailable");}
    std::filesystem::path path(folder);CoTaskMemFree(folder);return Absolute(path);
}
AppPaths AppPaths::Current(){return Resolve(ProgramDirectory(),LocalAppDataDirectory);}
std::filesystem::path AppPaths::BootstrapRoot() const {
    if(programRoot.parent_path().filename()!=L"versions")return {};
    return programRoot.parent_path().parent_path();
}
}
