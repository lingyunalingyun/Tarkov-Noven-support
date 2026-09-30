#include "data/TaskCatalog.h"

#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace noven::data {
namespace {
std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> result(1);
    for (std::size_t i=0;i<line.size();++i) {
        const char c=line[i];
        if(c=='\t'){result.emplace_back();continue;}
        if(c!='\\'){result.back()+=c;continue;}
        if(++i==line.size()) throw std::runtime_error("escape");
        switch(line[i]){case 't':result.back()+='\t';break;case 'n':result.back()+='\n';break;
        case 'r':result.back()+='\r';break;case '\\':result.back()+='\\';break;default:throw std::runtime_error("escape");}
    }
    return result;
}
void Identity(const std::string& value) {
    if(value.empty() || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
        throw std::runtime_error("identity");
}
std::int64_t Integer(const std::string& value) {
    std::int64_t result{};const auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),result);
    if(ec!=std::errc{} || end!=value.data()+value.size() || result<0) throw std::runtime_error("integer");
    return result;
}
double Number(const std::string& value,bool nonnegative=false) {
    double result{};const auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),result);
    if(ec!=std::errc{} || end!=value.data()+value.size() || !std::isfinite(result) || (nonnegative&&result<0)) throw std::runtime_error("number");
    return result;
}
template<class Consumer> void Read(const std::filesystem::path& directory,const char* name,const char* header,Consumer consume) {
    std::ifstream file(directory/(std::string("task_")+name+".tsv"),std::ios::binary);std::string line;
    if(!std::getline(file,line)) throw std::runtime_error("missing file");
    if(!line.empty()&&line.back()=='\r')line.pop_back();if(line!=header)throw std::runtime_error("header");
    const auto width=Split(line).size();std::unordered_set<std::string> rows;
    while(std::getline(file,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()||line.find('\0')!=std::string::npos||line.size()>131072
            ||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data(),static_cast<int>(line.size()),nullptr,0)<=0)
            throw std::runtime_error("UTF-8/row");
        if(!rows.insert(line).second)throw std::runtime_error("duplicate row");
        auto fields=Split(line);if(fields.size()!=width||(fields[0]!="regular"&&fields[0]!="pve"))throw std::runtime_error("columns/mode");
        consume(fields);
    }
}
}

const TaskTrader* TaskCatalog::Trader(std::string_view mode,std::string_view id) const {
    for(const auto& trader:traders_)if(trader.mode==mode&&trader.id==id)return &trader;return nullptr;
}
const TaskRecord* TaskCatalog::Task(std::string_view mode,std::string_view id) const {
    for(const auto& task:tasks_)if(task.mode==mode&&task.id==id)return &task;return nullptr;
}
std::string_view TaskCatalog::StructureMode(GameMode mode) const {
    if(mode==GameMode::Pve&&std::any_of(tasks_.begin(),tasks_.end(),[](const auto& task){return task.mode=="pve";}))return "pve";
    return "regular";
}
bool TaskCatalog::Load(const std::filesystem::path& directory,std::wstring& error) {
    traders_.clear();tasks_.clear();error.clear();
    try {
        TaskCatalog next;
        Read(directory,"traders","mode\tid\tnameZh\tnameEn\timageKey",[&](const auto& f){
            Identity(f[1]);if((f[2].empty()&&f[3].empty())||next.Trader(f[0],f[1]))throw std::runtime_error("trader");
            if(!f[4].empty()&&f[4]!="trader-"+f[1])throw std::runtime_error("trader image");
            next.traders_.push_back({f[0],f[1],f[2],f[3],f[4]});});
        Read(directory,"tasks","mode\tid\ttraderId\tnameZh\tnameEn\tlocationZh\tlocationEn\tminLevel\texperience\tfaction\tkappaRequired\tlightkeeperRequired",[&](const auto& f){
            Identity(f[1]);Identity(f[2]);if(!next.Trader(f[0],f[2])||next.Task(f[0],f[1])||(f[3].empty()&&f[4].empty())
                ||(f[10]!="0"&&f[10]!="1")||(f[11]!="0"&&f[11]!="1"))throw std::runtime_error("task");
            next.tasks_.push_back({f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[9],Integer(f[7]),Integer(f[8]),f[10]=="1",f[11]=="1"});});
        Read(directory,"requirements","mode\ttaskId\trequiredTaskId",[&](const auto& f){
            auto* task=const_cast<TaskRecord*>(next.Task(f[0],f[1]));if(!task||!next.Task(f[0],f[2]))throw std::runtime_error("requirement");
            if(std::find(task->prerequisites.begin(),task->prerequisites.end(),f[2])!=task->prerequisites.end())throw std::runtime_error("duplicate requirement");
            task->prerequisites.push_back(f[2]);});
        Read(directory,"objectives","mode\ttaskId\tid\ttype\tdescriptionZh\tdescriptionEn\tcount\toptional",[&](const auto& f){
            auto* task=const_cast<TaskRecord*>(next.Task(f[0],f[1]));Identity(f[2]);
            if(!task||(f[4].empty()&&f[5].empty())||(f[7]!="0"&&f[7]!="1"))throw std::runtime_error("objective");
            task->objectives.push_back({f[2],f[3],f[4],f[5],Number(f[6],true),f[7]=="1",{}});});
        std::unordered_map<std::string,TaskObjective*> objectiveIndex;
        for(auto& task:next.tasks_)for(auto& objective:task.objectives)objectiveIndex.emplace(task.mode+'\n'+objective.id,&objective);
        Read(directory,"objective_items","mode\tobjectiveId\titemId",[&](const auto& f){
            Identity(f[1]);Identity(f[2]);const auto found=objectiveIndex.find(f[0]+'\n'+f[1]);
            if(found==objectiveIndex.end())throw std::runtime_error("objective item");
            found->second->itemIds.push_back(f[2]);});
        Read(directory,"rewards","mode\ttaskId\ttype\ttargetId\tvalue",[&](const auto& f){
            auto* task=const_cast<TaskRecord*>(next.Task(f[0],f[1]));Identity(f[3]);if(!task)throw std::runtime_error("reward");
            task->rewards.push_back({f[2],f[3],Number(f[4])});});
        if(!std::any_of(next.tasks_.begin(),next.tasks_.end(),[](const auto& task){return task.mode=="regular";}))throw std::runtime_error("empty");
        std::sort(next.tasks_.begin(),next.tasks_.end(),[](const auto& a,const auto& b){return std::tie(a.mode,a.traderId,a.id)<std::tie(b.mode,b.traderId,b.id);});
        *this=std::move(next);return true;
    } catch(const std::exception& exception) {
        const std::string reason=exception.what();
        error=L"[tasks] invalid or missing generated TSV assets: "+std::wstring(reason.begin(),reason.end());return false;
    }
}

} // namespace noven::data
