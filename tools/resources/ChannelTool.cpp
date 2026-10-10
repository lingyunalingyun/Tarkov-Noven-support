#include "resources/ResourceOnline.h"
#include "resources/ResourceFiles.h"
#include "ui/MapPage.h"
#include <wincodec.h>
#include <array>
#include <iostream>
using namespace noven::resources;
namespace {
void Check(bool b){if(!b)throw std::runtime_error("resource channel check failed");}
std::string Text(std::wstring_view s){std::string out;for(auto c:s){Check(c<128);out+=static_cast<char>(c);}return out;}
class Counting final:public ContentBackend {
    std::shared_ptr<ContentBackend> inner_=WindowsContentBackend(true,true);
    struct Body final:ContentBody {Counting& owner;std::unique_ptr<ContentBody> inner;std::chrono::steady_clock::time_point returned{};
        Body(Counting& o,std::unique_ptr<ContentBody> b):owner(o),inner(std::move(b)){}
        std::size_t Read(std::span<char> out,std::stop_token stop)override{
            const auto begin=std::chrono::steady_clock::now();if(returned.time_since_epoch().count())owner.consumerMs+=std::chrono::duration_cast<std::chrono::milliseconds>(begin-returned).count();
            try{const auto n=inner->Read(out,stop);returned=std::chrono::steady_clock::now();owner.readMs+=std::chrono::duration_cast<std::chrono::milliseconds>(returned-begin).count();owner.bytes+=n;return n;}
            catch(const std::exception& e){owner.readMs+=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();std::cerr<<"native body failure after actualBodyBytes="<<owner.bytes.load()<<" readMs="<<owner.readMs.load()<<" consumerMs="<<owner.consumerMs.load()<<" "<<e.what()<<'\n';throw;}}};
public:
    std::atomic_uint64_t bytes{},readMs{},consumerMs{};
    ContentResponse Open(std::string_view url,std::optional<ContentRange> range,std::stop_token stop)override{
        const auto end=url.find('?');std::cout<<"request "<<url.substr(0,end)<<std::endl;
        ContentResponse result;
        try{result=inner_->Open(url,range,stop);}catch(const std::exception& e){std::cerr<<"native transport failure Win32="<<GetLastError()<<" "<<e.what()<<'\n';throw;}
        std::cout<<"HTTPS "<<url.substr(0,end)<<" status="<<result.status<<" range="<<result.range<<" length="<<result.length<<'\n';
        if(result.body)result.body=std::make_unique<Body>(*this,std::move(result.body));return result;
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
void RuntimeMap(ResourceService& service,bool installed){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
    {
    const std::string_view assets=NOVEN_RESOURCE_TEST_ASSETS;
    const auto directory=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(assets.data()),assets.size()));
    noven::ui::MapPage page;std::wstring error;Check(page.Initialize(directory,error,true));
    page.SelectMap("55f2d3fd4bdc2d5f408b4567");
    page.SetResourceResolver([&](auto id){return service.ResolveMap(id);},[](auto){return noven::ui::MapPage::ResourceInfo{};},{});
    page.ResourcesChanged();Check(page.MissingResource()!=installed);page.ResourceActive(false);
    }CoUninitialize();
}
void CatalogReady(ResourceService& service){
    const auto state=service.Remote();Check(state==RemoteAvailability::Online||state==RemoteAvailability::Cached);
    std::cout<<"authenticated catalog="<<(state==RemoteAvailability::Online?"fresh HTTPS":"reverified cached; metadata fetch unavailable")<<'\n';
}
void Flow(bool interrupt,const std::filesystem::path& program,const std::filesystem::path& test,ResourceRemotePolicy policy){
    const std::string mapId="55f2d3fd4bdc2d5f408b4567",resourceId="maps."+mapId;
    const auto paths=noven::common::AppPaths::Test(program,test);const auto sentinel=paths.Data()/"channel-preservation.txt";
    if(!std::filesystem::exists(sentinel))Check(WriteResourceText(sentinel,"keep user data"));
    auto backend=std::make_shared<Counting>();ResourceService* pointer{};bool paused{};
    {
        ResourceService service(paths,{}, {},{},[&]{if(interrupt&&pointer&&!paused){for(const auto& row:pointer->Snapshot())if(row.received>=65536&&row.state==ResourceState::Downloading){paused=true;Check(pointer->Act(row.record.resourceId,ResourceAction::Pause));}}},{{mapId,"SYNTHETIC Factory","SYNTHETIC Factory"}});pointer=&service;
        RefreshRemoteResources(service,paths,policy,{mapId},{},backend,true);CatalogReady(service);
        auto row=service.Snapshot().front();Check(row.record.downloadSize>ContentRangeBytes);const auto record=row.record;
        if(row.state!=ResourceState::Installed)Check(service.Act(record.resourceId,row.state==ResourceState::Paused?ResourceAction::Resume:ResourceAction::Download));Check(service.WaitIdle(std::chrono::seconds(180)));
        row=service.Snapshot().front();if(row.state==ResourceState::Error)std::cerr<<"resource state error="<<row.error<<'\n';if(interrupt){Check(paused&&row.state==ResourceState::Paused&&row.received>=65536);std::cout<<"interrupted partialBytes="<<row.received<<" actualBodyBytes="<<backend->bytes.load()<<'\n';return;}
        Check(row.state==ResourceState::Installed);RuntimeMap(service,true);auto lease=service.ResolveMap(mapId);Check(lease&&VerifyPackage(*lease,record));Decode(*lease);
        Check(!service.Act(record.resourceId,ResourceAction::Delete));lease.reset();Check(service.Act(record.resourceId,ResourceAction::Verify));Check(service.WaitIdle(std::chrono::seconds(30)));Check(service.Snapshot()[0].state==ResourceState::Installed);
        std::cout<<"remote install/PNG decode/verify/in-use delete guard PASS actualBodyBytes="<<backend->bytes.load()<<'\n';
    }
    {
        ResourceService offline(paths,{}, {},{}, {},{{mapId,"Factory","Factory"}});Check(offline.WaitIdle(std::chrono::seconds(30)));auto lease=offline.ResolveMap(mapId);Check(static_cast<bool>(lease));Decode(*lease);lease.reset();
        RuntimeMap(offline,true);Check(offline.Act(resourceId,ResourceAction::Delete));Check(offline.WaitIdle(std::chrono::seconds(30)));Check(!offline.ResolveMap(mapId));RuntimeMap(offline,false);Check(ReadResourceText(sentinel,100)=="keep user data");
    }
    {
        ResourceService again(paths,{}, {},{}, {},{{mapId,"Factory","Factory"}});RefreshRemoteResources(again,paths,policy,{mapId},{},backend,true);CatalogReady(again);
        Check(again.Act(resourceId,ResourceAction::Download));Check(again.WaitIdle(std::chrono::seconds(180)));Check(again.Snapshot()[0].state==ResourceState::Installed&&ReadResourceText(sentinel,100)=="keep user data");
    }std::cout<<"restart/offline/deletion/redownload/user-data PASS; GUI acceptance remains separate\n";
}
}
int wmain(int argc,wchar_t** argv)try{
    if(argc==6&&std::wstring_view(argv[1])==L"--verify"){
        const auto blob=ReadResourceText(std::filesystem::absolute(argv[3]),72);const std::array keys{ResourcePublicKey{Text(argv[4]),{blob.begin(),blob.end()}}};
        const auto manifest=VerifyResourceManifest(ReadResourceText(std::filesystem::absolute(argv[2]),2*MaximumManifestBytes+2048),keys,{"55f2d3fd4bdc2d5f408b4567"},Text(argv[5])=="synthetic");
        for(const auto& record:manifest.components){const auto path=std::filesystem::absolute(argv[2]).parent_path()/record.artifact;Check(std::filesystem::file_size(path)==record.downloadSize&&ResourceHash(path)==record.sha256);}
        std::cout<<"independent resource signature/package verification PASS components="<<manifest.components.size()<<'\n';return 0;
    }
    if(argc==3&&std::wstring_view(argv[1])==L"--probe"){Probe(Text(argv[2]));return 0;}
    if(argc!=8){std::cerr<<"--probe <fixed GitHub asset root> OR --interrupt|--flow <program test root> <user test root> <synthetic metadata root> <asset root> <public key file> <keyId>\n";return 2;}
    Check(std::wstring_view(argv[1])==L"--interrupt"||std::wstring_view(argv[1])==L"--flow");
    const auto blob=ReadResourceText(std::filesystem::absolute(argv[6]),72);ResourceRemotePolicy policy{{Text(argv[4])},{Text(argv[5])},{{Text(argv[7]),{blob.begin(),blob.end()}}}};
    Flow(std::wstring_view(argv[1])==L"--interrupt",std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::move(policy));return std::wstring_view(argv[1])==L"--interrupt"?3:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
