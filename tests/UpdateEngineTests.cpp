#include "UpdateTestSigning.h"
#include "updates/VersionStore.h"
#include "resources/ResourceFiles.h"
#include "common/AppPaths.h"
#include <iostream>
#include <cstring>
using namespace noven::updates;
namespace {
void Check(bool v){if(!v)throw std::runtime_error("update engine assertion");}
template<class F>void Reject(F f){bool failed{};try{f();}catch(...){failed=true;}Check(failed);}
class Stream final:public noven::resources::ResourceStream {
public:
    std::string bytes,id;std::uint64_t offset{},total{};std::size_t cursor{};bool wrong{},truncate{};
    std::uint64_t Offset()const override{return offset+(wrong?1:0);}
    std::uint64_t TotalSize()const override{return total;}
    std::string Identity()const override{return id;}
    std::size_t Read(std::span<char> out,std::stop_token stop)override{if(stop.stop_requested())throw std::runtime_error("cancelled");
        const auto n=std::min({out.size(),std::size_t(65536),bytes.size()-cursor});if(truncate&&cursor)return 0;
        memcpy(out.data(),bytes.data()+cursor,n);cursor+=n;return n;}
};
class Transport final:public UpdateTransport {
public:
    std::string pack;std::vector<std::pair<std::uint64_t,std::uint64_t>> requests;bool wrong{},truncate{},corrupt{},fail{};
    std::unique_ptr<noven::resources::ResourceStream> Open(std::string_view name,std::uint64_t offset,std::uint64_t count,std::string_view identity,std::stop_token)override{
        Check(name=="core.pack"&&offset+count<=pack.size());requests.emplace_back(offset,count);if(fail)throw std::runtime_error("offline simulated network failure");
        auto s=std::make_unique<Stream>();s->bytes=pack.substr(static_cast<std::size_t>(offset),static_cast<std::size_t>(count));s->id=identity;s->offset=offset;s->total=pack.size();s->wrong=wrong;s->truncate=truncate;
        if(corrupt&&!s->bytes.empty())s->bytes[0]^=1;return s;}
};
ReleaseFile Add(const std::filesystem::path& root,std::string name,const std::string& bytes,Transport& t){
    Check(noven::resources::WriteResourceText(root/name,bytes));ReleaseFile f{name,noven::resources::ResourceHash(root/name),bytes.size(),{}};
    const auto step=f.size<ChunkThreshold?f.size:ChunkBytes;for(std::uint64_t offset=0;offset<f.size;offset+=step){const auto size=std::min(step,f.size-offset);
        f.content.push_back({offset,size,t.pack.size(),HashFileRange(root/name,offset,size),"core.pack"});t.pack+=bytes.substr(static_cast<std::size_t>(offset),static_cast<std::size_t>(size));}return f;
}
}
int main()try {
    Check(UpdateCacheFits(0,0,MaximumUpdateCacheBytes,MaximumUpdateCacheEntries-2));
    Check(!UpdateCacheFits(MaximumUpdateCacheBytes,0,1,0)&&!UpdateCacheFits(0,MaximumUpdateCacheEntries,0,0));
    Check(!UpdateCacheFits(UINT64_MAX,0,0,0)&&!UpdateCacheFits(0,0,UINT64_MAX,SIZE_MAX));
    const auto root=std::filesystem::temp_directory_path()/("noven-update-engine-"+std::to_string(GetCurrentProcessId()));
    const auto program=root/"program",old=program/"versions"/"0.1.0",target=root/"target";
    const auto paths=noven::common::AppPaths::Test(program,root/"test");const auto cache=paths.UpdateCache();
    const std::vector<std::string> sentinels={"plugins/a/manifest.json","data/plugin-state.json","data/settings.json","data/recent-scans.json","data/raid-history.json","data/plugin-storage/a/value","logs/keep.log","resources/maps/another/keep.png"};
    for(const auto& p:sentinels)Check(noven::resources::WriteResourceText(paths.userRoot/p,"preserve"));
    Check(noven::resources::WriteResourceText(old/"NovenTarkovSupport.exe","baseline app"));Check(noven::resources::WriteResourceText(old/"NovenPluginHost.exe","baseline host"));
    Check(noven::resources::WriteResourceText(program/"NovenTarkovSupport.exe","legacy recovery app"));
    Check(noven::resources::WriteResourceText(program/"NovenPluginHost.exe","legacy recovery host"));
    Check(noven::resources::WriteResourceText(old/"unchanged.txt","unchanged"));Check(noven::resources::WriteResourceText(old/"obsolete.txt","obsolete"));
    auto large=std::string(static_cast<std::size_t>(3*ChunkBytes),'a');large[static_cast<std::size_t>(ChunkBytes)]='b';Check(noven::resources::WriteResourceText(old/"large.bin",large));
    noven::tests::UpdateSigner signer;const std::array keys{signer.publicKey};VersionStore store(program,"0.1.0",{signer.publicKey});store.SeedInitial();Check(store.Validate("0.1.0")&&store.Read().active=="0.1.0");
    Transport transport;ReleaseManifest manifest{"0.1.1","2026-10-10T00:00:00Z","offline",{}};
    manifest.files.push_back(Add(target,"NovenTarkovSupport.exe","new app",transport));manifest.files.push_back(Add(target,"NovenPluginHost.exe","baseline host",transport));manifest.files.push_back(Add(target,"unchanged.txt","unchanged",transport));
    manifest.files.push_back(Add(target,"large.bin",std::string(static_cast<std::size_t>(3*ChunkBytes),'a'),transport));manifest.files.push_back(Add(target,"new.txt","new",transport));
    const auto release=VerifyRelease(signer.Sign(EncodeReleaseManifest(manifest)),keys);const auto plan=PlanUpdate("0.1.0",old,cache,release);UpdateEngine engine(program,cache);
    Reject([&]{engine.Stage(release,plan,transport,{}, {},[]{return 0ULL;});});Check(transport.requests.empty());
    transport.fail=true;Reject([&]{engine.Stage(release,plan,transport);});transport.fail=false;Check(store.Read().active=="0.1.0");
    transport.wrong=true;Reject([&]{engine.Stage(release,plan,transport);});Check(store.Read().active=="0.1.0");transport.wrong=false;
    transport.corrupt=true;Reject([&]{engine.Stage(release,plan,transport);});transport.corrupt=false;
    const auto partial=cache/"partial"/release.Identity()/ (manifest.files[3].content[1].sha256+".part");
    transport.truncate=true;Reject([&]{engine.Stage(release,plan,transport);});transport.truncate=false;
    Check(store.Read().active=="0.1.0"&&std::filesystem::file_size(partial)==65536);Check(std::filesystem::remove(partial));
    std::stop_source stop;Reject([&]{engine.Stage(release,plan,transport,stop.get_token(),[&](auto progress){if(progress.downloaded>=65536)stop.request_stop();});});
    Check(store.Read().active=="0.1.0"&&!std::filesystem::exists(program/"versions"/"0.1.1"));
    Check(std::filesystem::file_size(partial)==65536);
    const auto prior=transport.requests.size();UpdateEngine restarted(program,cache);const auto installed=restarted.Stage(release,plan,transport);
    Check(installed==program/"versions"/"0.1.1"&&restarted.ValidateVersion(release)&&transport.requests.size()>prior);
    Check(transport.requests[prior].first==manifest.files[3].content[1].packOffset+65536);
    Check(!std::filesystem::exists(installed/"obsolete.txt")&&std::filesystem::exists(old/"obsolete.txt"));Check(store.Read().active=="0.1.0");
    const auto savedEnvelope=program/"releases"/"0.1.1.json";Check(noven::resources::WriteResourceText(savedEnvelope,"tampered signed manifest"));
    Reject([&]{store.Activate(release);});Check(store.Read().active=="0.1.0");Check(noven::resources::WriteResourceText(savedEnvelope,release.Envelope()));
    auto badWhole=manifest;badWhole.version="0.1.2";badWhole.files[3].sha256=std::string(64,'0');
    const auto invalid=VerifyRelease(signer.Sign(EncodeReleaseManifest(badWhole)),keys);const auto invalidPlan=PlanUpdate("0.1.0",old,cache,invalid);
    Reject([&]{engine.Stage(invalid,invalidPlan,transport);});Check(store.Read().active=="0.1.0"&&!std::filesystem::exists(program/"versions"/"0.1.2"));
    store.Activate(release);Check(store.Read().active=="0.1.1"&&store.Read().pending);auto launch=store.BeginLaunch();Check(launch.attempts==1);
    Reject([&]{store.ConfirmHealthy("0.1.1","wrong");});store.ConfirmHealthy("0.1.1",launch.token);Check(!store.Read().pending&&store.Read().previous=="0.1.0");
    store.Rollback();Check(store.Read().active=="0.1.0");store.Activate(release);auto unconfirmed=store.BeginLaunch();store.DeferUnconfirmedBoot(unconfirmed.active,unconfirmed.token);
    Check(store.BeginLaunch().active=="0.1.1");Check(store.BeginLaunch().active=="0.1.0");
    Check(noven::resources::WriteResourceText(program/"current.json","corrupt"));Check(store.BeginLaunch().active=="0.1.0");
    Reject([&]{store.Resolve("../test/user");});Check(noven::resources::WriteResourceText(program/"current.json","{\"schemaVersion\":1,\"active\":\"../../test/user\",\"previous\":\"\",\"pending\":false,\"attempts\":0,\"token\":\"\"}"));Check(store.BeginLaunch().active=="0.1.0");
    for(const auto& p:sentinels)Check(noven::resources::ReadResourceText(paths.userRoot/p,64)=="preserve");Check(store.Validate("0.1.0"));
    Check(noven::resources::ReadResourceText(program/"NovenTarkovSupport.exe",64)=="legacy recovery app");
    Check(noven::resources::ReadResourceText(program/"NovenPluginHost.exe",64)=="legacy recovery host");
    store.RemoveInstalledVersions();Check(!std::filesystem::exists(program/"versions")&&!std::filesystem::exists(program/"current.json"));
    for(const auto& p:sentinels)Check(noven::resources::ReadResourceText(paths.userRoot/p,64)=="preserve");
    std::cout<<"offline range/resume/atomic activation/health/rollback/user-data PASS; full="<<plan.fullBytes<<" reused="<<plan.reusedBytes<<" missing="<<plan.downloadBytes<<'\n';
    Check(noven::resources::RemoveResourceTree(root.parent_path(),root));return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
