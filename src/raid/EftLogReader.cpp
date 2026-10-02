#include "raid/EftLogReader.h"

namespace noven::raid {
namespace {
HANDLE OpenShared(const std::filesystem::path& path) {
    return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
}
bool Info(HANDLE file, std::string& identity, std::uint64_t& size) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info)) return false;
    identity = std::to_string(info.dwVolumeSerialNumber) + "-"
        + std::to_string(info.nFileIndexHigh) + "-" + std::to_string(info.nFileIndexLow) + "-"
        + std::to_string(info.ftCreationTime.dwHighDateTime) + "-" + std::to_string(info.ftCreationTime.dwLowDateTime);
    size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return true;
}
bool Utf8(const std::string& line) {
    return line.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        line.data(), static_cast<int>(line.size()), nullptr, 0) > 0;
}
}
EftLogReader::~EftLogReader() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
std::string EftLogReader::SourceIdentity() const {
    return cursor_.fileIdentity + ":" + std::to_string(cursor_.generation);
}
bool EftLogReader::Open(const std::filesystem::path& path, const std::optional<LogCursor>& saved) {
    HANDLE next = OpenShared(path);
    if (next == INVALID_HANDLE_VALUE) return false;
    std::string identity; std::uint64_t size{};
    if (!Info(next, identity, size)) { CloseHandle(next); return false; }
    LogCursor cursor{path, identity, 0, saved ? saved->generation : 0};
    if (saved && saved->fileIdentity == identity && saved->offset <= size) cursor.offset = saved->offset;
    else if (saved) { ++cursor.generation; reset_ = true; }
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(cursor.offset);
    if (!SetFilePointerEx(next, position, nullptr, FILE_BEGIN)) { CloseHandle(next); return false; }
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    file_ = next; cursor_ = std::move(cursor); position_ = cursor_.offset; observedSize_ = size;
    begin_ = end_ = 0; tail_.clear(); dropping_ = false;
    return true;
}
bool EftLogReader::Refresh() {
    HANDLE probe = OpenShared(cursor_.path);
    if (probe == INVALID_HANDLE_VALUE) return false;
    std::string identity; std::uint64_t size{};
    const bool ok = Info(probe, identity, size); CloseHandle(probe);
    if (!ok) return false;
    if (identity != cursor_.fileIdentity || size < observedSize_ || size < position_) {
        auto saved = cursor_;
        if (identity == saved.fileIdentity) { saved.offset = 0; ++saved.generation; }
        reset_ = true; return Open(cursor_.path, saved);
    }
    observedSize_ = size; return true;
}
ReadResult EftLogReader::NextLine(std::string& line, std::uint64_t& offset) {
    line.clear(); offset = cursor_.offset;
    for (;;) {
        if (begin_ == end_) {
            DWORD count{};
            if (!ReadFile(file_, buffer_.data(), static_cast<DWORD>(buffer_.size()), &count, nullptr))
                return ReadResult::Error;
            begin_ = 0; end_ = count; bytesRead_ += count;
            if (!count) return ReadResult::End;
        }
        const char c = buffer_[begin_++]; ++position_;
        if (c == '\n') {
            const bool skipped = dropping_; dropping_ = false;
            line.swap(tail_); tail_.clear(); cursor_.offset = position_;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (offset == 0 && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
            if (skipped || !Utf8(line)) { line.clear(); return ReadResult::Skipped; }
            return ReadResult::Line;
        }
        if (!dropping_) {
            if (tail_.size() == 65536) { tail_.clear(); dropping_ = true; }
            else tail_ += c;
        }
    }
}
}
