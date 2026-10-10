#include "updates/ReleaseManifest.h"
#include "updates/UpdatePlanner.h"
#include "resources/ResourceFiles.h"
#include "raid/RaidJson.h"
#include <bcrypt.h>
#include <iostream>
#include <array>
using namespace noven::updates;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("update manifest assertion");}
template<class F>void Reject(F f){bool failed{};try{f();}catch(...){failed=true;}Check(failed);}
struct TestSigner final {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE key{};ReleasePublicKey publicKey{"ephemeral-test-only",{}};
    TestSigner(){Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0);
        Check(BCryptGenerateKeyPair(algorithm,&key,256,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0);ULONG n{};
        Check(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,nullptr,0,&n,0)>=0);publicKey.cngPublicBlob.resize(n);
        Check(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,publicKey.cngPublicBlob.data(),n,&n,0)>=0);}
    ~TestSigner(){BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(algorithm,0);}
    std::string Sign(std::string_view payload){auto digest=ReleaseDigest(payload);std::array<unsigned char,64> signature{};ULONG n{};
        Check(BCryptSignHash(key,nullptr,digest.data(),static_cast<ULONG>(digest.size()),signature.data(),static_cast<ULONG>(signature.size()),&n,0)>=0&&n==64);
        return "{\"scheme\":\"ecdsa-p256-sha256\",\"keyId\":\"ephemeral-test-only\",\"payloadHex\":"+noven::raid::json::Quote(Hex(std::span(reinterpret_cast<const unsigned char*>(payload.data()),payload.size())))+",\"signatureHex\":"+noven::raid::json::Quote(Hex(signature))+"}";}
};
std::string Replace(std::string s,std::string_view from,std::string_view to){const auto at=s.find(from);Check(at!=s.npos);s.replace(at,from.size(),to);return s;}
ReleaseFile File(const std::filesystem::path& root,std::string name,std::string bytes,std::uint64_t& packOffset){
    Check(noven::resources::WriteResourceText(root/name,bytes));ReleaseFile f{name,noven::resources::ResourceHash(root/name),bytes.size(),{}};
    const auto step=f.size<ChunkThreshold?f.size:ChunkBytes;
    for(std::uint64_t offset=0;offset<f.size;offset+=step){const auto n=std::min(step,f.size-offset);f.content.push_back({offset,n,packOffset,HashFileRange(root/name,offset,n),"core.pack"});packOffset+=n;}return f;
}
}
int main()try {
    TestSigner signer;const std::array keys{signer.publicKey};std::uint64_t pack{};
    const auto temp=std::filesystem::temp_directory_path()/("noven-update-manifest-"+std::to_string(GetCurrentProcessId()));
    const auto old=temp/"old",target=temp/"target",cache=temp/"cache";Check(noven::resources::ResourceDirectories(cache));
    ReleaseManifest m{"0.1.1","2026-10-10T00:00:00Z","Offline only",{}};
    m.files.push_back(File(target,"unchanged.txt","unchanged",pack));m.files.push_back(File(target,"changed.txt","new small bytes",pack));
    auto large=std::string(static_cast<std::size_t>(3*ChunkBytes),'a');m.files.push_back(File(target,"large.bin",large,pack));m.files.push_back(File(target,"new.txt","new",pack));
    Check(noven::resources::WriteResourceText(old/"unchanged.txt","unchanged"));Check(noven::resources::WriteResourceText(old/"changed.txt","old"));
    large[static_cast<std::size_t>(ChunkBytes)]='b';Check(noven::resources::WriteResourceText(old/"large.bin",large));Check(noven::resources::WriteResourceText(old/"obsolete.txt","removed"));
    const auto text=EncodeReleaseManifest(m),envelope=signer.Sign(text);const auto authenticated=VerifyRelease(envelope,keys);
    Check(authenticated.Manifest().version=="0.1.1"&&authenticated.Identity().size()==64);
    const auto plan=PlanUpdate("0.1.0",old,cache,authenticated);
    Check(plan.unchangedFiles==1&&plan.reusedBytes==2*ChunkBytes+9&&plan.downloadBytes==ChunkBytes+18&&plan.files.size()==4);
    Check(plan.fullBytes==plan.reusedBytes+plan.downloadBytes&&plan.downloadBytes<plan.fullBytes&&plan.requiredFreeBytes>plan.fullBytes);
    Check(noven::resources::WriteResourceText(cache/m.files[1].sha256,"new small bytes"));const auto cached=PlanUpdate("0.1.0",old,cache,authenticated);Check(cached.downloadBytes==plan.downloadBytes-15);
    Check(noven::resources::WriteResourceText(cache/m.files[1].sha256,"poison"));Check(PlanUpdate("0.1.0",old,cache,authenticated).downloadBytes==plan.downloadBytes);
    Reject([&]{VerifyRelease(text,keys);});Reject([&]{VerifyRelease(envelope,{});});TestSigner wrong;const std::array wrongKeys{ReleasePublicKey{signer.publicKey.id,wrong.publicKey.cngPublicBlob}};
    Reject([&]{VerifyRelease(envelope,wrongKeys);});Reject([&]{VerifyRelease(Replace(envelope,"ecdsa-p256-sha256","sha256"),keys);});
    auto modified=envelope;const auto at=modified.find("signatureHex\":\"")+15;modified[at]=modified[at]=='a'?'b':'a';Reject([&]{VerifyRelease(modified,keys);});
    Reject([&]{VerifyRelease(signer.Sign(Replace(text,"\"schemaVersion\":1","\"schemaVersion\":2")),keys);});
    Reject([&]{ParseReleaseManifest(Replace(text,"0.1.1","../0.1.1"));});Reject([&]{ParseReleaseManifest(Replace(text,"unchanged.txt","../evil.exe"));});
    Reject([&]{ParseReleaseManifest(Replace(text,"\"pack\":\"core.pack\"","\"pack\":\"https://evil.test/a.pack\""));});
    Reject([&]{ParseReleaseManifest(Replace(text,"new.txt","changed.txt"));});Reject([&]{ParseReleaseManifest(Replace(text,"new.txt","CHANGED.TXT"));});
    Reject([&]{ParseReleaseManifest(Replace(text,"new.txt","changed.txt/child"));});
    Reject([&]{ParseReleaseManifest(Replace(text,"\"offset\":0","\"offset\":1"));});Reject([&]{ParseReleaseManifest(Replace(text,m.files[0].sha256,std::string(64,'g')));});
    Reject([&]{PlanUpdate("0.1.1",old,cache,authenticated);});Reject([&]{PlanUpdate("0.2.0",old,cache,authenticated);});
    for(const auto bad:{"/root.exe","C:/root.exe","a/../b","a\\b","aux.txt","x/nul/a","a/","a//b","a./b","a:stream"})Check(!ValidReleasePath(bad));
    Check(CompareReleaseVersions("0.1.10","0.1.9")>0&&CompareReleaseVersions("1.0.0","1.0.0")==0&&!ValidReleaseVersion("0.1.1-beta")&&!ValidReleaseVersion("01.1.0"));
    auto forward=Replace(text,"\"schemaVersion\":1","\"future\":{\"command\":\"never run\"},\"schemaVersion\":1");Check(VerifyRelease(signer.Sign(forward),keys).Manifest().files.size()==4);
    std::cout<<"signed manifest/delta/security PASS; full="<<plan.fullBytes<<" reuse="<<plan.reusedBytes<<" download="<<plan.downloadBytes<<"\n";
    Check(noven::resources::RemoveResourceTree(temp.parent_path(),temp));return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
