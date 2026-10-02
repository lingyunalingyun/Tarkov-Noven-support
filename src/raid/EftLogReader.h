#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace noven::raid {
struct LogCursor final {
    std::filesystem::path path;
    std::string fileIdentity;
    std::uint64_t offset{}, generation{};
};
enum class ReadResult { Line, End, Skipped, Error };
// 一个读取器拥有一个文件句柄；只提交完整行偏移，尾部片段仅暂存内存。
// Each reader owns one file handle; only complete-line offsets commit, tails stay in memory.
class EftLogReader final {
public:
    ~EftLogReader();
    EftLogReader() = default;
    EftLogReader(const EftLogReader&) = delete;
    EftLogReader& operator=(const EftLogReader&) = delete;
    bool Open(const std::filesystem::path& path, const std::optional<LogCursor>& saved = {});
    bool Refresh();
    ReadResult NextLine(std::string& line, std::uint64_t& offset);
    const LogCursor& Cursor() const noexcept { return cursor_; }
    std::string SourceIdentity() const;
    bool TakeSourceReset() noexcept { const bool reset = reset_; reset_ = false; return reset; }
    std::uint64_t BytesRead() const noexcept { return bytesRead_; }
    std::size_t BufferedBytes() const noexcept { return tail_.size() + end_ - begin_; }
private:
    HANDLE file_{INVALID_HANDLE_VALUE};
    LogCursor cursor_;
    std::array<char, 16384> buffer_{};
    std::size_t begin_{}, end_{};
    std::uint64_t position_{}, observedSize_{}, bytesRead_{};
    std::string tail_;
    bool dropping_{}, reset_{};
};
}
