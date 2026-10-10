#include "updates/ReleaseManifest.h"
#include "updates/UpdatePlanner.h"
#include "resources/ResourceFiles.h"
#include "plugins/PluginPipe.h"
#include "raid/RaidJson.h"
#include <bcrypt.h>
#include <fstream>
#include <iostream>
#include <array>
namespace {
using namespace noven;using namespace updates;
void Require(bool ok){if(!ok)throw std::runtime_error("release tool failed; no artifacts published");}
std::string Ascii(std::wstring_view s){std::string out;for(auto c:s){Require(c<128);out+=static_cast<char>(c);}return out;}
struct SigningKey {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE key{};
    SigningKey(){Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0);}
    ~SigningKey(){if(key)BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(algorithm,0);}
    std::string Export(LPCWSTR format){ULONG n{};Require(BCryptExportKey(key,nullptr,format,nullptr,0,&n,0)>=0);std::string out(n,'\0');Require(BCryptExportKey(key,nullptr,format,reinterpret_cast<PUCHAR>(out.data()),n,&n,0)>=0);return out;}
};
void ReviewKey(const std::filesystem::path& root){Require(!std::filesystem::exists(root)&&resources::ResourceDirectories(root));SigningKey key;
    Require(BCryptGenerateKeyPair(key.algorithm,&key.key,256,0)>=0&&BCryptFinalizeKeyPair(key.key,0)>=0);
    Require(resources::WriteResourceText(root/L"review-only.private",key.Export(BCRYPT_ECCPRIVATE_BLOB)));
    const auto publicKey=key.Export(BCRYPT_ECCPUBLIC_BLOB);Require(resources::WriteResourceText(root/L"review-only.public",publicKey));
    std::cout<<"NON-PRODUCTION keyId=review-only-p256 publicHex="<<Hex(std::span(reinterpret_cast<const unsigned char*>(publicKey.data()),publicKey.size()))<<'\n';
}
void OperatorKey(const std::filesystem::path& root,std::string id){
    Require(!id.empty()&&id.size()<=64&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==id.npos&&
        !id.starts_with("review-")&&!id.starts_with("test-")&&!std::filesystem::exists(root)&&resources::ResourceDirectories(root));
    // 操作员预先保护父目录；只生成全新密钥，不覆盖已有的发布身份。
    // Operator protects the parent first; create a new identity, never replace an existing key.
    SigningKey key;Require(BCryptGenerateKeyPair(key.algorithm,&key.key,256,0)>=0&&BCryptFinalizeKeyPair(key.key,0)>=0);
    Require(resources::WriteResourceText(root/L"update.private",key.Export(BCRYPT_ECCPRIVATE_BLOB)));
    const auto publicKey=key.Export(BCRYPT_ECCPUBLIC_BLOB);Require(resources::WriteResourceText(root/L"update.public",publicKey));
    std::cout<<"operator keyId="<<id<<" publicHex="<<Hex(std::span(reinterpret_cast<const unsigned char*>(publicKey.data()),publicKey.size()))<<'\n';
}
void Build(const std::filesystem::path& source,const std::filesystem::path& output,std::string version,std::string id,const std::filesystem::path& privateKey,std::string stamp){
    Require(ValidReleaseVersion(version)&&!id.empty()&&id.size()<=64&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==id.npos);
    Require(resources::SafeResourcePath(source)&&!std::filesystem::exists(output)&&resources::ResourceDirectories(output));
    const auto blob=resources::ReadResourceText(privateKey,104);Require(blob.size()==104);BCRYPT_ECCKEY_BLOB header{};memcpy(&header,blob.data(),sizeof(header));Require(header.dwMagic==BCRYPT_ECDSA_PRIVATE_P256_MAGIC&&header.cbKey==32);
    SigningKey key;Require(BCryptImportKeyPair(key.algorithm,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key.key,reinterpret_cast<PUCHAR>(const_cast<char*>(blob.data())),static_cast<ULONG>(blob.size()),0)>=0);
    std::vector<std::filesystem::path> files;for(const auto& entry:std::filesystem::recursive_directory_iterator(source)){Require(resources::SafeResourcePath(entry.path()));if(entry.is_regular_file())files.push_back(entry.path());Require(files.size()<=MaximumReleaseFiles);}
    std::sort(files.begin(),files.end());ReleaseManifest manifest{version,stamp,"",{}};std::uint64_t packOffset{};
    const auto packName="core-"+version+".pack";plugins::ipc::Handle pack(CreateFileW((output/packName).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));Require(static_cast<bool>(pack));
    std::array<char,65536> buffer{};
    for(const auto& path:files){ReleaseFile f;f.path=path.lexically_relative(source).generic_string();f.size=std::filesystem::file_size(path);Require(ValidReleasePath(f.path)&&f.size<=MaximumReleaseBytes-packOffset);f.sha256=HashFileRange(path,0,f.size);
        const auto step=f.size<ChunkThreshold?f.size:ChunkBytes;std::ifstream input(path,std::ios::binary);Require(static_cast<bool>(input));
        for(std::uint64_t offset=0;offset<f.size||(!f.size&&f.content.empty());offset+=step){const auto n=f.size?std::min(step,f.size-offset):0;
            f.content.push_back({offset,n,packOffset,HashFileRange(path,offset,n),packName});std::uint64_t left=n;
            while(left){const auto size=static_cast<DWORD>(std::min<std::uint64_t>(left,buffer.size()));input.read(buffer.data(),size);Require(input.gcount()==size);DWORD written{};Require(WriteFile(pack.Get(),buffer.data(),size,&written,nullptr)&&written==size);left-=size;}
            packOffset+=n;if(!f.size)break;}manifest.files.push_back(std::move(f));}
    Require(FlushFileBuffers(pack.Get()));pack.Reset();const auto payload=EncodeReleaseManifest(manifest);auto digest=ReleaseDigest(payload);std::array<unsigned char,64> signature{};ULONG n{};
    Require(BCryptSignHash(key.key,nullptr,digest.data(),static_cast<ULONG>(digest.size()),signature.data(),static_cast<ULONG>(signature.size()),&n,0)>=0&&n==64);
    const auto envelope="{\"scheme\":\"ecdsa-p256-sha256\",\"keyId\":"+raid::json::Quote(id)+",\"payloadHex\":"+raid::json::Quote(Hex(std::span(reinterpret_cast<const unsigned char*>(payload.data()),payload.size())))+",\"signatureHex\":"+raid::json::Quote(Hex(signature))+"}";
    const auto publicBlob=key.Export(BCRYPT_ECCPUBLIC_BLOB);const std::array keys{ReleasePublicKey{id,std::vector<unsigned char>(publicBlob.begin(),publicBlob.end())}};
    (void)VerifyRelease(envelope,keys);Require(resources::WriteResourceText(output/L"release.json",envelope));std::cout<<"signed offline release "<<version<<" packBytes="<<packOffset<<" files="<<files.size()<<'\n';
}
}
int wmain(int argc,wchar_t** argv)try {
    if(argc==3&&std::wstring_view(argv[1])==L"--review-key"){ReviewKey(std::filesystem::absolute(argv[2]));return 0;}
    if(argc==4&&std::wstring_view(argv[1])==L"--operator-key"){OperatorKey(std::filesystem::absolute(argv[2]),Ascii(argv[3]));return 0;}
    if(argc!=8||std::wstring_view(argv[1])!=L"--release"){std::cerr<<"--review-key <NEW test-only key directory> OR --operator-key <NEW protected operator directory> <keyId> OR --release <staged version root> <NEW artifact directory> <version> <keyId> <operator private CNG blob> <publishedAt>\n";return 2;}
    Build(std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),Ascii(argv[4]),Ascii(argv[5]),std::filesystem::absolute(argv[6]),Ascii(argv[7]));return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
