#include "updates/ReleaseManifest.h"
#include "plugins/PluginManifest.h"
#include "raid/RaidJson.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <set>
namespace noven::updates {
namespace {
[[noreturn]] void Invalid(){throw std::runtime_error("invalid or unauthenticated release manifest");}
using raid::json::Value;
std::string Text(const Value& v,const char* key,std::size_t maximum){auto s=v.At(key).String();
    if(s.empty()||s.size()>maximum||std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;}))Invalid();return s;}
std::uint64_t Number(const Value& v,const char* key,std::uint64_t maximum){const auto n=v.At(key).Int();
    if(n<0||static_cast<std::uint64_t>(n)>maximum)Invalid();return static_cast<std::uint64_t>(n);}
std::string Hash(const Value& v){auto s=Text(v,"sha256",64);if(s.size()!=64||s.find_first_not_of("0123456789abcdef")!=s.npos)Invalid();return s;}
std::string Fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
struct Algorithm {BCRYPT_ALG_HANDLE handle{};~Algorithm(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}};
struct Key {BCRYPT_KEY_HANDLE handle{};~Key(){if(handle)BCryptDestroyKey(handle);}};
}
std::string Hex(std::span<const unsigned char> bytes){constexpr char digits[]="0123456789abcdef";std::string text; text.reserve(bytes.size()*2);
    for(auto b:bytes){text+=digits[b>>4];text+=digits[b&15];}return text;}
