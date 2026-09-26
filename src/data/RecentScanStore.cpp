#include "data/RecentScanStore.h"

#include "common/DebugLog.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace noven::data {
namespace {

bool ValidUtf8(const std::string& text) {
    return text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}

void AppendUtf8(std::string& text, unsigned codepoint) {
    if (codepoint <= 0x7F) text.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7FF) {
        text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

class Reader final {
public:
    explicit Reader(std::string_view input) : input_(input) {}
    bool End() { Skip(); return position_ == input_.size(); }
    bool Take(char c) {
        Skip();
        if (position_ == input_.size() || input_[position_] != c) return false;
        ++position_;
        return true;
    }
    bool Key(std::string_view key) {
        std::string actual;
        return String(actual) && actual == key && Take(':');
    }
    bool Literal(std::string_view value) {
        Skip();
        if (input_.substr(position_, value.size()) != value) return false;
        position_ += value.size();
        return true;
    }
    bool String(std::string& output) {
        if (!Take('"')) return false;
        output.clear();
        while (position_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') return ValidUtf8(output);
            if (c < 0x20) return false;
            if (c != '\\') { output.push_back(static_cast<char>(c)); continue; }
            if (position_ == input_.size()) return false;
            switch (input_[position_++]) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'u': {
                unsigned codepoint = 0;
                if (!Hex4(codepoint)) return false;
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                    if (!Take('\\') || !Take('u')) return false;
                    unsigned low = 0;
                    if (!Hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + low - 0xDC00;
                } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) return false;
                AppendUtf8(output, codepoint);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    template<class T> bool Number(T& output) {
        Skip();
        const char* begin = input_.data() + position_;
        const char* end = input_.data() + input_.size();
        const auto parsed = std::from_chars(begin, end, output);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) return false;
        position_ += static_cast<std::size_t>(parsed.ptr - begin);
        return true;
    }
    bool Boolean(bool& output) {
        if (Literal("true")) { output = true; return true; }
        if (Literal("false")) { output = false; return true; }
        return false;
    }
    template<class T> bool OptionalNumber(std::optional<T>& output) {
        if (Literal("null")) { output.reset(); return true; }
        T value{};
        if (!Number(value)) return false;
        output = value;
        return true;
    }
private:
    void Skip() {
        while (position_ < input_.size() && (input_[position_] == ' '
            || input_[position_] == '\n' || input_[position_] == '\r'
            || input_[position_] == '\t')) ++position_;
    }
    bool Hex4(unsigned& output) {
        for (int i = 0; i < 4; ++i) {
            if (position_ == input_.size()) return false;
            const char c = input_[position_++];
            output <<= 4;
            if (c >= '0' && c <= '9') output += static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') output += static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') output += static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    std::string_view input_;
    std::size_t position_{};
};

std::string_view ModeCode(GameMode mode) {
    switch (mode) {
    case GameMode::Pvp: return "pvp";
    case GameMode::Pve: return "pve";
    case GameMode::Seasonal: return "seasonal";
    }
    return "pvp";
}
std::string_view StatusCode(FleaStatus status) {
    switch (status) {
    case FleaStatus::Allowed: return "allowed";
    case FleaStatus::Banned: return "banned";
    case FleaStatus::LockedOrUnavailable: return "locked";
    case FleaStatus::Unknown: return "unknown";
    }
    return "unknown";
}
bool DecodeMode(std::string_view code, GameMode& mode) {
    if (code == "pvp") mode = GameMode::Pvp;
    else if (code == "pve") mode = GameMode::Pve;
    else if (code == "seasonal") mode = GameMode::Seasonal;
    else return false;
    return true;
}
bool DecodeStatus(std::string_view code, FleaStatus& status) {
    if (code == "allowed") status = FleaStatus::Allowed;
    else if (code == "banned") status = FleaStatus::Banned;
    else if (code == "locked") status = FleaStatus::LockedOrUnavailable;
    else if (code == "unknown") status = FleaStatus::Unknown;
    else return false;
    return true;
}
bool ValidEntry(const RecentScanEntry& entry) {
    return entry.scanId != 0 && !entry.stableItemId.empty()
        && !entry.canonicalName.empty() && entry.scannedAtUnixMs > 0
        && entry.itemWidth >= 0 && entry.itemHeight >= 0
        && ValidUtf8(entry.stableItemId) && ValidUtf8(entry.canonicalName)
        && ValidUtf8(entry.canonicalShortName) && ValidUtf8(entry.bestTraderName)
        && (!entry.fleaPrice || *entry.fleaPrice >= 0)
        && (!entry.bestTraderPrice || *entry.bestTraderPrice >= 0)
        && (!entry.valuePerSlot || (std::isfinite(*entry.valuePerSlot)
            && *entry.valuePerSlot >= 0));
}
bool ReadEntry(Reader& json, RecentScanEntry& entry) {
    std::string mode, match, status;
    return json.Take('{') && json.Key("scanId") && json.Number(entry.scanId)
        && json.Take(',') && json.Key("stableItemId") && json.String(entry.stableItemId)
        && json.Take(',') && json.Key("canonicalName") && json.String(entry.canonicalName)
        && json.Take(',') && json.Key("canonicalShortName") && json.String(entry.canonicalShortName)
        && json.Take(',') && json.Key("gameMode") && json.String(mode) && DecodeMode(mode, entry.gameMode)
        && json.Take(',') && json.Key("matchMode") && json.String(match)
        && ((match == "strict" && (entry.matchMode = RecentMatchMode::Strict, true))
            || (match == "best_effort" && (entry.matchMode = RecentMatchMode::BestEffort, true)))
        && json.Take(',') && json.Key("ambiguous") && json.Boolean(entry.ambiguous)
        && json.Take(',') && json.Key("scannedAtUnixMs") && json.Number(entry.scannedAtUnixMs)
        && json.Take(',') && json.Key("fleaPrice") && json.OptionalNumber(entry.fleaPrice)
        && json.Take(',') && json.Key("bestTraderPrice") && json.OptionalNumber(entry.bestTraderPrice)
        && json.Take(',') && json.Key("bestTraderName") && json.String(entry.bestTraderName)
        && json.Take(',') && json.Key("valuePerSlot") && json.OptionalNumber(entry.valuePerSlot)
        && json.Take(',') && json.Key("fleaStatus") && json.String(status)
        && DecodeStatus(status, entry.fleaStatus)
        && json.Take(',') && json.Key("itemWidth") && json.Number(entry.itemWidth)
        && json.Take(',') && json.Key("itemHeight") && json.Number(entry.itemHeight)
        && json.Take('}') && ValidEntry(entry);
}
bool ParseHistory(std::string_view input, std::vector<RecentScanEntry>& entries) {
    Reader json(input);
    int version = 0;
    if (!json.Take('{') || !json.Key("schemaVersion") || !json.Number(version)
        || version != 1 || !json.Take(',') || !json.Key("entries")
        || !json.Take('[')) return false;
    std::unordered_set<std::uint64_t> ids;
    if (!json.Take(']')) {
        do {
            RecentScanEntry entry;
            if (!ReadEntry(json, entry) || !ids.insert(entry.scanId).second) return false;
            if (entries.size() < RecentScanStore::kMaxEntries)
                entries.push_back(std::move(entry));
            if (json.Take(']')) break;
            if (!json.Take(',')) return false;
        } while (true);
    }
    return json.Take('}') && json.End();
}
std::string Escape(std::string_view text) {
    std::string output = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { output.push_back('\\'); output.push_back(static_cast<char>(c)); }
        else if (c < 0x20) {
            output += "\\u00";
            output.push_back(hex[c >> 4]); output.push_back(hex[c & 15]);
        } else output.push_back(static_cast<char>(c));
    }
    output.push_back('"');
    return output;
}
template<class T> void WriteOptional(std::ostream& output, const std::optional<T>& value) {
    if (value) output << *value;
    else output << "null";
}
bool SaveHistory(const std::filesystem::path& path,
                 const std::vector<RecentScanEntry>& entries) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << std::setprecision(17) << "{\"schemaVersion\":1,\"entries\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (i != 0) output << ',';
        output << "{\"scanId\":" << e.scanId
               << ",\"stableItemId\":" << Escape(e.stableItemId)
               << ",\"canonicalName\":" << Escape(e.canonicalName)
               << ",\"canonicalShortName\":" << Escape(e.canonicalShortName)
               << ",\"gameMode\":" << Escape(ModeCode(e.gameMode))
               << ",\"matchMode\":" << Escape(e.matchMode == RecentMatchMode::Strict
                   ? "strict" : "best_effort")
               << ",\"ambiguous\":" << (e.ambiguous ? "true" : "false")
               << ",\"scannedAtUnixMs\":" << e.scannedAtUnixMs
               << ",\"fleaPrice\":";
        WriteOptional(output, e.fleaPrice);
        output << ",\"bestTraderPrice\":";
        WriteOptional(output, e.bestTraderPrice);
        output << ",\"bestTraderName\":" << Escape(e.bestTraderName)
               << ",\"valuePerSlot\":";
        WriteOptional(output, e.valuePerSlot);
        output << ",\"fleaStatus\":" << Escape(StatusCode(e.fleaStatus))
               << ",\"itemWidth\":" << e.itemWidth
               << ",\"itemHeight\":" << e.itemHeight << '}';
    }
    output << "]}";
    output.flush();
    if (!output) return false;
    output.close();
    return MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
} // namespace

RecentScanStore::~RecentScanStore() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_one();
    if (writer_.joinable()) writer_.join();
}

