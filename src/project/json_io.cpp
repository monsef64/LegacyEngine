#include "project/json_io.hpp"
#include <simdjson.h>
namespace {
Json convert(simdjson::dom::element element,int depth){
    if(depth>64)throw std::runtime_error("JSON nesting exceeds limit");
    using simdjson::dom::element_type;
    switch(element.type()){
    case element_type::ARRAY:{Json result=Json::array();for(auto value:element.get_array())result.push_back(convert(value,depth+1));return result;}
    case element_type::OBJECT:{Json result=Json::object();for(auto field:element.get_object())result[std::string(field.key)]=convert(field.value,depth+1);return result;}
    case element_type::INT64:return std::int64_t(element.get_int64());
    case element_type::UINT64:return std::uint64_t(element.get_uint64());
    case element_type::DOUBLE:return double(element.get_double());
    case element_type::STRING:return std::string(std::string_view(element.get_string()));
    case element_type::BOOL:return bool(element.get_bool());
    case element_type::NULL_VALUE:return nullptr;
    }
    throw std::runtime_error("Unsupported JSON element");
}
}
Json parseJson(const std::string& text){
    if(text.size()>16*1024*1024)throw std::runtime_error("JSON exceeds 16 MiB limit");
    try{
        thread_local simdjson::dom::parser parser(16*1024*1024);
        auto document=parser.parse(text);
        return convert(document.value(),0);
    }catch(const simdjson::simdjson_error& e){throw std::runtime_error(std::string("Invalid JSON: ")+e.what());}
}
