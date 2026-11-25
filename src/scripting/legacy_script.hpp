#pragma once
#include "scripting/lua_highlight.hpp"
struct ExportDeclaration {
    std::size_t begin, end, body;
    bool bare;
    bool typed = false;
};
// Only line-leading declarations outside Lua strings and block comments count.
inline std::vector<ExportDeclaration> exportDeclarations(std::string_view source) {
    std::vector<ExportDeclaration> declarations;
    std::size_t consumed = 0;
    for (const auto& token : luaTokens(source)) {
        if (token.begin < consumed) {
            continue;
        }
        const bool typed =
            token.kind == LuaToken::Api && (source.substr(token.begin, 6) == "global" ||
                                            source.substr(token.begin, 6) == "expose");
        const bool bare = typed;
        if (!typed) {
            continue;
        }
        const auto body = token.begin + 6;
        if (body < source.size() && !std::isspace(static_cast<unsigned char>(source[body]))) {
            continue;
        }
        auto start = source.rfind('\n', token.begin);
        start = start == source.npos ? 0 : start + 1;
        bool leading = true;
        for (auto i = start; i < token.begin; ++i) {
            if (source[i] != ' ' && source[i] != '\t' && source[i] != '\r') {
                leading = false;
            }
        }
        if (!leading) {
            continue;
        }
        auto end = source.find('\n', body);
        if (end == source.npos) {
            end = source.size();
        }
        declarations.push_back({token.begin, end, body, bare, typed});
        consumed = end;
    }
    return declarations;
}
inline std::string preprocessLegacyScript(std::string_view source) {
    std::string result(source);
    for (const auto& declaration : exportDeclarations(source)) {
        if (declaration.bare) {
            for (auto i = declaration.begin; i < declaration.end; ++i) {
                if (result[i] != '\r') {
                    result[i] = ' ';
                }
            }
        }
    }
    return result;
}
