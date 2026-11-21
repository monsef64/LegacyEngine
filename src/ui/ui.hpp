#pragma once
#include <string>

// Serialized configuration only; RmlUi documents live in GameUI, never in ECS.
struct UI {
    bool enabled = true;
    std::string rml;
    std::string rcss;
};
