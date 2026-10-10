#include "updates/UpdatePlanner.h"
#include "resources/ResourceFiles.h"
#include "plugins/PluginPipe.h"
#include <bcrypt.h>
#include <array>
namespace noven::updates {
std::string HashFileRange(const std::filesystem::path& path,std::uint64_t offset,std::uint64_t size){
    if(!resources::SafeResourcePath(path)||size>MaximumReleaseBytes||offset>MaximumReleaseBytes-size)throw std::runtime_error("unsafe update content");
    plugins::ipc::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER length{},position{};position.QuadPart=static_cast<LONGLONG>(offset);
    if(!file||!GetFileInformationByHandle(file.Get(),&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
        !GetFileSizeEx(file.Get(),&length)||length.QuadPart<0||offset+size>static_cast<std::uint64_t>(length.QuadPart)||!SetFilePointerEx(file.Get(),position,nullptr,FILE_BEGIN))throw std::runtime_error("unreadable update content");
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);throw std::runtime_error("SHA256 unavailable");}
    std::array<unsigned char,65536> buffer{};std::array<unsigned char,32> digest{};bool ok=true;
    while(size){const auto n=static_cast<DWORD>(std::min<std::uint64_t>(size,buffer.size()));DWORD read{};
        if(!ReadFile(file.Get(),buffer.data(),n,&read,nullptr)||read!=n||BCryptHashData(hash,buffer.data(),n,0)<0){ok=false;break;}size-=n;}
    ok=ok&&BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)throw std::runtime_error("update hash failed");return Hex(digest);
}
UpdatePlan PlanUpdate(std::string_view currentVersion,const std::filesystem::path& current,const std::filesystem::path& cache,const AuthenticatedRelease& authenticated){
    const auto& target=authenticated.Manifest();if(CompareReleaseVersions(target.version,currentVersion)<=0)throw std::runtime_error("update downgrade or same version denied");
    if(!resources::SafeResourcePath(current)||!resources::SafeResourcePath(cache))throw std::runtime_error("unsafe update roots");
    UpdatePlan plan;plan.version=target.version;plan.manifestIdentity=authenticated.Identity();
    const auto matches=[](const auto& path,std::uint64_t offset,std::uint64_t size,const auto& hash,bool exact){try{
        return (!exact||std::filesystem::file_size(path)==size)&&HashFileRange(path,offset,size)==hash;}catch(...){return false;}};
    for(const auto& f:target.files){PlannedFile file;file.target=f;plan.fullBytes+=f.size;const auto local=current/std::filesystem::path(f.path);
        file.unchanged=matches(local,0,f.size,f.sha256,true);if(file.unchanged)++plan.unchangedFiles;
        for(const auto& range:f.content){PlannedRange part{range,ContentSource::Download,{}};const auto object=cache/std::filesystem::path(range.sha256);
            if(file.unchanged||matches(local,range.fileOffset,range.size,range.sha256,false)){part.source=ContentSource::LocalFile;part.local=local;}
            else if(matches(object,0,range.size,range.sha256,true)){part.source=ContentSource::Cache;part.local=object;}
            if(part.source==ContentSource::Download)plan.downloadBytes+=range.size;else plan.reusedBytes+=range.size;file.ranges.push_back(std::move(part));}
        plan.files.push_back(std::move(file));}
    // 旧版本不动；预留完整新版本、待下载缓存以及一个最大文件的重建余量。
    // Retain the old version; reserve new content, missing cache and one largest reconstruction.
    std::uint64_t largest{};for(const auto& f:target.files)largest=std::max(largest,f.size);
    plan.requiredFreeBytes=plan.fullBytes+plan.downloadBytes+largest;return plan;
}
}
