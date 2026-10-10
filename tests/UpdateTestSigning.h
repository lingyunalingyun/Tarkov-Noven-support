#pragma once
#include "updates/ReleaseManifest.h"
#include "raid/RaidJson.h"
#include <bcrypt.h>
#include <array>
namespace noven::tests {
// 每个离线测试生成临时密钥；私钥不写盘、不链接进产品。
// Generate ephemeral keys per offline test; never write/link private keys into products.
struct UpdateSigner final {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE key{};updates::ReleasePublicKey publicKey{"ephemeral-test-only",{}};
    static void Require(bool value){if(!value)throw std::runtime_error("test signing failed");}
    UpdateSigner(){Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0);
        Require(BCryptGenerateKeyPair(algorithm,&key,256,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0);ULONG n{};
        Require(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,nullptr,0,&n,0)>=0);publicKey.cngPublicBlob.resize(n);
        Require(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,publicKey.cngPublicBlob.data(),n,&n,0)>=0);}
    ~UpdateSigner(){BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(algorithm,0);}
    std::string Sign(std::string_view payload){auto digest=updates::ReleaseDigest(payload);std::array<unsigned char,64> signature{};ULONG n{};
        Require(BCryptSignHash(key,nullptr,digest.data(),static_cast<ULONG>(digest.size()),signature.data(),static_cast<ULONG>(signature.size()),&n,0)>=0&&n==64);
        return "{\"scheme\":\"ecdsa-p256-sha256\",\"keyId\":\"ephemeral-test-only\",\"payloadHex\":"+raid::json::Quote(updates::Hex(std::span(reinterpret_cast<const unsigned char*>(payload.data()),payload.size())))+",\"signatureHex\":"+raid::json::Quote(updates::Hex(signature))+"}";}
};
}
