#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace noven::plugins {
inline constexpr unsigned StorageSchemaVersion=1,MaximumStorageKeys=1024,MaximumStorageList=64;
inline constexpr std::size_t MaximumStorageValue=32*1024,MaximumStorageBytes=4*1024*1024;
enum class StorageOperation {Get=1,Set=2,Delete=3,List=4};
enum class StorageStatus {Ok,PermissionDenied,NotFound,InvalidRequest,Quota,Corrupt,IoError,Unavailable};
struct StorageRequest final {
    std::uint64_t requestId{};
    StorageOperation operation{StorageOperation::Get};
    std::string key,value;
    std::uint32_t offset{},limit{};
};
struct StorageResult final {
    std::uint64_t requestId{};
    StorageStatus status{StorageStatus::Ok};
    std::string value;
    std::vector<std::string> keys;
    std::uint32_t total{},nextOffset{};
};
// 键按 UTF-8 原字节精确比较，不归一化/折叠大小写，也不解释为路径。
// Keys compare exact UTF-8 bytes without normalization/case folding or path interpretation.
bool ValidStorageKey(std::string_view key);
bool ValidStorageRequest(const StorageRequest& request);
bool ValidStorageResult(const StorageResult& result);
std::string EncodeStorageBytes(std::string_view bytes);
std::string DecodeStorageBytes(std::string_view base64);
}