bool RecentScanStore::Load(const std::filesystem::path& path, std::wstring& error) {
    path_ = path;
    std::vector<RecentScanEntry> loaded;
    bool valid = true;
    if (std::filesystem::exists(path)) {
        std::ifstream input(path, std::ios::binary);
        if (!input) valid = false;
        else {
            const std::string text{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
            valid = !input.bad() && ParseHistory(text, loaded);
        }
    }
    if (!valid) error = L"Recent scan history is malformed or unreadable";
    else entries_ = std::move(loaded);
    writer_ = std::thread(&RecentScanStore::PersistLoop, this);
    return valid;
}

bool RecentScanStore::Append(RecentScanEntry entry) {
    if (!ValidEntry(entry)) return false;
    {
        std::lock_guard lock(mutex_);
        // 递增历史 ID 同时阻止已从 200 条窗口淘汰的旧结果再次插入。
        // Monotonic IDs also reject duplicates already evicted from the 200-row window.
        if (!entries_.empty() && entry.scanId <= (std::max_element)(entries_.begin(),
            entries_.end(), [](const auto& a, const auto& b) {
                return a.scanId < b.scanId;
            })->scanId) return false;
        entries_.insert(entries_.begin(), std::move(entry));
        if (entries_.size() > kMaxEntries) entries_.resize(kMaxEntries);
        ++revision_;
    }
    changed_.notify_one();
    return true;
}

std::vector<RecentScanEntry> RecentScanStore::Snapshot() const {
    std::lock_guard lock(mutex_);
    return entries_;
}

std::uint64_t RecentScanStore::MaxScanId() const {
    std::lock_guard lock(mutex_);
    std::uint64_t maximum = 0;
    for (const auto& entry : entries_) maximum = (std::max)(maximum, entry.scanId);
    return maximum;
}

void RecentScanStore::PersistLoop() {
    std::uint64_t written = 0;
    for (;;) {
        std::vector<RecentScanEntry> snapshot;
        std::uint64_t revision = 0;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] { return stopping_ || revision_ != written; });
            if (stopping_ && revision_ == written) return;
            snapshot = entries_;
            revision = revision_;
        }
        // 不在锁内写盘，也不让 F2 的 UI 消息处理等待磁盘。
        // Persist outside the mutex so append and UI snapshots never wait on disk.
        if (!SaveHistory(path_, snapshot)) {
            common::DebugLog(L"[recent-scans] could not persist history");
        }
        written = revision;
    }
}

} // namespace noven::data
