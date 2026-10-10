#include "updates/UpdateLocalSource.h"
#include "resources/ResourceFiles.h"
#include <iostream>
#include <windows.h>
using namespace noven;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("offline executable review assertion");}
class Counting final:public updates::UpdateTransport {
    std::shared_ptr<updates::UpdateSource> source_;
    struct Stream final:resources::ResourceStream {
        std::unique_ptr<resources::ResourceStream> source;std::uint64_t* bytes;
        std::uint64_t Offset()const override{return source->Offset();}std::uint64_t TotalSize()const override{return source->TotalSize();}
        std::string Identity()const override{return source->Identity();}
        std::size_t Read(std::span<char> out,std::stop_token stop)override{const auto n=source->Read(out,stop);*bytes+=n;return n;}
    };
public:
    std::uint64_t downloaded{};
    explicit Counting(std::filesystem::path fixture):source_(updates::LocalUpdateSource(fixture)){}
    std::unique_ptr<resources::ResourceStream> Open(std::string_view pack,std::uint64_t offset,std::uint64_t size,std::string_view identity,std::stop_token stop)override{
        auto stream=std::make_unique<Stream>();stream->source=source_->Open(pack,offset,size,identity,stop);stream->bytes=&downloaded;return stream;}
};
}
int wmain(int argc,wchar_t** argv)try{
    Check(argc==3);const auto fixture=std::filesystem::absolute(argv[1]);
    const auto blob=resources::ReadResourceText(std::filesystem::absolute(argv[2]),72);Check(blob.size()==72);
    const std::vector keys{updates::ReleasePublicKey{"review-only-p256",std::vector<unsigned char>(blob.begin(),blob.end())}};
    const auto release=updates::VerifyRelease(resources::ReadResourceText(fixture/L"update-review-fixture"/L"release.json",2*updates::MaximumManifestBytes+2048),keys);
    Check(release.Manifest().version=="0.1.1");
    const auto root=std::filesystem::temp_directory_path()/("noven-update-review-"+std::to_string(GetCurrentProcessId()));Check(!std::filesystem::exists(root));
    const auto program=root/"program",old=program/"versions"/"0.1.0";Check(resources::ResourceDirectories(old));
    const auto source=fixture/L"versions"/L"0.1.0";
    for(const auto& entry:std::filesystem::recursive_directory_iterator(source)){
        Check(resources::SafeResourcePath(entry.path()));const auto destination=old/entry.path().lexically_relative(source);
        if(entry.is_directory())Check(resources::ResourceDirectories(destination));else Check(std::filesystem::copy_file(entry.path(),destination));}
    const auto paths=common::AppPaths::Test(old,root/"data");
    Check(resources::WriteResourceText(paths.Data()/L"settings.json","review user state"));
    updates::VersionStore store(program,"0.1.0",keys);store.SeedInitial();
    const auto plan=updates::PlanUpdate("0.1.0",old,paths.UpdateCache(),release);Counting transport(fixture/L"update-review-fixture");
    updates::UpdateEngine engine(program,paths.UpdateCache());const auto installed=engine.Stage(release,plan,transport);
    Check(transport.downloaded<=plan.downloadBytes&&transport.downloaded<plan.fullBytes&&plan.reusedBytes>0&&engine.ValidateVersion(release));
    Check(!std::filesystem::exists(installed/"synthetic-obsolete.txt")&&std::filesystem::exists(old/"synthetic-obsolete.txt"));
    Check(std::filesystem::exists(installed/"synthetic-new.txt"));
    store.Activate(release);const auto boot=store.BeginLaunch();store.ConfirmHealthy(boot.active,boot.token);Check(store.Read().active=="0.1.1");
    store.Rollback();Check(store.Read().active=="0.1.0"&&resources::ReadResourceText(paths.Data()/L"settings.json",64)=="review user state");
    std::cout<<"REAL offline signed payload verified, not user manual acceptance; full="<<plan.fullBytes<<" reused="<<plan.reusedBytes<<" planned="<<plan.downloadBytes
        <<" actualDownloaded="<<transport.downloaded<<" savedPercent="<<100.0*(1.0-static_cast<double>(transport.downloaded)/static_cast<double>(plan.fullBytes))<<'\n';
    Check(resources::RemoveResourceTree(root.parent_path(),root));return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
