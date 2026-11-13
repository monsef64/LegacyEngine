#pragma once
#include "assets/assets.hpp"
#include "project/project_io.hpp"
#include <future>
#include <optional>
#include <algorithm>

// Single active level. Requests never replace a World inside a Lua callback.
// Async workers read bytes only; ECS/JSON validation runs at the frame boundary.
class LevelSystem {
public:
    inline static LevelSystem* active=nullptr;
    struct Transition{std::string name;std::unique_ptr<ProjectIO::Loaded> level;};
    std::string error;
    bool suspended=false;
private:
    AssetManager assets_;
    ProjectIO::Meshes meshes_;
    std::vector<std::string> names_;
    std::string current_,requested_,bytes_;
    bool pending_=false,unload_=false;
    std::future<std::string> reader_;
    static std::string read(const std::filesystem::path& file){
        if(std::filesystem::file_size(file)>ProjectIO::maxBytes)throw std::runtime_error("Level exceeds 16 MiB");
        std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("Cannot open level");
        std::string text{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
        if(text.size()>ProjectIO::maxBytes || in.bad())throw std::runtime_error("Cannot read level");
        return text;
    }
    void manifest(){Json json{{"version",1},{"levels",names_},{"current",current_}};writeJsonFile(assets_.resolve("Levels/levels.json"),json);}
public:
    explicit LevelSystem(const ProjectIO::Meshes& meshes):meshes_(meshes){}
    ~LevelSystem(){if(active==this)active=nullptr;}
    const std::string& current()const{return current_;}
    const std::vector<std::string>& names()const{return names_;}
    bool busy()const{return pending_;}
    void cancel(){if(reader_.valid())reader_=std::future<std::string>{};pending_=false;bytes_.clear();requested_.clear();}
    const auto& root()const{return assets_.root;}
    static bool validName(const std::string& name){
        if(name.empty() || name.size()>128 || name=="." || name==".." || name.back()=='.' || name.back()==' ')return false;
        for(unsigned char c:name)if(c<32 || c=='/' || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')return false;
        return true;
    }
    static std::string path(const std::string& name){if(!validName(name))throw std::runtime_error("Invalid level name");return "Levels/"+name+".level";}
    void setRoot(const std::filesystem::path& root){
        if(root==assets_.root)return;
        cancel();
        assets_.root=root;names_.clear();current_.clear();error.clear();
        try{const auto file=assets_.resolve("Levels/levels.json");if(!std::filesystem::exists(file))return;
            const auto json=readJsonFile(file,128*1024);if(json.at("version")!=1 || !json.at("levels").is_array() || json.at("levels").size()>256)throw std::runtime_error("Invalid level registry");
            auto names=json.at("levels").get<std::vector<std::string>>();for(std::size_t i=0;i<names.size();++i)if(!validName(names[i]) || std::find(names.begin(),names.begin()+i,names[i])!=names.begin()+i)throw std::runtime_error("Invalid or duplicate level name");
            auto current=json.at("current").get<std::string>();if(!current.empty() && std::find(names.begin(),names.end(),current)==names.end())throw std::runtime_error("Unknown current level");names_=std::move(names);current_=std::move(current);
        }catch(const std::exception& e){error=e.what();}
    }
    void save(const std::string& name,World& world,std::string_view source,const OrbitCamera& camera){
        if(!validName(name))throw std::runtime_error("Use a level name without path separators");
        bool added=std::find(names_.begin(),names_.end(),name)==names_.end();if(added && names_.size()>=256)throw std::runtime_error("Maximum 256 levels");
        auto document=parseJson(ProjectIO::encode(world,source,camera,meshes_));ProjectIO::Loaded check;ProjectIO::decode(document.dump(),check,meshes_);
        writeJsonFile(assets_.resolve(path(name)),document);if(added)names_.push_back(name);
        manifest();
    }
    void saveCurrent(World& world,std::string_view source,const OrbitCamera& camera){
        const auto name=current_.empty()?std::string("Main"):current_;save(name,world,source,camera);commit(name,true);
    }
    void renameCurrent(const std::string& name){
        if(pending_ || suspended || current_.empty())throw std::runtime_error("No editable current level");
        if(!validName(name))throw std::runtime_error("Invalid level name");
        if(name==current_)return;
        if(std::find(names_.begin(),names_.end(),name)!=names_.end() || std::filesystem::exists(assets_.resolve(path(name))))throw std::runtime_error("A level with that name exists");
        auto it=std::find(names_.begin(),names_.end(),current_);
        const auto old=current_;std::filesystem::rename(assets_.resolve(path(old)),assets_.resolve(path(name)));
        *it=name;current_=name;
        try{manifest();}catch(...){*it=old;current_=old;std::filesystem::rename(assets_.resolve(path(name)),assets_.resolve(path(old)));throw;}
    }
    void create(const std::string& name){
        if(std::find(names_.begin(),names_.end(),name)!=names_.end() || std::filesystem::exists(assets_.resolve(path(name))))throw std::runtime_error("A level with that name exists");
        World empty;save(name,empty,"",OrbitCamera{});
    }
    bool load(std::string name={},bool async=false){
        if(suspended || pending_){error="A level transition is already in progress";return false;}
        if(name.empty())name=current_.empty()?(names_.empty()?"":names_.front()):current_;
        if(std::find(names_.begin(),names_.end(),name)==names_.end()){error="Unknown level: "+name;return false;}
        try{auto file=assets_.resolve(path(name));if(async)reader_=std::async(std::launch::async,[file]{return read(file);});else bytes_=read(file);
            requested_=name;pending_=true;unload_=false;error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
    bool unload(const std::string& name={}){
        if(suspended || pending_ || current_.empty() || (!name.empty() && name!=current_)){error="Only the current level can be unloaded";return false;}
        requested_.clear();bytes_.clear();unload_=true;pending_=true;error.clear();return true;
    }
    bool relative(int offset){auto it=std::find(names_.begin(),names_.end(),current_);if(it==names_.end()){error="No current level";return false;}auto index=std::distance(names_.begin(),it)+offset;if(index<0 || index>=static_cast<std::ptrdiff_t>(names_.size())){error="No adjacent level";return false;}return load(names_[std::size_t(index)]);}
    std::optional<Transition> poll(){
        if(!pending_)return {};
        if(reader_.valid() && reader_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};
        pending_=false;
        try{if(reader_.valid())bytes_=reader_.get();auto loaded=std::make_unique<ProjectIO::Loaded>();if(!unload_)ProjectIO::decode(bytes_,*loaded,meshes_);bytes_.clear();error.clear();return Transition{requested_,std::move(loaded)};}
        catch(const std::exception& e){bytes_.clear();error=e.what();return {};}
    }
    void commit(const std::string& name,bool persist){if(!name.empty() && std::find(names_.begin(),names_.end(),name)==names_.end())throw std::runtime_error("Unknown level");current_=name;if(persist)manifest();}
};
