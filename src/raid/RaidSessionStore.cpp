#include "raid/RaidSessionStore.h"
#include "raid/RaidJson.h"
#include <fstream>
#include <set>
#include <sstream>

namespace noven::raid {
namespace {
using json::Value;
constexpr std::uintmax_t kMaximumFile = 64*1024*1024;
std::string Utf8(const std::filesystem::path& p) { const auto value=p.u8string();return {value.begin(),value.end()}; }
std::filesystem::path Path(const Value& v) {
    const auto& text=v.String();if(text.size()>32768||text.find('\0')!=text.npos)throw std::runtime_error("invalid path");
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
template<class E> E Enum(const Value& v,int max) {const auto n=v.Int();if(n<0||n>max)throw std::runtime_error("invalid enum");return static_cast<E>(n);}
std::uint64_t Unsigned(const Value& v) {const auto n=v.Int();if(n<0)throw std::runtime_error("negative cursor");return static_cast<std::uint64_t>(n);}
std::optional<std::int64_t> Optional(const Value& v) {if(v.type==Value::Type::Null)return {};const auto n=v.Int();if(n<0||n>253402300799999LL)throw std::runtime_error("invalid time");return n;}
std::string Identity(const Value& v,bool empty=false) {
    auto text=v.String();if((text.empty()&&!empty)||text.size()>256
        ||text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:-")!=text.npos)
        throw std::runtime_error("invalid identity");return text;
}
RaidSession Session(const Value& v) {
    RaidSession s;s.localSessionId=Identity(v.At("localSessionId"),true);s.eftRaidId=Identity(v.At("eftRaidId"),true);
    s.mapId=Identity(v.At("mapId"),true);s.gameMode=Enum<GameMode>(v.At("gameMode"),4);
    s.raidType=Enum<RaidType>(v.At("raidType"),2);s.outcome=Enum<RaidOutcome>(v.At("outcome"),5);
    s.startedAt=Optional(v.At("startedAt"));s.endedAt=Optional(v.At("endedAt"));s.duration=Optional(v.At("duration"));
    s.startObserved=v.At("startObserved").Bool();s.endObserved=v.At("endObserved").Bool();
    s.sourceInterrupted=v.At("sourceInterrupted").Bool();
    if(v.At("parserVersion").Int()!=kParserVersion)throw std::runtime_error("unsupported parser version");
    s.parserVersion=kParserVersion;
    s.startSource=Identity(v.At("startSource"),true);s.startOffset=Unsigned(v.At("startOffset"));
    if(s.parserVersion!=kParserVersion||(s.startedAt&&!s.startObserved)||(s.endedAt&&!s.endObserved)
        ||(s.startedAt&&s.endedAt&&*s.endedAt<*s.startedAt)
        ||(s.duration&&(!s.startedAt||!s.endedAt||*s.duration!=*s.endedAt-*s.startedAt)))
        throw std::runtime_error("invalid session completeness");return s;
}
std::string Number(const std::optional<std::int64_t>& v) {return v?std::to_string(*v):"null";}
std::string SessionJson(const RaidSession& s) {
    std::ostringstream o;o<<"{\"localSessionId\":"<<json::Quote(s.localSessionId)<<",\"eftRaidId\":"<<json::Quote(s.eftRaidId)
        <<",\"mapId\":"<<json::Quote(s.mapId)<<",\"gameMode\":"<<static_cast<int>(s.gameMode)
        <<",\"raidType\":"<<static_cast<int>(s.raidType)<<",\"outcome\":"<<static_cast<int>(s.outcome)
        <<",\"startedAt\":"<<Number(s.startedAt)<<",\"endedAt\":"<<Number(s.endedAt)<<",\"duration\":"<<Number(s.duration)
        <<",\"startObserved\":"<<(s.startObserved?"true":"false")<<",\"endObserved\":"<<(s.endObserved?"true":"false")
        <<",\"sourceInterrupted\":"<<(s.sourceInterrupted?"true":"false")<<",\"parserVersion\":"<<s.parserVersion
        <<",\"startSource\":"<<json::Quote(s.startSource)<<",\"startOffset\":"<<s.startOffset<<'}';return o.str();
}
RaidCheckpoint Decode(std::string_view text) {
    const auto root=json::Parser(text).Parse();if(root.At("schemaVersion").Int()!=1)throw std::runtime_error("unsupported raid schema");
    RaidCheckpoint cp;auto& d=cp.detector;const auto& v=root.At("detector");
    d.state=Enum<SessionState>(v.At("state"),3);d.mode=Enum<GameMode>(v.At("mode"),4);
    if(v.At("preparing").type!=Value::Type::Null)d.preparing=Session(v.At("preparing"));
    if(v.At("active").type!=Value::Type::Null)d.active=Session(v.At("active"));
    d.settlementSessionId=Identity(v.At("settlementSessionId"),true);d.replayingCompleted=v.At("replayingCompleted").Bool();
    std::set<std::string> ids,eftIds;
    const auto& sessions=v.At("completed").Array();if(sessions.size()>100000)throw std::runtime_error("history capacity exceeded");
    for(const auto& item:sessions) {auto s=Session(item);
        if(!s.startObserved||!s.endObserved||s.localSessionId.empty()||!ids.insert(s.localSessionId).second
            ||(!s.eftRaidId.empty()&&!eftIds.insert(s.eftRaidId).second))throw std::runtime_error("invalid duplicate/completed session");
        d.completed.push_back(std::move(s));}
    if(d.active&&(!d.active->startObserved||d.active->endObserved||d.active->localSessionId.empty()
        ||!ids.insert(d.active->localSessionId).second))throw std::runtime_error("invalid active session");
    if(d.state==SessionState::ActiveRaid&&(!d.active||d.active->sourceInterrupted))throw std::runtime_error("missing active state");
    if(!d.settlementSessionId.empty()&&!ids.contains(d.settlementSessionId))throw std::runtime_error("invalid settlement reference");
    cp.sourceGroup=Path(root.At("sourceGroup"));
    const auto& cursors=root.At("cursors").Array();if(cursors.size()>4096)throw std::runtime_error("source capacity exceeded");
    std::set<std::filesystem::path> paths;
    for(const auto& item:cursors) {LogCursor c;c.path=Path(item.At("path"));
        c.fileIdentity=Identity(item.At("fileIdentity"));c.offset=Unsigned(item.At("offset"));c.generation=Unsigned(item.At("generation"));
        if(c.path.empty()||!paths.insert(c.path).second)throw std::runtime_error("invalid cursor path");cp.cursors.push_back(std::move(c));}
    return cp;
}
std::string Encode(const RaidCheckpoint& cp) {
    if(cp.detector.completed.size()>100000||cp.cursors.size()>4096)throw std::runtime_error("raid store capacity exceeded");
    const auto& d=cp.detector;std::ostringstream o;o<<"{\"schemaVersion\":1,\"sourceGroup\":"<<json::Quote(Utf8(cp.sourceGroup))
        <<",\"detector\":{\"state\":"<<static_cast<int>(d.state)<<",\"mode\":"<<static_cast<int>(d.mode)
        <<",\"preparing\":"<<(d.preparing?SessionJson(*d.preparing):"null")<<",\"active\":"<<(d.active?SessionJson(*d.active):"null")
        <<",\"settlementSessionId\":"<<json::Quote(d.settlementSessionId)<<",\"replayingCompleted\":"<<(d.replayingCompleted?"true":"false")<<",\"completed\":[";
    bool comma=false;for(const auto& s:d.completed) {if(comma)o<<',';comma=true;o<<SessionJson(s);}o<<"]},\"cursors\":[";
    comma=false;for(const auto& c:cp.cursors) {if(comma)o<<',';comma=true;o<<"{\"path\":"<<json::Quote(Utf8(c.path))
        <<",\"fileIdentity\":"<<json::Quote(c.fileIdentity)<<",\"offset\":"<<c.offset<<",\"generation\":"<<c.generation<<'}';}
    o<<"]}";return o.str();
}
}
RaidSessionStore::~RaidSessionStore() {if(lease_!=INVALID_HANDLE_VALUE)CloseHandle(lease_);}
bool RaidSessionStore::Load(const std::filesystem::path& file,RaidCheckpoint& checkpoint,std::string& error) {
    writable_=false;file_=file;error.clear();
    if(lease_!=INVALID_HANDLE_VALUE) {CloseHandle(lease_);lease_=INVALID_HANDLE_VALUE;}
    try {
        std::filesystem::create_directories(file.parent_path());
        lease_=CreateFileW((file.wstring()+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lease_==INVALID_HANDLE_VALUE)throw std::runtime_error("raid store already owned/unavailable");
        RaidCheckpoint loaded;
        if(std::filesystem::exists(file)) {
            const auto size=std::filesystem::file_size(file);if(size>kMaximumFile)throw std::runtime_error("raid store exceeds capacity");
            std::ifstream in(file,std::ios::binary);std::string text(static_cast<std::size_t>(size),'\0');
            if(!in.read(text.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("raid store read failed");loaded=Decode(text);
        }
        checkpoint=std::move(loaded);writable_=true;return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
bool RaidSessionStore::Save(const RaidCheckpoint& checkpoint,std::string& error) {
    error.clear();if(!writable_) {error="raid store not writable";return false;}
    try {
        const auto text=Encode(checkpoint);if(text.size()>kMaximumFile)throw std::runtime_error("raid store exceeds capacity");
        static_cast<void>(Decode(text)); // 写入前验证不变量，失败保留旧文件。 / Validate before writing; failure preserves the old file.
        const auto temp=std::filesystem::path(file_.wstring()+L".tmp");
        HANDLE handle=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("raid temp open failed");
        DWORD written{};const bool ok=WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)
            && written==text.size()&&FlushFileBuffers(handle);CloseHandle(handle);
        if(!ok) {DeleteFileW(temp.c_str());throw std::runtime_error("raid temp write failed");}
        const bool replaced=std::filesystem::exists(file_)?ReplaceFileW(file_.c_str(),temp.c_str(),nullptr,0,nullptr,nullptr)!=0
            :MoveFileExW(temp.c_str(),file_.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
        if(!replaced) {DeleteFileW(temp.c_str());throw std::runtime_error("raid atomic replace failed");}return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
}
