#pragma once
#include "../../third_party/json/json.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
using Json=nlohmann::json;
// SIMD-accelerated parsing; serialization and the editable DOM retain Json.
Json parseJson(const std::string& text);
inline Json readJsonFile(const std::filesystem::path& path,std::size_t limit){
    if(std::filesystem::file_size(path)>limit)throw std::runtime_error("JSON file exceeds size limit");
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read JSON file");return parseJson({std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()});
}
inline void writeJsonFile(const std::filesystem::path& path,const Json& document){
    std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try{std::ofstream out(temp,std::ios::binary);out<<document.dump(2)<<'\n';out.close();if(!out)throw std::runtime_error("Cannot write JSON file");std::filesystem::rename(temp,path);}
    catch(...){std::error_code ec;std::filesystem::remove(temp,ec);throw;}
}
