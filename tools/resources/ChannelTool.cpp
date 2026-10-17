#include "resources/ResourceOnline.h"
#include "resources/ResourceFiles.h"
#include "ui/MapPage.h"
#include <wincodec.h>
#include <array>
#include <iostream>
#include <source_location>
#include <fstream>
#include <algorithm>
using namespace noven::resources;
namespace {
void Check(bool b,const std::source_location at=std::source_location::current()){if(!b)throw std::runtime_error("resource channel check failed at line "+std::to_string(at.line()));}
std::string Text(std::wstring_view s){std::string out;for(auto c:s){Check(c<128);out+=static_cast<char>(c);}return out;}
std::vector<ResourceMapLabel> Labels(){
    const std::string_view assets=NOVEN_RESOURCE_TEST_ASSETS;const auto root=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(assets.data()),assets.size()));
    noven::data::MapCatalog catalog;std::wstring error;Check(catalog.Load(root/L"data",error));std::vector<ResourceMapLabel> result;
    for(const auto& map:catalog.Maps())result.push_back({map.id,map.nameZh,map.nameEn});return result;
}
std::vector<std::string> MapIds(){std::vector<std::string> ids;for(const auto& label:Labels())ids.push_back(label.stableMapId);return ids;}
class Counting final:public ContentBackend {
    std::shared_ptr<ContentBackend> inner_=WindowsContentBackend(true,true);
    bool cutNext_{};
    struct Body final:ContentBody {Counting& owner;std::unique_ptr<ContentBody> inner;std::chrono::steady_clock::time_point returned{};bool cut{};std::uint64_t read{},offset{};
        Body(Counting& o,std::unique_ptr<ContentBody> b,bool reset=false,std::uint64_t start=0):owner(o),inner(std::move(b)),cut(reset),offset(start){}
        std::size_t Read(std::span<char> out,std::stop_token stop)override{
            const auto begin=std::chrono::steady_clock::now();if(returned.time_since_epoch().count())owner.consumerMs+=std::chrono::duration_cast<std::chrono::milliseconds>(begin-returned).count();
            try{if(cut&&read>=65536){cut=false;inner.reset();++owner.cuts;std::cout<<"operator-injected HTTPS connection close offset="<<offset+read<<std::endl;throw ContentNetworkError("injected connection interruption",12030);}
                const auto n=inner->Read(out,stop);returned=std::chrono::steady_clock::now();owner.readMs+=std::chrono::duration_cast<std::chrono::milliseconds>(returned-begin).count();owner.bytes+=n;read+=n;return n;}
            catch(const std::exception& e){owner.readMs+=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();std::cerr<<"native body failure after actualBodyBytes="<<owner.bytes.load()<<" readMs="<<owner.readMs.load()<<" consumerMs="<<owner.consumerMs.load()<<" "<<e.what()<<'\n';throw;}}};
public:
    explicit Counting(bool cut=false):cutNext_(cut){}
    std::atomic_uint64_t bytes{},readMs{},consumerMs{},readRetries{},failedOpens{},cuts{};
    void ReadRetry(ContentRange range,unsigned retry,unsigned code)override{
        ++readRetries;std::cout<<"retry="<<retry<<" offset="<<range.offset<<" count="<<range.count<<" Win32="<<code<<std::endl;
    }
    ContentResponse Open(std::string_view url,std::optional<ContentRange> range,std::stop_token stop)override{
        const auto begin=std::chrono::steady_clock::now();const auto end=url.find('?');std::cout<<"request "<<url.substr(0,end);if(range)std::cout<<" offset="<<range->offset<<" count="<<range->count;std::cout<<std::endl;
        ContentResponse result;
        try{result=inner_->Open(url,range,stop);}catch(const std::exception& e){++failedOpens;std::cerr<<"native transport failure "<<e.what()<<'\n';throw;}
        std::cout<<"HTTPS "<<url.substr(0,end)<<" status="<<result.status<<" range="<<result.range<<" length="<<result.length<<" rateLimited="<<result.rateLimited<<" headersMs="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count()<<'\n';
        const bool cut=cutNext_&&result.status==206;if(cut)cutNext_=false;
        if(result.body)result.body=std::make_unique<Body>(*this,std::move(result.body),cut,range?range->offset:0);return result;
    }
};
void Probe(const std::string& root){auto backend=std::make_shared<Counting>();auto adapter=GithubReleaseBackend(root,backend);const ResourceSourcePolicy policy{root};
    for(const auto range:{ContentRange{0,32},ContentRange{12345,ContentRangeBytes},ContentRange{65536,65536}}){
        auto stream=OpenContentRange(policy,"synthetic-range-v1.nvr",range,"probe",6291456,adapter,{});std::array<char,65536> buffer{};std::uint64_t at=range.offset;
        while(const auto n=stream->Read(buffer,{})){for(std::size_t i=0;i<n;++i)Check(static_cast<unsigned char>(buffer[i])==((at+i)*31+17)%251);at+=n;}Check(at==range.offset+range.count);
    }std::cout<<"native GitHub Releases Range PASS actualBytes="<<backend->bytes.load()<<'\n';
}
void Decode(const std::filesystem::path& root){
    const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Check(SUCCEEDED(hr));IWICImagingFactory* factory{};
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))));bool decoded{};
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.path().extension()==L".png"){
        IWICBitmapDecoder* decoder{};IWICBitmapFrameDecode* frame{};UINT w{},h{};
        Check(SUCCEEDED(factory->CreateDecoderFromFilename(entry.path().c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)));
        Check(SUCCEEDED(decoder->GetFrame(0,&frame))&&SUCCEEDED(frame->GetSize(&w,&h))&&w&&h);frame->Release();decoder->Release();decoded=true;break;
    }factory->Release();CoUninitialize();Check(decoded);
}
void RuntimeMap(ResourceService& service,bool installed,std::string_view mapId){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
    {
    const std::string_view assets=NOVEN_RESOURCE_TEST_ASSETS;
    const auto directory=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(assets.data()),assets.size()));
    noven::ui::MapPage page;std::wstring error;Check(page.Initialize(directory,error,true));
    page.SelectMap(mapId);Check(page.MapId()==mapId);
    page.SetResourceResolver([&](auto id){return service.ResolveMap(id);},[](auto){return noven::ui::MapPage::ResourceInfo{};},{});
    page.ResourcesChanged();Check(page.MissingResource()!=installed);
    for(const auto mode:{noven::data::GameMode::Pvp,noven::data::GameMode::Pve}) {
        if(page.Mode()!=mode)Check(page.SetMode(mode));
        Check(page.MapId()==mapId&&page.Information());
        for(const auto& floor:page.Information()->floors){page.SelectFloor(floor.id);Check(page.FloorId()==floor.id&&page.MissingResource()!=installed);}
    }
    page.ResourceActive(false);
    }CoUninitialize();
}
void CatalogReady(ResourceService& service){
    const auto state=service.Remote();Check(state==RemoteAvailability::Online||state==RemoteAvailability::Cached);
    std::cout<<"authenticated catalog="<<(state==RemoteAvailability::Online?"fresh HTTPS":"reverified cached; metadata fetch unavailable")<<'\n';
}
// 显式离线操作员检查使用同一服务/解包器；不进入生产配置或安装器。
// Explicit local operator validation uses the same service/extractor, never production configuration.
class LocalPackageTransport final:public ResourceTransport {
    std::filesystem::path root_;
    struct Stream final:ResourceStream {
        ResourceRecord record;std::uint64_t offset;std::ifstream input;
        Stream(const std::filesystem::path& path,ResourceRecord r,std::uint64_t start):record(std::move(r)),offset(start),input(path,std::ios::binary){Check(input.good());input.seekg(static_cast<std::streamoff>(start));}
        std::uint64_t Offset()const override{return offset;}
        std::uint64_t TotalSize()const override{return record.downloadSize;}
        std::string Identity()const override{return record.sha256;}
        std::size_t Read(std::span<char> out,std::stop_token stop)override{if(stop.stop_requested())return 0;input.read(out.data(),static_cast<std::streamsize>(out.size()));return static_cast<std::size_t>(input.gcount());}
    };
public:
    explicit LocalPackageTransport(std::filesystem::path root):root_(std::move(root)){}
    std::unique_ptr<ResourceStream> Open(const ResourceRecord& r,std::uint64_t offset,std::stop_token)override{return std::make_unique<Stream>(root_/r.artifact,r,offset);}
};
void LocalPackage(const std::filesystem::path& catalog,const std::filesystem::path& test){
    const auto manifest=ParseResourceManifest(ReadResourceText(catalog,MaximumManifestBytes),MapIds());Check(manifest.components.size()==1);
    const auto& record=manifest.components.front();const auto paths=noven::common::AppPaths::Test(test/L"program",test/L"data");
    {
    ResourceService service(paths,manifest,std::make_shared<LocalPackageTransport>(catalog.parent_path()));
    Check(service.Act(record.resourceId,ResourceAction::Download));Check(service.WaitIdle(std::chrono::seconds(30)));
    RuntimeMap(service,true,record.stableMapId);auto lease=service.ResolveMap(record.stableMapId);Check(lease&&VerifyPackage(*lease,record));Decode(*lease);
    const auto installed=*lease;lease.reset();
    for(const auto& entry:std::filesystem::recursive_directory_iterator(installed))if(entry.path().extension()==L".png"){
        std::ofstream corrupt(entry.path(),std::ios::binary|std::ios::trunc);corrupt<<"corrupt test copy";Check(corrupt.good());break;}
    Check(service.Act(record.resourceId,ResourceAction::Verify));Check(service.WaitIdle(std::chrono::seconds(30)));Check(!service.ResolveMap(record.stableMapId));
    Check(service.Act(record.resourceId,ResourceAction::Repair));Check(service.WaitIdle(std::chrono::seconds(30)));Check(static_cast<bool>(service.ResolveMap(record.stableMapId)));RuntimeMap(service,true,record.stableMapId);
    }
    ResourceService offline(paths,{}, {},{}, {},Labels());Check(offline.WaitIdle(std::chrono::seconds(30)));RuntimeMap(offline,true,record.stableMapId);
    Check(offline.Act(record.resourceId,ResourceAction::Delete));Check(offline.WaitIdle(std::chrono::seconds(30)));RuntimeMap(offline,false,record.stableMapId);
    std::cout<<"local package/PvP+PvE floors/native PNG/corrupt verify/repair/offline restart/delete PASS map="<<record.stableMapId<<'\n';
}
void Flow(bool interrupt,bool cut,const std::filesystem::path& program,const std::filesystem::path& test,ResourceRemotePolicy policy,bool production,std::string mapId){
    const std::string resourceId="maps."+mapId;
    const auto paths=noven::common::AppPaths::Test(program,test);const auto sentinel=paths.Data()/"channel-preservation.txt";
    if(!std::filesystem::exists(sentinel))Check(WriteResourceText(sentinel,"keep user data"));
    auto backend=std::make_shared<Counting>(cut);ResourceService* pointer{};bool paused{};
    {
        ResourceService service(paths,{}, {},{},[&]{if(interrupt&&pointer&&!paused){for(const auto& row:pointer->Snapshot())if(row.received>=65536&&row.state==ResourceState::Downloading){paused=true;Check(pointer->Act(row.record.resourceId,ResourceAction::Pause));}}},Labels());pointer=&service;
        RefreshRemoteResources(service,paths,policy,MapIds(),{},backend,!production);CatalogReady(service);
        const auto selected=[&]{auto rows=service.Snapshot();auto it=std::ranges::find_if(rows,[&](const auto& row){return row.record.resourceId==resourceId;});Check(it!=rows.end());return *it;};
        auto row=selected();Check(row.record.downloadSize>0);if(!production)Check(row.record.downloadSize>ContentRangeBytes);const auto record=row.record;
        if(row.state!=ResourceState::Installed)Check(service.Act(record.resourceId,row.state==ResourceState::Paused?ResourceAction::Resume:ResourceAction::Download));Check(service.WaitIdle(std::chrono::seconds(180)));
        row=selected();if(row.state==ResourceState::Error)std::cerr<<"resource state error="<<row.error<<'\n';if(interrupt){Check(paused&&row.state==ResourceState::Paused&&row.received>=65536);std::cout<<"interrupted partialBytes="<<row.received<<" actualBodyBytes="<<backend->bytes.load()<<'\n';return;}
        Check(row.state==ResourceState::Installed);RuntimeMap(service,true,mapId);auto lease=service.ResolveMap(mapId);Check(lease&&VerifyPackage(*lease,record));Decode(*lease);
        Check(!service.Act(record.resourceId,ResourceAction::Delete));lease.reset();Check(service.Act(record.resourceId,ResourceAction::Verify));Check(service.WaitIdle(std::chrono::seconds(30)));Check(selected().state==ResourceState::Installed);
        Check(service.Act(record.resourceId,ResourceAction::Repair));Check(service.WaitIdle(std::chrono::seconds(30)));Check(selected().state==ResourceState::Installed);
        std::cout<<"remote install/PNG decode/verify/in-use delete guard PASS actualBodyBytes="<<backend->bytes.load()<<'\n';
        const auto before=backend->bytes.load();Check(service.Act(resourceId,ResourceAction::Delete));Check(service.WaitIdle(std::chrono::seconds(30)));RuntimeMap(service,false,mapId);
        Check(selected().received==0&&service.ClearDownloadCache());Check(service.WaitIdle(std::chrono::seconds(30)));
        Check(service.Act(resourceId,ResourceAction::Download));Check(service.WaitIdle(std::chrono::seconds(180)));Check(selected().state==ResourceState::Installed);RuntimeMap(service,true,mapId);
        auto second=service.ResolveMap(mapId);Check(second&&VerifyPackage(*second,record));Decode(*second);
        std::cout<<"same-session empty-cache delete/redownload/reload PASS actualBodyBytes="<<backend->bytes.load()-before<<'\n';
    }
    {
        ResourceService offline(paths,{}, {},{}, {},Labels());Check(offline.WaitIdle(std::chrono::seconds(30)));auto lease=offline.ResolveMap(mapId);Check(static_cast<bool>(lease));Decode(*lease);lease.reset();
        RuntimeMap(offline,true,mapId);Check(offline.Act(resourceId,ResourceAction::Delete));Check(offline.WaitIdle(std::chrono::seconds(30)));Check(!offline.ResolveMap(mapId));RuntimeMap(offline,false,mapId);Check(ReadResourceText(sentinel,100)=="keep user data");
    }
    {
        ResourceService again(paths,{}, {},{}, {},Labels());RefreshRemoteResources(again,paths,policy,MapIds(),{},backend,!production);CatalogReady(again);
        Check(again.Act(resourceId,ResourceAction::Download));Check(again.WaitIdle(std::chrono::seconds(180)));auto last=again.ResolveMap(mapId);Check(static_cast<bool>(last)&&ReadResourceText(sentinel,100)=="keep user data");RuntimeMap(again,true,mapId);Decode(*last);
    }if(cut)Check(backend->cuts==1&&backend->readRetries>=1);
    std::cout<<"restart/offline/deletion/redownload/user-data PASS actualBodyBytes="<<backend->bytes.load()<<" readRetries="<<backend->readRetries.load()<<" failedOpens="<<backend->failedOpens.load()<<" injectedCloses="<<backend->cuts.load()<<"; GUI acceptance remains separate\n";
}
}
int wmain(int argc,wchar_t** argv)try{
    if(argc==4&&std::wstring_view(argv[1])==L"--local-package"){LocalPackage(std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]));return 0;}
    if((argc==6&&std::wstring_view(argv[1])==L"--verify")||(argc==7&&std::wstring_view(argv[1])==L"--verify-remote")){
        Check(std::wstring_view(argv[5])==L"production"||std::wstring_view(argv[5])==L"synthetic");
        const auto blob=ReadResourceText(std::filesystem::absolute(argv[3]),72);const std::array keys{ResourcePublicKey{Text(argv[4]),{blob.begin(),blob.end()}}};
        const auto manifest=VerifyResourceManifest(ReadResourceText(std::filesystem::absolute(argv[2]),2*MaximumManifestBytes+2048),keys,MapIds(),Text(argv[5])=="synthetic");
        for(const auto& record:manifest.components){const auto path=std::filesystem::absolute(argv[2]).parent_path()/record.artifact;Check(std::filesystem::file_size(path)==record.downloadSize&&ResourceHash(path)==record.sha256);}
        if(argc==7){auto backend=std::make_shared<Counting>();const ResourceSourcePolicy policy{Text(argv[6])};auto transport=HttpsResourceTransport(policy,GithubReleaseBackend(policy.baseUrl,backend,manifest.components));
            for(const auto& record:manifest.components){std::ifstream local(std::filesystem::absolute(argv[2]).parent_path()/record.artifact,std::ios::binary);auto stream=transport->Open(record,0,{});std::array<char,65536> remote{},expected{};std::uint64_t bytes{};
                while(const auto n=stream->Read(remote,{})){local.read(expected.data(),static_cast<std::streamsize>(n));Check(local.gcount()==static_cast<std::streamsize>(n)&&std::equal(remote.begin(),remote.begin()+n,expected.begin()));bytes+=n;}Check(bytes==record.downloadSize);std::cout<<"verified remote resource "<<record.resourceId<<" bytes="<<bytes<<std::endl;}
            std::cout<<"all remote resource bytes match signed local packages; actualBodyBytes="<<backend->bytes.load()<<std::endl;}
        std::cout<<"independent resource signature/package verification PASS components="<<manifest.components.size()<<'\n';return 0;
    }
    if(argc==3&&std::wstring_view(argv[1])==L"--probe"){Probe(Text(argv[2]));return 0;}
    if(argc!=8&&argc!=9){std::cerr<<"--verify[-remote] <signed catalog> <public blob> <keyId> <production|synthetic> [asset root] OR --[production-]interrupt|flow|cut-flow <program test root> <user test root> <metadata root> <asset root> <public blob> <keyId> [stableMapId]\n";return 2;}
    const std::wstring_view mode=argv[1];const bool production=mode.starts_with(L"--production-");
    const bool interrupt=mode==L"--interrupt"||mode==L"--production-interrupt",cut=mode==L"--cut-flow"||mode==L"--production-cut-flow";
    Check(interrupt||cut||mode==L"--flow"||mode==L"--production-flow");
    const auto blob=ReadResourceText(std::filesystem::absolute(argv[6]),72);ResourceRemotePolicy policy{{Text(argv[4])},{Text(argv[5])},{{Text(argv[7]),{blob.begin(),blob.end()}}}};
    const auto mapId=argc==9?Text(argv[8]):std::string("55f2d3fd4bdc2d5f408b4567");const auto knownMaps=MapIds();Check(std::ranges::find(knownMaps,mapId)!=knownMaps.end());
    Flow(interrupt,cut,std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::move(policy),production,mapId);return interrupt?3:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
