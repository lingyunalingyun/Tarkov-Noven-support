#pragma once
#include "updates/ReleaseManifest.h"
#include <filesystem>
#include <functional>
namespace noven::updates {
enum class ContentSource {LocalFile,Cache,Download};
struct PlannedRange final {ContentRange content;ContentSource source;std::filesystem::path local;};
struct PlannedFile final {ReleaseFile target;std::vector<PlannedRange> ranges;bool unchanged{};};
struct UpdatePlan final {
    std::string version,manifestIdentity;std::vector<PlannedFile> files;
    std::uint64_t fullBytes{},reusedBytes{},downloadBytes{},requiredFreeBytes{};
    std::size_t unchangedFiles{};
};
std::string HashFileRange(const std::filesystem::path&,std::uint64_t offset,std::uint64_t size);
UpdatePlan PlanUpdate(std::string_view currentVersion,const std::filesystem::path& currentRoot,
    const std::filesystem::path& cache,const AuthenticatedRelease& target);
}
