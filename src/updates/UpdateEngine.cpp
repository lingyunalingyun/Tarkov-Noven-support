#include "updates/UpdateEngine.h"
#include "resources/ResourceFiles.h"
#include "plugins/PluginPipe.h"
#include <array>
namespace noven::updates {
namespace {
using plugins::ipc::Handle;
[[noreturn]]void Fail(){throw std::runtime_error("update staging failed; old version unchanged");}
void Cancel(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("update interrupted; partial content retained");}
bool ApplicationEntries(const ReleaseManifest& manifest){bool main{},host{};for(const auto& file:manifest.files){
    main|=file.path=="NovenTarkovSupport.exe";host|=file.path=="NovenPluginHost.exe";}return main&&host;}
bool ExactInventory(const std::filesystem::path& root,std::size_t expected){std::size_t files{},entries{};
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root)){
        if(++entries>65536||!resources::SafeResourcePath(entry.path()))return false;if(entry.is_regular_file()&&++files>expected)return false;}
    return files==expected;
}
Handle Open(const std::filesystem::path& path,DWORD access,DWORD disposition){
    if(!resources::SafeResourcePath(path))Fail();Handle file(CreateFileW(path.c_str(),access,FILE_SHARE_READ,nullptr,disposition,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};if(!file||!GetFileInformationByHandle(file.Get(),&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))Fail();return file;
}
void Position(HANDLE file,std::uint64_t offset){LARGE_INTEGER n{};n.QuadPart=static_cast<LONGLONG>(offset);if(!SetFilePointerEx(file,n,nullptr,FILE_BEGIN))Fail();}
void Write(HANDLE file,const char* p,DWORD n){DWORD written{};if(!WriteFile(file,p,n,&written,nullptr)||written!=n)Fail();}
bool Matches(const std::filesystem::path& path,std::uint64_t size,const std::string& hash){try{return std::filesystem::file_size(path)==size&&HashFileRange(path,0,size)==hash;}catch(...){return false;}}
void CopyRange(const std::filesystem::path& source,std::uint64_t offset,std::uint64_t size,HANDLE target,std::stop_token stop){
    auto file=Open(source,GENERIC_READ,OPEN_EXISTING);Position(file.Get(),offset);std::array<char,65536> buffer{};
    while(size){Cancel(stop);const auto n=static_cast<DWORD>(std::min<std::uint64_t>(size,buffer.size()));DWORD read{};
        if(!ReadFile(file.Get(),buffer.data(),n,&read,nullptr)||read!=n)Fail();Write(target,buffer.data(),n);size-=n;}
}
}
UpdateEngine::UpdateEngine(std::filesystem::path program,std::filesystem::path cache):program_(std::move(program)),cache_(std::move(cache)){
    if(!resources::SafeResourcePath(program_)||!resources::SafeResourcePath(cache_))Fail();
}
bool UpdateEngine::ValidateVersion(const AuthenticatedRelease& target) const try {
    const auto root=program_/L"versions"/target.Manifest().version;if(!resources::SafeResourcePath(root))return false;
    if(!ApplicationEntries(target.Manifest())||!ExactInventory(root,target.Manifest().files.size()))return false;
    for(const auto& f:target.Manifest().files)if(!Matches(root/f.path,f.size,f.sha256))return false;return true;
}catch(...){return false;}
std::filesystem::path UpdateEngine::Stage(const AuthenticatedRelease& release,const UpdatePlan& plan,UpdateTransport& transport,
    std::stop_token stop,std::function<void(UpdateProgress)> progress,std::function<std::uint64_t()> space){
    if(!ApplicationEntries(release.Manifest())||plan.manifestIdentity!=release.Identity()||plan.version!=release.Manifest().version||plan.files.size()!=release.Manifest().files.size())Fail();
    // 调用方的计划只是性能提示；目标字节和路径始终来自已验证清单。
    // The caller's plan is only a performance hint; authenticated manifest defines all target paths/content.
    for(std::size_t i=0;i<plan.files.size();++i){const auto& f=release.Manifest().files[i];const auto& p=plan.files[i];
        if(p.target.path!=f.path||p.target.sha256!=f.sha256||p.target.size!=f.size||p.ranges.size()!=f.content.size())Fail();
        for(std::size_t j=0;j<f.content.size();++j){const auto& a=f.content[j];const auto& b=p.ranges[j].content;
            if(a.sha256!=b.sha256||a.pack!=b.pack||a.size!=b.size||a.packOffset!=b.packOffset||a.fileOffset!=b.fileOffset)Fail();}}
    if(!resources::ResourceDirectories(program_/L"versions")||!resources::ResourceDirectories(cache_))Fail();
    std::uint64_t cachedBytes{},incomingBytes{};std::size_t entries{},incomingEntries{};
    for(const auto& entry:std::filesystem::recursive_directory_iterator(cache_)){
        if(++entries>MaximumUpdateCacheEntries||!resources::SafeResourcePath(entry.path()))Fail();
        if(entry.is_regular_file()){const auto size=entry.file_size();if(size>MaximumUpdateCacheBytes-cachedBytes)Fail();cachedBytes+=size;}}
    for(const auto& file:plan.files)for(const auto& range:file.ranges)if(range.source==ContentSource::Download){incomingBytes+=range.content.size;incomingEntries+=2;}
    if(!UpdateCacheFits(cachedBytes,entries,incomingBytes,incomingEntries))
        throw std::runtime_error("update content cache quota exceeded; old version unchanged");
    if(!space)space=[this]{ULARGE_INTEGER available{};if(!GetDiskFreeSpaceExW(program_.c_str(),&available,nullptr,nullptr))Fail();return available.QuadPart;};
    if(space()<plan.requiredFreeBytes)throw std::runtime_error("insufficient update disk space");
    const auto versions=program_/L"versions",stage=versions/(".stage-"+release.Identity()),final=versions/release.Manifest().version;
    if(std::filesystem::exists(final)){if(ValidateVersion(release))return final;Fail();}
    if(std::filesystem::exists(stage)&&!resources::RemoveResourceTree(versions,stage))Fail();if(!resources::ResourceDirectories(stage))Fail();
    UpdateProgress state{0,plan.downloadBytes,{},false};std::array<char,65536> buffer{};
    try{
        for(const auto& p:plan.files)for(const auto& part:p.ranges){Cancel(stop);if(part.source!=ContentSource::Download)continue;
            const auto& range=part.content;const auto object=cache_/range.sha256;if(Matches(object,range.size,range.sha256))continue;
            const auto partialRoot=cache_/L"partial"/release.Identity();if(!resources::ResourceDirectories(partialRoot))Fail();const auto partial=partialRoot/(range.sha256+".part");
            std::uint64_t received{};if(std::filesystem::exists(partial)){if(!resources::SafeResourcePath(partial))Fail();received=std::filesystem::file_size(partial);if(received>range.size){if(!DeleteFileW(partial.c_str()))Fail();received=0;}}
            auto output=Open(partial,GENERIC_WRITE,OPEN_ALWAYS);Position(output.Get(),received);
            if(received<range.size){auto stream=transport.Open(range.pack,range.packOffset+received,range.size-received,release.Identity(),stop);
                if(!stream||stream->Offset()!=range.packOffset+received||stream->TotalSize()<range.packOffset+range.size||stream->Identity()!=release.Identity())Fail();
                while(received<range.size){Cancel(stop);const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),range.size-received));
                    const auto read=stream->Read(std::span(buffer.data(),n),stop);if(!read||read>n)Fail();Write(output.Get(),buffer.data(),static_cast<DWORD>(read));received+=read;state.downloaded+=read;state.file=p.target.path;if(progress)progress(state);}
                char extra{};if(stream->Read(std::span(&extra,1),stop))Fail();}
            if(!FlushFileBuffers(output.Get()))Fail();output.Reset();Cancel(stop);
            if(!Matches(partial,range.size,range.sha256)){if(resources::SafeResourcePath(partial))DeleteFileW(partial.c_str());throw std::runtime_error("update content integrity mismatch");}
            if(!resources::SafeResourcePath(object)||!MoveFileExW(partial.c_str(),object.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Fail();
        }
        state.constructing=true;
        for(const auto& p:plan.files){Cancel(stop);state.file=p.target.path;if(progress)progress(state);const auto path=stage/p.target.path;
            if(!resources::ResourceDirectories(path.parent_path()))Fail();auto output=Open(path,GENERIC_WRITE,CREATE_NEW);
            for(const auto& part:p.ranges){const bool local=part.source==ContentSource::LocalFile;const auto source=local?part.local:cache_/part.content.sha256;
                CopyRange(source,local?part.content.fileOffset:0,part.content.size,output.Get(),stop);}
            if(!FlushFileBuffers(output.Get()))Fail();output.Reset();if(!Matches(path,p.target.size,p.target.sha256))Fail();}
        Cancel(stop);if(!resources::WriteResourceText(program_/L"releases"/(plan.version+".json"),release.Envelope()))Fail();
        if(!resources::SafeResourcePath(final)||!MoveFileExW(stage.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH))Fail();return final;
    }catch(...){resources::RemoveResourceTree(versions,stage);throw;}
}
}
