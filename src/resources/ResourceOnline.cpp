#include "resources/ResourceOnline.h"
#include "resources/ResourceFiles.h"
#include "plugins/PluginHttpOrigin.h"
#include "raid/RaidJson.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
namespace noven::resources {
namespace {
[[noreturn]] void Invalid(){throw std::runtime_error("invalid authenticated resource metadata");}
struct Algorithm {BCRYPT_ALG_HANDLE h{};~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);}};
struct Key {BCRYPT_KEY_HANDLE h{};~Key(){if(h)BCryptDestroyKey(h);}};
class RetryBackend final:public ContentBackend {
    std::shared_ptr<ContentBackend> backend_;
public:
    explicit RetryBackend(std::shared_ptr<ContentBackend> backend):backend_(std::move(backend)){}
    ContentResponse Open(std::string_view url,std::optional<ContentRange> range,std::stop_token stop)override{
        if(stop.stop_requested())throw std::runtime_error("cancelled resource request");
        for(unsigned attempt=0;;++attempt)try{return backend_->Open(url,range,stop);}
        catch(const ContentNetworkError& error){
            if(attempt==2||stop.stop_requested()||!TransientContentError(error.code))throw;
            ContentRetryDelay(attempt,stop);
        }
    }
    void ReadRetry(ContentRange range,unsigned retry,unsigned code)override{backend_->ReadRetry(range,retry,code);}
};
class GithubBackend final:public ContentBackend {
    std::string root_;std::shared_ptr<ContentBackend> backend_;std::map<std::string,std::uint64_t> assets_;std::atomic_bool apiLimited_{};
public:
    GithubBackend(std::string root,std::shared_ptr<ContentBackend> backend,std::span<const ResourceRecord> assets):root_(std::move(root)),backend_(std::move(backend)){
        if(assets.size()>MaximumComponents)Invalid();for(const auto& record:assets){if(!record.githubAssetId||!ValidArtifactName(record.artifact)||!assets_.emplace(record.artifact,record.githubAssetId).second)Invalid();}
    }
    ContentResponse Open(std::string_view url,std::optional<ContentRange> range,std::stop_token stop)override{
        if(!url.starts_with(root_)||!ValidArtifactName(url.substr(root_.size())))Invalid();
        const auto found=assets_.find(std::string(url.substr(root_.size())));
        if(!assets_.empty()&&found==assets_.end())Invalid();
        const bool useApi=found!=assets_.end()&&!apiLimited_.load();
        const auto request=!useApi?std::string(url):"https://api.github.com/repos/lingyunalingyun/Noven-Update-Channel/releases/assets/"+std::to_string(found->second);
        auto response=backend_->Open(request,range,stop);
        // 公共 API 的匿名配额不适合逐块传输；仅确认限流后使用固定标签/签名文件名。
        // On confirmed API quota exhaustion, use the approved release tag/signed basename, never an arbitrary URL.
        if(useApi&&response.rateLimited&&(response.status==403||response.status==429)){apiLimited_=true;response={};response=backend_->Open(url,range,stop);}
        if(response.status==302){
            if(!TrustedGithubAssetRedirect(response.location))Invalid();
            const auto destination=response.location;response={};
            response=backend_->Open(destination,range,stop);
        }
        if(response.status>=300&&response.status<400)Invalid();return response;
    }
    unsigned ReadRetries()const override{return 2;}
    void ReadRetry(ContentRange range,unsigned retry,unsigned code)override{
        const auto message=L"[resources] read retry="+std::to_wstring(retry)+L" offset="+std::to_wstring(range.offset)+L" count="+std::to_wstring(range.count)+L" Win32="+std::to_wstring(code)+L"\n";
        OutputDebugStringW(message.c_str());backend_->ReadRetry(range,retry,code);
    }
};
}
std::string ResourceHex(std::span<const unsigned char> bytes){constexpr char digits[]="0123456789abcdef";std::string s;for(auto b:bytes){s+=digits[b>>4];s+=digits[b&15];}return s;}
std::vector<unsigned char> ResourceUnhex(std::string_view s,std::size_t maximum){
    if(s.size()%2||s.size()/2>maximum||s.find_first_not_of("0123456789abcdef")!=s.npos)Invalid();
    auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};std::vector<unsigned char> bytes;
    for(std::size_t i=0;i<s.size();i+=2)bytes.push_back(static_cast<unsigned char>(nibble(s[i])*16+nibble(s[i+1])));return bytes;
}
std::vector<unsigned char> ResourceManifestDigest(std::string_view payload){
    if(payload.empty()||payload.size()>MaximumManifestBytes)Invalid();Algorithm a;
    if(BCryptOpenAlgorithmProvider(&a.h,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)Invalid();
    BCRYPT_HASH_HANDLE h{};if(BCryptCreateHash(a.h,&h,nullptr,0,nullptr,0,0)<0)Invalid();
    constexpr std::string_view domain="Noven.ResourceManifest.v1\n";std::vector<unsigned char> digest(32);
    const bool ok=BCryptHashData(h,reinterpret_cast<PUCHAR>(const_cast<char*>(domain.data())),static_cast<ULONG>(domain.size()),0)>=0&&
        BCryptHashData(h,reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),static_cast<ULONG>(payload.size()),0)>=0&&BCryptFinishHash(h,digest.data(),32,0)>=0;
    BCryptDestroyHash(h);if(!ok)Invalid();return digest;
}
ResourceManifest VerifyResourceManifest(std::string_view text,std::span<const ResourcePublicKey> keys,const std::vector<std::string>& maps,bool synthetic){
    if(text.empty()||text.size()>2*MaximumManifestBytes+2048)Invalid();const auto envelope=raid::json::Parser(text).Parse();
    if(envelope.At("scheme").String()!="ecdsa-p256-sha256")Invalid();const auto id=envelope.At("keyId").String();if(id.size()>64)Invalid();
    const auto found=std::find_if(keys.begin(),keys.end(),[&](const auto& key){return key.id==id;});if(found==keys.end()||found->blob.size()!=72)Invalid();
    const auto bytes=ResourceUnhex(envelope.At("payloadHex").String(),MaximumManifestBytes),signature=ResourceUnhex(envelope.At("signatureHex").String(),64);
    if(signature.size()!=64)Invalid();BCRYPT_ECCKEY_BLOB header{};memcpy(&header,found->blob.data(),sizeof(header));
    if(header.dwMagic!=BCRYPT_ECDSA_PUBLIC_P256_MAGIC||header.cbKey!=32)Invalid();Algorithm a;Key key;
    if(BCryptOpenAlgorithmProvider(&a.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)<0||BCryptImportKeyPair(a.h,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key.h,const_cast<PUCHAR>(found->blob.data()),72,0)<0)Invalid();
    const std::string payload(bytes.begin(),bytes.end());auto digest=ResourceManifestDigest(payload);
    if(BCryptVerifySignature(key.h,nullptr,digest.data(),32,const_cast<PUCHAR>(signature.data()),64,0)<0)Invalid();
    if(raid::json::Parser(payload).Parse().At("audience").String()!=(synthetic?"synthetic":"production"))Invalid();
    auto manifest=ParseResourceManifest(payload,maps);for(const auto& r:manifest.components)if(!r.githubAssetId)Invalid();return manifest;
}
bool TrustedGithubAssetRedirect(std::string_view location){
    const auto url=plugins::ParseHttpUrl(location);
    return url&&url->port==443&&url->origin=="https://release-assets.githubusercontent.com"&&
        location.starts_with("https://release-assets.githubusercontent.com/")&&url->target.starts_with("/github-production-release-asset/")&&
        url->target.find("..") ==url->target.npos&&url->target.substr(0,url->target.find('?')).find('%')==std::string::npos;
}
std::shared_ptr<ContentBackend> GithubReleaseBackend(std::string root,std::shared_ptr<ContentBackend> backend,std::span<const ResourceRecord> assets){
    constexpr std::string_view prefix="https://github.com/lingyunalingyun/Noven-Update-Channel/releases/download/";
    if(!root.starts_with(prefix)||!root.ends_with('/'))Invalid();const auto tag=std::string_view(root).substr(prefix.size(),root.size()-prefix.size()-1);
    if(tag.empty()||tag.size()>80||tag.front()=='.'||tag.find("..")!=tag.npos||tag.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=tag.npos)Invalid();
    return std::make_shared<GithubBackend>(std::move(root),std::make_shared<RetryBackend>(backend?std::move(backend):WindowsContentBackend(true,true)),assets);
}
ResourceRemotePolicy CompiledResourcePolicy(){
    ResourceRemotePolicy policy;policy.metadata.baseUrl=NOVEN_RESOURCE_SOURCE_ROOT;policy.assets.baseUrl=NOVEN_RESOURCE_ASSET_ROOT;
    constexpr std::string_view id=NOVEN_RESOURCE_KEY_ID,hex=NOVEN_RESOURCE_PUBLIC_KEY_HEX;
    if(!id.empty()&&!hex.empty())policy.keys.push_back({std::string(id),ResourceUnhex(hex,72)});return policy;
}
void RefreshRemoteResources(ResourceService& service,const common::AppPaths& paths,const ResourceRemotePolicy& policy,
    const std::vector<std::string>& maps,std::stop_token stop,std::shared_ptr<ContentBackend> backend,bool synthetic){
    if(policy.metadata.baseUrl.empty()){service.RemoteStatus(RemoteAvailability::ProductionEndpointUnconfigured);return;}
    service.RemoteStatus(RemoteAvailability::Loading);const auto cache=paths.ResourceManifests()/L"remote-signed.json";
    std::string text;bool cached{};
    try{text=FetchContentText(policy.metadata,"manifest.json",2*MaximumManifestBytes+2048,std::make_shared<RetryBackend>(backend?backend:WindowsContentBackend(true,true)),stop);}
    catch(...){try{text=ReadResourceText(cache,2*MaximumManifestBytes+2048);cached=true;}catch(...){service.RemoteStatus(RemoteAvailability::NetworkUnavailable);return;}}
    try{
        const auto manifest=VerifyResourceManifest(text,policy.keys,maps,synthetic);
        if(stop.stop_requested())return;
        auto transport=HttpsResourceTransport(policy.assets,GithubReleaseBackend(policy.assets.baseUrl,backend,manifest.components));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!service.WaitIdle(std::chrono::milliseconds(100))){if(stop.stop_requested()||std::chrono::steady_clock::now()>deadline)return;}
        if(stop.stop_requested()||!service.ConfigureRemote(manifest,std::move(transport),cached?RemoteAvailability::Cached:RemoteAvailability::Online))return;
        service.RemoteStatus(cached?RemoteAvailability::Cached:RemoteAvailability::Online);
        if(!cached)(void)WriteResourceText(cache,text);
    }catch(...){service.RemoteStatus(RemoteAvailability::ManifestInvalid);}
}
}
