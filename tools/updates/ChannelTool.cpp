#include "updates/UpdateHttpsSource.h"
#include "resources/ResourceFiles.h"
#include <array>
#include <iostream>
#include <fstream>
namespace {
using namespace noven;
void Check(bool value){if(!value)throw std::runtime_error("channel validation failed");}
std::string Ascii(std::wstring_view text){std::string result;for(auto c:text){Check(c<128);result+=static_cast<char>(c);}return result;}
std::vector<updates::ReleasePublicKey> Keys(const std::filesystem::path& path,std::string id){
    const auto bytes=resources::ReadResourceText(path,72);Check(bytes.size()==72);
    return {{std::move(id),std::vector<unsigned char>(bytes.begin(),bytes.end())}};
}
class Counting final:public updates::UpdateTransport {
    std::shared_ptr<updates::UpdateSource> source_;
    struct Stream final:resources::ResourceStream {
        std::unique_ptr<resources::ResourceStream> source;std::uint64_t* bytes;
        std::uint64_t Offset()const override{return source->Offset();}
        std::uint64_t TotalSize()const override{return source->TotalSize();}
        std::string Identity()const override{return source->Identity();}
        std::size_t Read(std::span<char> out,std::stop_token stop)override{auto n=source->Read(out,stop);*bytes+=n;return n;}
    };
public:
    std::uint64_t received{};
    explicit Counting(std::shared_ptr<updates::UpdateSource> source):source_(std::move(source)){}
    std::unique_ptr<resources::ResourceStream> Open(std::string_view pack,std::uint64_t offset,std::uint64_t size,std::string_view identity,std::stop_token stop)override{
        std::cout<<"HTTPS Range pack="<<pack<<" offset="<<offset<<" bytes="<<size<<std::endl;
        auto stream=std::make_unique<Stream>();stream->source=source_->Open(pack,offset,size,identity,stop);stream->bytes=&received;return stream;
    }
};
void VerifyLocal(const std::filesystem::path& artifacts,const std::filesystem::path& publicKey,std::string id){
    const auto release=updates::VerifyRelease(resources::ReadResourceText(artifacts/L"release.json",2*updates::MaximumManifestBytes+2048),Keys(publicKey,std::move(id)));
    std::uint64_t total{};
    for(const auto& file:release.Manifest().files){
        for(const auto& range:file.content)Check(updates::HashFileRange(artifacts/range.pack,range.packOffset,range.size)==range.sha256);
        total+=file.size;
    }
    std::cout<<"independent signature/pack verification PASS version="<<release.Manifest().version<<" bytes="<<total<<std::endl;
}
void VerifyRemote(const std::filesystem::path& artifacts,const std::filesystem::path& publicKey,std::string id,std::string root){
    VerifyLocal(artifacts,publicKey,id);
    const auto release=updates::VerifyRelease(resources::ReadResourceText(artifacts/L"release.json",2*updates::MaximumManifestBytes+2048),Keys(publicKey,std::move(id)));
    auto backend=resources::WindowsContentBackend(true);const resources::ResourceSourcePolicy policy{std::move(root)};
    std::array<char,65536> remote{},local{};std::uint64_t total{};
    for(const auto& file:release.Manifest().files)for(const auto& range:file.content){
        if(!range.size)continue;
        const auto pack=artifacts/range.pack;const auto packSize=std::filesystem::file_size(pack);
        std::ifstream input(pack,std::ios::binary);input.seekg(static_cast<std::streamoff>(range.packOffset));Check(static_cast<bool>(input));
        auto stream=resources::OpenContentRange(policy,range.pack,{range.packOffset,range.size},release.Identity(),packSize,backend,{});
        while(auto n=stream->Read(remote,{})){input.read(local.data(),static_cast<std::streamsize>(n));Check(input.gcount()==static_cast<std::streamsize>(n)&&std::equal(remote.begin(),remote.begin()+n,local.begin()));total+=n;}
        std::cout<<"verified remote pack range "<<range.pack<<" "<<range.packOffset<<" "<<range.size<<std::endl;
    }
    std::cout<<"all authenticated remote pack bytes match local verified artifacts; received="<<total<<std::endl;
}
}
int wmain(int argc,wchar_t** argv)try{
    if(argc==5&&std::wstring_view(argv[1])==L"--verify"){
        VerifyLocal(std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),Ascii(argv[4]));return 0;
    }
    if(argc==6&&std::wstring_view(argv[1])==L"--verify-remote"){
        VerifyRemote(std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),Ascii(argv[4]),Ascii(argv[5]));return 0;
    }
    if(argc!=7){std::cerr<<"--verify <artifacts> <public blob> <keyId> OR --plan/--interrupt/--resume/--lifecycle <isolated Program root> <isolated Test root> <approved HTTPS root> <public blob> <keyId>\n";return 2;}
    const auto mode=std::wstring_view(argv[1]);Check(mode==L"--plan"||mode==L"--interrupt"||mode==L"--resume"||mode==L"--lifecycle");
    const auto program=std::filesystem::absolute(argv[2]);auto keys=Keys(std::filesystem::absolute(argv[5]),Ascii(argv[6]));
    const auto paths=common::AppPaths::Test(program/L"versions"/L"0.1.0",std::filesystem::absolute(argv[3]));
    updates::VersionStore store(program,"0.1.0",keys);Check(store.Validate("0.1.0"));
    auto source=updates::HttpsUpdateSource({Ascii(argv[4])});Check(static_cast<bool>(source));
    const auto release=updates::VerifyRelease(source->FetchManifest({}),keys);Check(release.Manifest().version=="0.1.1");
    const auto plan=updates::PlanUpdate("0.1.0",paths.programRoot,paths.UpdateCache(),release);
    std::cout<<"verified remote version="<<release.Manifest().version<<" full="<<plan.fullBytes<<" reused="<<plan.reusedBytes<<" planned="<<plan.downloadBytes<<std::endl;
    if(mode==L"--plan")return 0;
    updates::UpdateEngine engine(program,paths.UpdateCache());Counting transport(source);std::stop_source cancel;
    if(mode!=L"--lifecycle"){
        try{engine.Stage(release,plan,transport,cancel.get_token(),[&](updates::UpdateProgress p){if(mode==L"--interrupt"&&p.downloaded>=65536)cancel.request_stop();});}
        catch(const std::exception& e){
            std::cout<<"actual body bytes="<<transport.received<<" result="<<e.what()<<std::endl;
            if(mode==L"--interrupt"&&cancel.stop_requested()){Check(store.Validate("0.1.0"));return 3;}throw;
        }
        Check(engine.ValidateVersion(release)&&store.Validate("0.1.0"));
        std::cout<<"remote stage PASS actual body bytes="<<transport.received<<" savedPercent="<<100.0*(1.0-static_cast<double>(transport.received)/static_cast<double>(plan.fullBytes))<<std::endl;return 0;
    }
    // 仅显式操作员隔离目录；这是核心状态验证，不声称 Launcher/UI 人工验收。
    // Explicit isolated operator roots only; this does not claim Launcher/UI manual acceptance.
    Check(engine.ValidateVersion(release));
    const auto marker=paths.Data()/L"channel-review-preservation.txt";
    Check(resources::WriteResourceText(marker,"channel review user state"));
    store.Activate(release);auto boot=store.BeginLaunch();store.ConfirmHealthy(boot.active,boot.token);Check(store.Read().active=="0.1.1");
    store.Rollback();Check(store.Read().active=="0.1.0");
    store.Activate(release);boot=store.BeginLaunch();store.ConfirmHealthy(boot.active,boot.token);
    Check(store.Read().active=="0.1.1"&&resources::ReadResourceText(marker,128)=="channel review user state");
    std::cout<<"activation/rollback/reactivation/user-data core PASS; UI/manual installation acceptance remains separate\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
