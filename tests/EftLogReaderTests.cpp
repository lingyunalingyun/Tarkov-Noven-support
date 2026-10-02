#include "raid/EftLogReader.h"
#include "raid/RaidEventParser.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
using namespace noven::raid;
void Require(bool ok, const char* msg) { if (!ok) { std::cerr << msg << '\n'; std::exit(1); } }
void Write(const std::filesystem::path& path, std::string_view text, bool append = false) {
    std::ofstream file(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc)); file << text;
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("noven-reader-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root); const auto path = root / "synthetic.log";
    Write(path, "\xEF\xBB\xBFSession mode: Pve\r\nGameSta");
    LogCursor saved;
    {
        EftLogReader reader; Require(reader.Open(path), "open");
        std::string line; std::uint64_t offset{};
        Require(reader.NextLine(line, offset) == ReadResult::Line && ParseRaidEvents(line).size() == 1, "BOM/CRLF");
        saved = reader.Cursor();
        Require(reader.NextLine(line, offset) == ReadResult::End && reader.Cursor().offset == saved.offset, "partial not committed");
        Write(path, "rted\n中文\n", true); Require(reader.Refresh(), "append refresh");
        Require(reader.NextLine(line, offset) == ReadResult::Line && line == "GameStarted", "split line joined");
        Require(reader.NextLine(line, offset) == ReadResult::Line && line == "中文", "UTF-8");
        Write(path, "short\n"); Require(reader.Refresh() && reader.TakeSourceReset(), "truncate reset");
        Require(reader.NextLine(line, offset) == ReadResult::Line && line == "short", "truncated new generation");
    }
    Write(path, "first\nGameSta");
    { EftLogReader r; Require(r.Open(path), "open restart seed"); std::string l; std::uint64_t p;
      r.NextLine(l,p); r.NextLine(l,p); saved = r.Cursor(); }
    Write(path, "rted\n", true);
    { EftLogReader r; Require(r.Open(path,saved), "resume"); std::string l; std::uint64_t p;
      Require(r.NextLine(l,p) == ReadResult::Line && l == "GameStarted", "restart reads partial from complete boundary");
      const auto old = r.SourceIdentity();
      std::filesystem::rename(path,root/"old.log"); Write(path,"new source\n");
      Require(r.Refresh() && r.TakeSourceReset() && old != r.SourceIdentity(), "replacement identity"); }
    Write(path, std::string(200000,'x') + "\nGameStarted\n");
    { EftLogReader r; r.Open(path); std::string l; std::uint64_t p;
      Require(r.NextLine(l,p) == ReadResult::Skipped && r.BufferedBytes() <= 81920, "oversized line bounded");
      Require(r.NextLine(l,p) == ReadResult::Line && l == "GameStarted", "recover after oversized line"); }
    Write(path, std::string(8*1024*1024,'x') + "\n");
    { EftLogReader r; r.Open(path); std::string l; std::uint64_t p; r.NextLine(l,p); r.NextLine(l,p);
      const auto read = r.BytesRead(); Write(path,"GameStarted\n",true); r.Refresh(); r.NextLine(l,p);
      Require(r.BytesRead()-read == 12, "large already-read file only reads 12-byte append");
      std::cout << "8 MiB baseline, append bytes read: " << r.BytesRead()-read << '\n'; }
    std::filesystem::remove_all(root);
}