std::vector<unsigned char> Unhex(std::string_view s,std::size_t maximum){
    if(s.size()%2||s.size()/2>maximum||s.find_first_not_of("0123456789abcdef")!=s.npos)Invalid();
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};std::vector<unsigned char> out(s.size()/2);
    for(std::size_t i=0;i<out.size();++i)out[i]=static_cast<unsigned char>((digit(s[i*2])<<4)|digit(s[i*2+1]));return out;
}
std::vector<unsigned char> ReleaseDigest(std::string_view payload){
    if(payload.size()>MaximumManifestBytes)Invalid();Algorithm algorithm;
    if(BCryptOpenAlgorithmProvider(&algorithm.handle,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)Invalid();
    BCRYPT_HASH_HANDLE hash{};if(BCryptCreateHash(algorithm.handle,&hash,nullptr,0,nullptr,0,0)<0)Invalid();
    constexpr std::string_view domain="Noven.ReleaseManifest.v1\n";std::vector<unsigned char> digest(32);
    const bool ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(domain.data())),static_cast<ULONG>(domain.size()),0)>=0&&
        BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),static_cast<ULONG>(payload.size()),0)>=0&&
        BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    BCryptDestroyHash(hash);if(!ok)Invalid();return digest;
}
bool ValidReleaseVersion(std::string_view version){return version.size()<=64&&plugins::ValidSemanticVersion(version)&&version.find_first_not_of("0123456789.")==version.npos;}
int CompareReleaseVersions(std::string_view a,std::string_view b){
    if(!ValidReleaseVersion(a)||!ValidReleaseVersion(b))Invalid();
    for(int i=0;i<3;++i){const auto da=a.find('.'),db=b.find('.');auto x=a.substr(0,da),y=b.substr(0,db);
        if(x.size()!=y.size())return x.size()<y.size()?-1:1;if(x!=y)return x<y?-1:1;
        if(i<2){a.remove_prefix(da+1);b.remove_prefix(db+1);}}
    return 0;
}
bool ValidReleasePath(std::string_view path){
    if(path.empty()||path.size()>240||path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/")!=path.npos)return false;
    while(!path.empty()) {const auto slash=path.find('/');const auto part=path.substr(0,slash);
        if(part.empty()||part.size()>80||part=="."||part==".."||part.back()=='.')return false;
        const auto stem=Fold(std::string(part.substr(0,part.find('.'))));
        if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||(stem.size()==4&&(stem.starts_with("com")||stem.starts_with("lpt"))&&stem[3]>='1'&&stem[3]<='9'))return false;
        if(slash==path.npos)return true;path.remove_prefix(slash+1);if(path.empty())return false;
    }return false;
}
ReleaseManifest ParseReleaseManifest(std::string_view text){
    if(text.empty()||text.size()>MaximumManifestBytes)Invalid();const auto root=raid::json::Parser(text).Parse();
    if(root.At("schemaVersion").Int()!=1||Text(root,"channel",16)!="stable")Invalid();ReleaseManifest manifest;
    manifest.version=Text(root,"releaseVersion",64);if(!ValidReleaseVersion(manifest.version))Invalid();
    manifest.publishedAt=Text(root,"publishedAt",40);const auto note=root.object.find("notes");
    if(note!=root.object.end()){manifest.notes=note->second.String();if(manifest.notes.size()>4096)Invalid();}
    const auto& files=root.At("files").Array();if(files.empty()||files.size()>MaximumReleaseFiles)Invalid();
    std::set<std::string> paths;std::uint64_t total{},ranges{};
    for(const auto& v:files){ReleaseFile f;f.path=Text(v,"path",240);if(!ValidReleasePath(f.path)||!paths.insert(Fold(f.path)).second)Invalid();
        const auto folded=Fold(f.path);
        for(const auto& existing:paths)if(existing!=folded&&(existing.starts_with(folded+"/")||folded.starts_with(existing+"/")))Invalid();
        f.size=Number(v,"size",MaximumReleaseBytes);f.sha256=Hash(v);if(f.size>MaximumReleaseBytes-total)Invalid();total+=f.size;
        const auto strategy=Text(v,"strategy",16);if(strategy!=(f.size<ChunkThreshold?"file":"chunks"))Invalid();
        const auto& parts=v.At("content").Array();const auto expected=f.size<ChunkThreshold?1:(f.size+ChunkBytes-1)/ChunkBytes;
        if(parts.size()!=expected||ranges+parts.size()>8192)Invalid();ranges+=parts.size();std::uint64_t offset{};
        for(const auto& p:parts){ContentRange r;r.fileOffset=Number(p,"offset",f.size);r.size=Number(p,"size",ChunkThreshold);
            r.packOffset=Number(p,"packOffset",MaximumReleaseBytes);r.sha256=Hash(p);r.pack=Text(p,"pack",128);
            if(r.pack.find('/')!=r.pack.npos||!ValidReleasePath(r.pack)||!r.pack.ends_with(".pack")||r.fileOffset!=offset||r.packOffset>MaximumReleaseBytes-r.size)Invalid();
            const auto size=f.size<ChunkThreshold?f.size:std::min(ChunkBytes,f.size-offset);if(r.size!=size)Invalid();offset+=r.size;f.content.push_back(std::move(r));}
        if(offset!=f.size||(strategy=="file"&&f.content[0].sha256!=f.sha256))Invalid();manifest.files.push_back(std::move(f));}
    std::sort(manifest.files.begin(),manifest.files.end(),[](const auto& a,const auto& b){return a.path<b.path;});return manifest;
}
std::string EncodeReleaseManifest(const ReleaseManifest& m){using raid::json::Quote;
    std::string s="{\"schemaVersion\":1,\"releaseVersion\":"+Quote(m.version)+",\"channel\":\"stable\",\"publishedAt\":"+Quote(m.publishedAt)+",\"notes\":"+Quote(m.notes)+",\"files\":[";
    bool comma{};for(const auto& f:m.files){if(comma)s+=',';comma=true;s+="{\"path\":"+Quote(f.path)+",\"size\":"+std::to_string(f.size)+",\"sha256\":"+Quote(f.sha256)+",\"strategy\":"+Quote(f.size<ChunkThreshold?"file":"chunks")+",\"content\":[";
        bool part{};for(const auto& r:f.content){if(part)s+=',';part=true;s+="{\"offset\":"+std::to_string(r.fileOffset)+",\"size\":"+std::to_string(r.size)+",\"packOffset\":"+std::to_string(r.packOffset)+",\"sha256\":"+Quote(r.sha256)+",\"pack\":"+Quote(r.pack)+"}";}s+="]}";}s+="]}";
    (void)ParseReleaseManifest(s);return s;
}
AuthenticatedRelease VerifyRelease(std::string_view text,std::span<const ReleasePublicKey> keys){
    if(text.empty()||text.size()>2*MaximumManifestBytes+2048)Invalid();const auto envelope=raid::json::Parser(text).Parse();
    if(Text(envelope,"scheme",32)!="ecdsa-p256-sha256")Invalid();const auto id=Text(envelope,"keyId",64);
    const auto found=std::find_if(keys.begin(),keys.end(),[&](const auto& k){return k.id==id;});if(found==keys.end())Invalid();
    const auto payload=Unhex(envelope.At("payloadHex").String(),MaximumManifestBytes),signature=Unhex(envelope.At("signatureHex").String(),64);
    if(signature.size()!=64||found->cngPublicBlob.size()!=sizeof(BCRYPT_ECCKEY_BLOB)+64)Invalid();
    BCRYPT_ECCKEY_BLOB header{};memcpy(&header,found->cngPublicBlob.data(),sizeof(header));if(header.dwMagic!=BCRYPT_ECDSA_PUBLIC_P256_MAGIC||header.cbKey!=32)Invalid();
    Algorithm algorithm;Key key;if(BCryptOpenAlgorithmProvider(&algorithm.handle,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)<0||
        BCryptImportKeyPair(algorithm.handle,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key.handle,const_cast<PUCHAR>(found->cngPublicBlob.data()),static_cast<ULONG>(found->cngPublicBlob.size()),0)<0)Invalid();
    const std::string bytes(payload.begin(),payload.end());auto digest=ReleaseDigest(bytes);
    if(BCryptVerifySignature(key.handle,nullptr,digest.data(),static_cast<ULONG>(digest.size()),const_cast<PUCHAR>(signature.data()),static_cast<ULONG>(signature.size()),0)<0)Invalid();
    AuthenticatedRelease release;release.manifest_=ParseReleaseManifest(bytes);release.identity_=Hex(digest);return release;
}
}
