#pragma once
#include <string_view>
#include <string>
#include <algorithm>
#include <vector>
#include <cctype>
struct LuaToken {std::size_t begin,end;enum Kind {Plain,Keyword,String,Comment,Number,Api} kind;};
inline std::vector<LuaToken> luaTokens(std::string_view s){
    std::vector<LuaToken> tokens;
    auto longEnd=[&](std::size_t start)->std::size_t{if(start>=s.size() || s[start]!='[')return start;auto p=start+1;while(p<s.size() && s[p]=='=')++p;if(p>=s.size() || s[p]!='[')return start;auto close="]"+std::string(p-start-1,'=')+"]";auto end=s.find(close,p+1);return end==s.npos?s.size():end+close.size();};
    for(std::size_t i=0;i<s.size();){auto begin=i;auto kind=LuaToken::Plain;
        if(s.substr(i,2)=="--"){kind=LuaToken::Comment;auto end=longEnd(i+2);if(end>i+2)i=end;else{auto e=s.find('\n',i);i=e==s.npos?s.size():e;}}
        else if(s[i]=='\'' || s[i]=='"'){kind=LuaToken::String;char q=s[i++];while(i<s.size()){if(s[i]=='\\'){i=std::min(i+2,s.size());continue;}if(s[i++]==q)break;}}
        else if(auto end=longEnd(i);end>i){kind=LuaToken::String;i=end;}
        else if(std::isdigit(static_cast<unsigned char>(s[i]))){kind=LuaToken::Number;++i;while(i<s.size() && (std::isalnum(static_cast<unsigned char>(s[i]))||s[i]=='.'))++i;}
        else if(std::isalpha(static_cast<unsigned char>(s[i]))||s[i]=='_'){++i;while(i<s.size() && (std::isalnum(static_cast<unsigned char>(s[i]))||s[i]=='_'))++i;
            auto word=s.substr(begin,i-begin);std::string_view keys=" and break do else elseif end false for function goto if in local nil not or repeat return then true until while ";
            if(keys.find(" "+std::string(word)+" ")!=keys.npos)kind=LuaToken::Keyword;
            else if(word=="self"||word=="Camera"||word=="GameObject"||word=="AudioSource"||word=="AudioClip")kind=LuaToken::Api;
        }else ++i;
        tokens.push_back({begin,i,kind});
    }return tokens;
}
