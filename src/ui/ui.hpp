#pragma once
#include <string>

// Serialized configuration only; RmlUi documents live in GameUI, never in ECS.
struct Canvas {
    bool enabled = true;
    std::string rml;
    std::string rcss;
};

using UI = Canvas; // Backwards-compatible C++ component identity.
