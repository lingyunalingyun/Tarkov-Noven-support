#include "resources/ResourceOnline.h"
#include "resources/ResourceFiles.h"
#include "updates/ReleaseManifest.h"
#include "raid/RaidJson.h"
#include <bcrypt.h>
#include <array>
#include <iostream>
using namespace noven::resources;
namespace {
void Check(bool b){if(!b)throw std::runtime_error("resource online assertion");}
template<class F>void Reject(F f){bool denied{};try{f();}catch(...){denied=true;}Check(denied);}
struct Signer {
    BCRYPT_ALG_HANDLE a{};BCRYPT_KEY_HANDLE key{};ResourcePublicKey pub{"ephemeral-resource-test",{}};
    Signer(){Check(BCryptOpenAlgorithmProvider(&a,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0);Check(BCryptGenerateKeyPair(a,&key,256,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0);ULONG n{};
        Check(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,nullptr,0,&n,0)>=0);pub.blob.resize(n);Check(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,pub.blob.data(),n,&n,0)>=0);}
    ~Signer(){BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(a,0);}
    std::string Sign(std::string_view payload,bool release=false){auto digest=release?noven::updates::ReleaseDigest(payload):ResourceManifestDigest(payload);std::array<unsigned char,64> sig{};ULONG n{};
        Check(BCryptSignHash(key,nullptr,digest.data(),32,sig.data(),64,&n,0)>=0&&n==64);
        return "{\"scheme\":\"ecdsa-p256-sha256\",\"keyId\":\"ephemeral-resource-test\",\"payloadHex\":"+noven::raid::json::Quote(ResourceHex(std::span(reinterpret_cast<const unsigned char*>(payload.data()),payload.size())))+",\"signatureHex\":"+noven::raid::json::Quote(ResourceHex(sig))+"}";}
};
class Backend final:public ContentBackend {
public:
    unsigned calls{},failures{};std::string destination="https://release-assets.githubusercontent.com/github-production-release-asset/1/a?signature=x%3D";bool loop{};
    std::vector<std::string> urls;std::vector<std::optional<ContentRange>> ranges;
    ContentResponse Open(std::string_view url,std::optional<ContentRange> range,std::stop_token)override{urls.emplace_back(url);ranges.push_back(range);++calls;if(calls<=failures)throw ContentNetworkError("injected connection timeout",12002);if(calls==failures+1||loop)return {302,{},{},{},destination};return {206,{},{},{},{}};}
};
class Quota final:public ContentBackend {
public:
    std::vector<std::string> urls;std::vector<ContentRange> ranges;bool limited{true};
    ContentResponse Open(std::string_view url,std::optional<ContentRange> range,std::stop_token)override{
        urls.emplace_back(url);Check(range.has_value());ranges.push_back(*range);
        if(url.starts_with("https://api.github.com/"))return {403,{},{},{},{},limited};
        if(url.starts_with("https://github.com/"))return {302,{},{},{},"https://release-assets.githubusercontent.com/github-production-release-asset/1/a",false};
        return {206,{},{},{},{},false};
    }
};
class Metadata final:public ContentBackend {
    struct Body final:ContentBody {std::string text;std::size_t at{};explicit Body(std::string s):text(std::move(s)){}
        std::size_t Read(std::span<char> out,std::stop_token)override{const auto n=std::min(out.size(),text.size()-at);std::copy_n(text.data()+at,n,out.data());at+=n;return n;}};
public:
    std::string text;bool outage{};
    ContentResponse Open(std::string_view,std::optional<ContentRange>,std::stop_token)override{if(outage)throw std::runtime_error("offline");return {200,{},std::to_string(text.size()),std::make_unique<Body>(text),{}};}
};
}
int main()try{
    Signer signer;const std::array keys{signer.pub};const std::string payload="{\"schemaVersion\":1,\"audience\":\"production\",\"components\":[]}";
    Check(VerifyResourceManifest(signer.Sign(payload),keys,{"factory"}).components.empty());
    ResourceRecord record{"maps.factory","factory","Factory","Factory","0.0.1",std::string(64,'a'),"factory-0.0.1.nvr",128,64};
    record.githubAssetId=1;
    auto catalog=EncodeResourceManifest({{record}});catalog.insert(1,"\"audience\":\"production\",");
    Check(VerifyResourceManifest(signer.Sign(catalog),keys,{"factory"}).components.size()==1);
    auto unsignedAsset=record;unsignedAsset.githubAssetId=0;
    auto noAsset=EncodeResourceManifest({{unsignedAsset}});noAsset.insert(1,"\"audience\":\"production\",");
    Reject([&]{VerifyResourceManifest(signer.Sign(noAsset),keys,{"factory"});});
    for(const auto badId:{"0","-1","9007199254740992"}){auto bad=catalog;const auto at=bad.find("\"githubAssetId\":1");Check(at!=std::string::npos);bad.replace(at,17,std::string("\"githubAssetId\":")+badId);Reject([&]{VerifyResourceManifest(signer.Sign(bad),keys,{"factory"});});}
    Reject([&]{VerifyResourceManifest(signer.Sign(catalog),keys,{"woods"});});
    Reject([&]{VerifyResourceManifest(std::string(2*MaximumManifestBytes+2049,'x'),keys,{"factory"});});
    Reject([&]{VerifyResourceManifest(payload,keys,{"factory"});});Reject([&]{VerifyResourceManifest(signer.Sign(payload,true),keys,{"factory"});});
    Reject([&]{VerifyResourceManifest(signer.Sign(payload),{}, {"factory"});});Reject([&]{VerifyResourceManifest(signer.Sign(payload),keys,{"factory"},true);});
    auto altered=signer.Sign(payload);altered[altered.size()-5]=altered[altered.size()-5]=='a'?'b':'a';Reject([&]{VerifyResourceManifest(altered,keys,{"factory"});});
    Signer wrong;const std::array wrongKeys{wrong.pub};Reject([&]{VerifyResourceManifest(signer.Sign(payload),wrongKeys,{"factory"});});
    for(const auto url:{"http://release-assets.githubusercontent.com/github-production-release-asset/a","https://release-assets.githubusercontent.com.evil.test/github-production-release-asset/a","https://github.com/other/a","https://release-assets.githubusercontent.com/evil/a","https://release-assets.githubusercontent.com/github-production-release-asset/../a","https://release-assets.githubusercontent.com:444/github-production-release-asset/a"})Check(!TrustedGithubAssetRedirect(url));
    auto backend=std::make_shared<Backend>();const std::string root="https://github.com/lingyunalingyun/Noven-Update-Channel/releases/download/maps-v1/";auto adapter=GithubReleaseBackend(root,backend);
    auto apiBackend=std::make_shared<Backend>();auto api=GithubReleaseBackend(root,apiBackend,std::span(&record,1));
    Check(api->ReadRetries()==2&&apiBackend->ReadRetries()==0);
    Check(api->Open(root+record.artifact,ContentRange{12345,4194304},{}).status==206);
    Check(apiBackend->urls.front()=="https://api.github.com/repos/lingyunalingyun/Noven-Update-Channel/releases/assets/1"&&apiBackend->urls.back()==apiBackend->destination);
    Check(apiBackend->ranges.size()==2);for(const auto& range:apiBackend->ranges)Check(range&&range->offset==12345&&range->count==4194304);
    Reject([&]{api->Open(root+"unknown.nvr",{},{});});Check(apiBackend->calls==2);
    Reject([&]{GithubReleaseBackend(root,apiBackend,std::span(&unsignedAsset,1));});
    auto quota=std::make_shared<Quota>();auto fallback=GithubReleaseBackend(root,quota,std::span(&record,1));
    Check(fallback->Open(root+record.artifact,ContentRange{3,8},{}).status==206&&quota->urls.size()==3);
    Check(quota->urls[1]==root+record.artifact&&quota->urls[2].starts_with("https://release-assets.githubusercontent.com/"));
    Check(fallback->Open(root+record.artifact,ContentRange{11,3},{}).status==206&&quota->urls.size()==5&&quota->urls[3]==root+record.artifact);
    for(std::size_t i=0;i<quota->ranges.size();++i)Check(quota->ranges[i].offset==(i<3?3:11)&&quota->ranges[i].count==(i<3?8:3));
    auto denied=std::make_shared<Quota>();denied->limited=false;auto noFallback=GithubReleaseBackend(root,denied,std::span(&record,1));
    Check(noFallback->Open(root+record.artifact,ContentRange{0,1},{}).status==403&&denied->urls.size()==1);
    Check(adapter->Open(root+"factory.nvr",ContentRange{0,32},{}).status==206&&backend->calls==2);
    backend->calls=0;backend->loop=true;Reject([&]{adapter->Open(root+"factory.nvr",{},{});});Check(backend->calls==2);backend->loop=false;
    backend->calls=0;backend->destination="https://evil.test/a";Reject([&]{adapter->Open(root+"factory.nvr",{},{});});Check(backend->calls==1);
    Reject([&]{adapter->Open(root+"../evil.nvr",{},{});});Reject([&]{GithubReleaseBackend("https://github.com/evil/repo/releases/download/maps-v1/");});
    backend->destination="https://release-assets.githubusercontent.com/github-production-release-asset/1/a";backend->calls=0;backend->failures=1;
    Check(adapter->Open(root+"factory.nvr",{},{}).status==206&&backend->calls==3);
    backend->calls=0;backend->failures=9;Reject([&]{adapter->Open(root+"factory.nvr",{},{});});Check(backend->calls==3);
    const auto temp=std::filesystem::temp_directory_path()/("noven-online-"+std::to_string(GetCurrentProcessId()));
    ResourceService service(noven::common::AppPaths::Test(temp/"program",temp/"test"),{}, {},{}, {},{{"factory","Factory","Factory"}});
    service.RemoteStatus(RemoteAvailability::NetworkUnavailable);Check(!service.CanDownload()&&ResourceActions(service.Snapshot()[0],service.Remote()).empty());
    service.RemoteStatus(RemoteAvailability::ManifestInvalid);Check(!service.Act("maps.factory",ResourceAction::Download));
    auto metadata=std::make_shared<Metadata>();metadata->text=signer.Sign(payload);
    const auto paths=noven::common::AppPaths::Test(temp/"program",temp/"test");ResourceRemotePolicy policy{{"https://raw.githubusercontent.com/lingyunalingyun/Noven-Update-Channel/main/maps/"},{root},{signer.pub}};
    RefreshRemoteResources(service,paths,policy,{"factory"},{},metadata);Check(service.Remote()==RemoteAvailability::Online&&!service.Act("maps.factory",ResourceAction::Download));
    metadata->outage=true;RefreshRemoteResources(service,paths,policy,{"factory"},{},metadata);Check(service.Remote()==RemoteAvailability::Cached);
    metadata->outage=false;metadata->text=payload;RefreshRemoteResources(service,paths,policy,{"factory"},{},metadata);Check(service.Remote()==RemoteAvailability::ManifestInvalid&&!service.CanDownload());
    Check(ReadResourceText(paths.ResourceManifests()/"remote-signed.json",2*MaximumManifestBytes+2048)!=payload);
    metadata->outage=true;Check(WriteResourceText(paths.ResourceManifests()/"remote-signed.json","corrupt"));RefreshRemoteResources(service,paths,policy,{"factory"},{},metadata);Check(service.Remote()==RemoteAvailability::ManifestInvalid);
    service.Shutdown();Check(RemoveResourceTree(temp.parent_path(),temp));
    std::cout<<"resource authentication / cross-domain / redirect policy PASS (offline)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
