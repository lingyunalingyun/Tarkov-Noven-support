#include "events/EventTranslation.h"
#include "raid/RaidJson.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>

namespace noven::events {
namespace {
namespace json=raid::json;
constexpr std::size_t maxCache=2*1024*1024,maxText=32768,maxEntries=512;
bool Valid(std::string_view s) {
    return !s.empty()&&s.size()<=maxText&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0)>0;
}
std::vector<std::string> Texts(const std::vector<EventRecord>& records) {
    std::set<std::string> seen;std::vector<std::string> out;
    const auto add=[&](const std::string& s){if(Valid(s)&&seen.insert(s).second)out.push_back(s);};
    for(const auto& e:records){add(e.title);add(e.summary);
        for(const auto& evidence:e.sourceEvidence)if(evidence.sourceKind==SourceKind::CommunityWiki)add(evidence.summary);}
    return out;
}
std::wstring Escape(std::string_view s) {
    constexpr wchar_t hex[]=L"0123456789ABCDEF";std::wstring out;
    for(unsigned char c:s){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=c;
        else{out+=L'%';out+=hex[c>>4];out+=hex[c&15];}}
    return out;
}
std::pair<std::size_t,std::string> TaskTail(std::string_view source) {
    std::string result;std::size_t begin=source.size(),cursor{};
    while(cursor<source.size()){
        const auto end=source.find('\n',cursor);const auto line=source.substr(cursor,end==source.npos?source.size()-cursor:end-cursor);
        if(line.starts_with("Quest ")&&line.ends_with(" has been added.")){
            if(result.empty())begin=cursor;
            const auto name=line.substr(6,line.size()-6-16);std::string display(name);
            // 用户确认的显示译名，不创建任务身份；未确认名称保留英文。
            // User-confirmed display names never create task identities; unconfirmed names remain English.
            for(const auto& [english,chinese]:std::initializer_list<std::pair<std::string_view,std::string_view>>{
                {"Fog of War","战争迷雾"},{"Number Temporarily Unavailable","无法接通的号码"},
                {"All-Inclusive Support","全方位支援"},{"A Familiar Face...","熟悉的面孔……"},{"Scope the Clearing","圈地行动"}})
                if(name==english)display=chinese;
            if(!result.empty())result+='\n';result+="新增任务："+display;
        }else{begin=source.size();result.clear();}
        if(end==source.npos)break;cursor=end+1;
    }
    return {begin,result};
}
std::string Terms(std::string value,std::string_view source={}) {
    // 仅修正明确的显示术语，不用译文识别地图、任务或活动状态。
    // Correct explicit display terms only; translations never identify maps/tasks/event status.
    const auto letter=[](unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z');};
    const auto word=[&](std::string_view term){
        std::size_t pos{};while((pos=source.find(term,pos))!=source.npos){
            if((!pos||!letter(source[pos-1]))&&(pos+term.size()==source.size()||!letter(source[pos+term.size()])))return true;
            pos+=term.size();
        }return false;
    };
    // “扫荡者”是机翻歧义词：只有原文明确 Scav 且未混入 Rogues/Raiders 才修正。
    // The ambiguous machine term is corrected only with explicit Scav source text and no mixed Rogue/Raider identities.
    const bool scav=(word("Scav")||word("Scavs"))&&!word("Rogue")&&!word("Rogues")&&!word("Raider")&&!word("Raiders");
    for(const auto& [from,to]:std::initializer_list<std::pair<std::string_view,std::string_view>>{
        {"Glukhar","格鲁哈"},{"Reserve","储备站"},{"Lighthouse","灯塔"},{"季节性游戏模式","赛季模式"},
        {"水处理厂","污水处理厂"},{"拾荒者","Scav"},{"游荡者","Rogues"},{"掠夺者","Raiders"},{"扫荡者","Scav"}}) {
        if(from=="扫荡者"&&!scav)continue;
        std::size_t pos{};while((pos=value.find(from,pos))!=value.npos) {
            if((pos&&letter(value[pos-1]))||(pos+from.size()<value.size()&&letter(value[pos+from.size()]))){pos+=from.size();continue;}
            // 已正确的地点名不重复加前缀；旧缓存也能安全复用同一规则。
            // Do not prefix an already-correct location again; the same rules safely normalize old cache entries.
            if(from=="水处理厂"&&std::string_view(value).substr(0,pos).ends_with("污")){pos+=from.size();continue;}
            value.replace(pos,from.size(),to);pos+=to.size();
        }
    }
    const auto [begin,tasks]=TaskTail(source);
    if(!tasks.empty()){
        const auto count=static_cast<std::size_t>(std::count(tasks.begin(),tasks.end(),'\n'))+1;
        std::size_t cut=value.size();bool valid=true;
        for(std::size_t i=0;i<count;++i){const auto p=cut?value.rfind('\n',cut-1):value.npos;
            const auto line=std::string_view(value).substr(p==value.npos?0:p+1,cut-(p==value.npos?0:p+1));
            valid&=line.find("任务")!=line.npos||line.find("Quest")!=line.npos;cut=p==value.npos?0:p;
        }
        if(valid){value.resize(cut);if(!value.empty())value+='\n';value+=tasks;}
        else return std::string(source); // 缓存结构不确定时保留原文。 / Preserve original when cached structure is uncertain.
    }
    return value;
}
}
std::vector<std::string> EventTranslation::Split(std::string_view text) {
    if(!Valid(text))return {};
    std::vector<std::string> parts;
    while(!text.empty()) {
        std::size_t n=(std::min)(std::size_t{500},text.size());
        if(n<text.size()) {
            while(n&&(static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;
            const auto boundary=text.substr(0,n).find_last_of(" \n\t");
            if(boundary!=text.npos&&boundary>n/2)n=boundary+1;
        }
        parts.emplace_back(text.substr(0,n));text.remove_prefix(n);
    }return parts;
}
bool EventTranslation::Parse(const HttpResponse& response,std::string& translated,std::string& error) {
    translated.clear();error.clear();try {
        if(!response.error.empty()||response.status!=200||!response.contentType.starts_with("application/json")
            ||response.body.empty()||response.body.size()>128*1024)throw std::runtime_error("invalid translation response");
        const auto root=json::Parser(response.body,true).Parse();const auto& status=root.At("responseStatus");
        if(!((status.type==json::Value::Type::Integer&&status.Int()==200)
            ||(status.type==json::Value::Type::String&&status.String()=="200")))throw std::runtime_error("translation API rejected request");
        if(root.At("quotaFinished").Bool())throw std::runtime_error("translation quota exhausted");
        auto value=root.At("responseData").At("translatedText").String();
        if(!Valid(value)||value.starts_with("QUERY LENGTH LIMIT")||value.find("MYMEMORY WARNING")!=value.npos)throw std::runtime_error("invalid translated text");
        translated=Terms(std::move(value));return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
EventTranslation::~EventTranslation(){if(lease_!=INVALID_HANDLE_VALUE)CloseHandle(lease_);}
bool EventTranslation::Load(const std::filesystem::path& file,std::string& error) {
    error.clear();file_=file;text_.clear();writable_=false;
    if(lease_!=INVALID_HANDLE_VALUE){CloseHandle(lease_);lease_=INVALID_HANDLE_VALUE;}
    try {
        std::filesystem::create_directories(file.parent_path());
        lease_=CreateFileW((file.wstring()+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lease_==INVALID_HANDLE_VALUE)throw std::runtime_error("translation cache unavailable");
        if(std::filesystem::exists(file)) {
            const auto size=std::filesystem::file_size(file);if(size>maxCache)throw std::runtime_error("translation cache too large");
            std::ifstream in(file,std::ios::binary);std::string bytes(static_cast<std::size_t>(size),'\0');
            if(!in.read(bytes.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("translation cache read failed");
            const auto root=json::Parser(bytes).Parse();
            if(root.At("schemaVersion").Int()!=1||root.At("provider").String()!="MyMemory"||root.At("locale").String()!="zh-CN")throw std::runtime_error("invalid translation cache schema");
            const auto& values=root.At("entries").Array();if(values.size()>maxEntries)throw std::runtime_error("translation capacity exceeded");
            std::map<std::string,std::string> next;
            for(const auto& v:values){const auto& source=v.At("source").String();const auto& target=v.At("translated").String();
                if(!Valid(source)||!Valid(target)||!next.emplace(source,Terms(target,source)).second)throw std::runtime_error("invalid translation entry");}
            text_=std::move(next);
        }
        writable_=true;return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool EventTranslation::Save(std::string& error) {
    std::string bytes="{\"schemaVersion\":1,\"provider\":\"MyMemory\",\"locale\":\"zh-CN\",\"entries\":[";bool comma=false;
    for(const auto& [source,target]:text_){if(comma)bytes+=',';comma=true;bytes+="{\"source\":"+json::Quote(source)+",\"translated\":"+json::Quote(target)+'}';}
    bytes+="]}";if(bytes.size()>maxCache){error="translation cache capacity exceeded";return false;}
    const auto temp=file_.wstring()+L".tmp";
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){error="translation temp open failed";return false;}
    DWORD written{};const bool ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(h);CloseHandle(h);
    if(!ok){DeleteFileW(temp.c_str());error="translation write failed";return false;}
    const bool replaced=GetFileAttributesW(file_.c_str())!=INVALID_FILE_ATTRIBUTES
        ?ReplaceFileW(file_.c_str(),temp.c_str(),nullptr,0,nullptr,nullptr)!=0:MoveFileExW(temp.c_str(),file_.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
    if(!replaced){DeleteFileW(temp.c_str());error="translation replace failed";return false;}return true;
}
bool EventTranslation::Refresh(const std::vector<EventRecord>& records,std::stop_token stop,std::string& error) {
    error.clear();if(!writable_){error="translation cache not writable";return false;}
    const auto originals=Texts(records);auto next=text_;bool dirty=false,ok=true;std::size_t bytes{},requests{};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    for(const auto& source:originals) {
        if(text_.contains(source))continue;
        const auto [taskBegin,taskText]=TaskTail(source);
        const auto input=std::string_view(source).substr(0,taskBegin);
        if(stop.stop_requested()||std::chrono::steady_clock::now()>deadline||bytes+input.size()>4000||requests+Split(input).size()>12)break;
        std::string translated;
        for(const auto& part:Split(input)) {
            if(stop.stop_requested()||std::chrono::steady_clock::now()>deadline){ok=false;error="translation stopped";break;}
            std::string value;++requests;bytes+=part.size();
            if(!Parse(http_.Get(L"api.mymemory.translated.net",L"/get?q="+Escape(part)+L"&langpair=en%7Czh-CN"),value,error)){ok=false;break;}
            translated+=value;
            if(!part.empty()&&(part.back()=='\n'||part.back()==' '))translated+=part.back();
        }
        if(!ok)break;
        if(!taskText.empty()){if(!translated.empty()&&translated.back()!='\n')translated+='\n';translated+=taskText;}
        if(Valid(translated)){next[source]=Terms(std::move(translated),source);dirty=true;}
    }
    // 原文变更不复用旧译文；只保留当前窗口，失败保留已有缓存。
    // Changed originals never reuse stale translations; retain the current window, preserving cache on failure.
    if(dirty) {
        std::erase_if(next,[&](const auto& item){return std::ranges::find(originals,item.first)==originals.end();});
        while(next.size()>maxEntries)next.erase(std::prev(next.end()));
        auto old=std::move(text_);text_=std::move(next);std::string saveError;
        if(!Save(saveError)){text_=std::move(old);error=saveError;return false;}
    }return ok;
}
void EventTranslation::Apply(std::vector<EventRecord>& records) const {
    for(auto& event:records) {
        event.machineText.clear();
        for(const auto& source:Texts({event}))if(const auto i=text_.find(source);i!=text_.end())event.machineText.emplace(*i);
    }
}
}
