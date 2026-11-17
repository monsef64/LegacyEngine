#include "ui/mygui_platform.hpp"
#include "editor_font.hpp"
#include "editor_icons.hpp"

#include <SDL3/SDL.h>
#include <glad/gl.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ui {

namespace {

uint8_t clampByte(float v) {
    return static_cast<uint8_t>(std::clamp(std::lround(v * 255.0f), 0l, 255l));
}

class MemoryDataStream final : public MyGUI::IDataStream {
  public:
    explicit MemoryDataStream(std::string data) : data_(std::move(data)) {}

    bool eof() override {
        return pos_ >= data_.size();
    }
    size_t size() override {
        return data_.size();
    }
    void readline(std::string& source, MyGUI::Char delim) override {
        source.clear();
        while (pos_ < data_.size()) {
            const char c = data_[pos_++];
            if (static_cast<unsigned char>(c) == delim) {
                break;
            }
            if (c != '\r') {
                source.push_back(c);
            }
        }
    }
    size_t read(void* buf, size_t count) override {
        const size_t avail = data_.size() - pos_;
        const size_t n = std::min(count, avail);
        if (n > 0) {
            std::memcpy(buf, data_.data() + pos_, n);
            pos_ += n;
        }
        return n;
    }

  private:
    std::string data_;
    size_t pos_ = 0;
};

class SdlLogListener final : public MyGUI::ILogListener {
  public:
    void log(std::string_view section,
             MyGUI::LogLevel level,
             const struct tm*,
             std::string_view message,
             std::string_view,
             int) override {
        if (level == MyGUI::LogLevel::Error || level == MyGUI::LogLevel::Critical) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "MyGUI [%.*s]: %.*s",
                         static_cast<int>(section.size()),
                         section.data(),
                         static_cast<int>(message.size()),
                         message.data());
        }
    }
};

uint32_t nextUtf8(const char*& p, const char* end) {
    if (p >= end) {
        return 0;
    }
    const unsigned char c0 = static_cast<unsigned char>(*p++);
    if (c0 < 0x80) {
        return c0;
    }
    if ((c0 & 0xE0) == 0xC0 && p < end) {
        const unsigned char c1 = static_cast<unsigned char>(*p++);
        return ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
    }
    if ((c0 & 0xF0) == 0xE0 && p + 1 < end) {
        const unsigned char c1 = static_cast<unsigned char>(*p++);
        const unsigned char c2 = static_cast<unsigned char>(*p++);
        return ((c0 & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
    }
    if ((c0 & 0xF8) == 0xF0 && p + 2 < end) {
        const unsigned char c1 = static_cast<unsigned char>(*p++);
        const unsigned char c2 = static_cast<unsigned char>(*p++);
        const unsigned char c3 = static_cast<unsigned char>(*p++);
        return ((c0 & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
    }
    return '?';
}

constexpr const char* kMyGuiVertexShader = R"GLSL(#version 330 core
layout(location = 0) in vec3 VertexPosition;
layout(location = 1) in vec4 VertexColor;
layout(location = 2) in vec2 VertexTexCoord;
out vec4 vColor;
out vec2 vTexCoord;
uniform float YScale;
uniform float FlipV;
void main() {
    vColor = VertexColor;
    vTexCoord = vec2(VertexTexCoord.x, FlipV > 0.5 ? (1.0 - VertexTexCoord.y) : VertexTexCoord.y);
    gl_Position = vec4(VertexPosition.x, VertexPosition.y * YScale, VertexPosition.z, 1.0);
}
)GLSL";

constexpr const char* kMyGuiFragmentShader = R"GLSL(#version 330 core
in vec4 vColor;
in vec2 vTexCoord;
uniform sampler2D Texture;
out vec4 fragColor;
void main() {
    fragColor = texture(Texture, vTexCoord) * vColor;
}
)GLSL";

constexpr const char* kLayersXml = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<MyGUI type="Layer" version="1.2">
    <Layer type="SharedLayer" name="Wallpaper"><Property key="Pick" value="false"/></Layer>
    <Layer type="SharedLayer" name="Back"><Property key="Pick" value="true"/></Layer>
    <Layer type="OverlappedLayer" name="Overlapped"><Property key="Pick" value="true"/></Layer>
    <Layer type="SharedLayer" name="Middle"><Property key="Pick" value="true"/></Layer>
    <Layer type="OverlappedLayer" name="Modal"><Property key="Pick" value="true"/></Layer>
    <Layer type="SharedLayer" name="Main"><Property key="Pick" value="true"/></Layer>
    <Layer type="OverlappedLayer" name="Popup"><Property key="Pick" value="true"/></Layer>
    <Layer type="SharedLayer" name="ToolTip"><Property key="Pick" value="false"/></Layer>
    <Layer type="SharedLayer" name="DragAndDrop"><Property key="Pick" value="false"/></Layer>
    <Layer type="SharedLayer" name="Pointer"><Property key="Pick" value="false"/></Layer>
</MyGUI>
)XML";

constexpr const char* kFontsXml = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<MyGUI type="Resource" version="1.1">
    <Resource type="LegacyMergedFont" name="Default">
        <Property key="Size" value="18"/>
    </Resource>
</MyGUI>
)XML";

constexpr const char* kSkinsXml = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<MyGUI type="Resource" version="1.1">
    <Resource type="ResourceSkin" name="PanelEmpty" size="16 16"/>
    <Resource type="ResourceSkin" name="TextBoxSkin" size="16 16">
        <BasisSkin type="SimpleText" offset="0 0 16 16" align="Stretch"/>
    </Resource>
    <Resource type="ResourceSkin" name="ImageBox" size="16 16">
        <BasisSkin type="MainSkin" offset="0 0 16 16" align="Stretch"/>
    </Resource>
    <Resource type="ResourceSkin" name="EditClientSkin" size="16 16">
        <BasisSkin type="EditText" offset="0 0 16 16" align="Stretch"/>
    </Resource>
    <Resource type="ResourceSkin" name="LegacySolidSkin" size="16 16" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 16 16" align="Stretch">
            <State name="normal" offset="2 2 12 12"/>
            <State name="disabled" offset="2 2 12 12"/>
            <State name="highlighted" offset="2 2 12 12"/>
            <State name="pushed" offset="2 2 12 12"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyPanelSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 4 4" align="Left Top"><State name="normal" offset="16 0 4 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="4 0 24 4" align="HStretch Top"><State name="normal" offset="20 0 24 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="28 0 4 4" align="Right Top"><State name="normal" offset="44 0 4 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="0 4 4 24" align="Left VStretch"><State name="normal" offset="16 4 4 24"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="4 4 24 24" align="Stretch"><State name="normal" offset="20 4 24 24"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="28 4 4 24" align="Right VStretch"><State name="normal" offset="44 4 4 24"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="0 28 4 4" align="Left Bottom"><State name="normal" offset="16 28 4 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="4 28 24 4" align="HStretch Bottom"><State name="normal" offset="20 28 24 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="28 28 4 4" align="Right Bottom"><State name="normal" offset="44 28 4 4"/></BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyChildSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 4 4" align="Left Top"><State name="normal" offset="48 0 4 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="4 0 24 4" align="HStretch Top"><State name="normal" offset="52 0 24 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="28 0 4 4" align="Right Top"><State name="normal" offset="76 0 4 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="0 4 4 24" align="Left VStretch"><State name="normal" offset="48 4 4 24"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="4 4 24 24" align="Stretch"><State name="normal" offset="52 4 24 24"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="28 4 4 24" align="Right VStretch"><State name="normal" offset="76 4 4 24"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="0 28 4 4" align="Left Bottom"><State name="normal" offset="48 28 4 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="4 28 24 4" align="HStretch Bottom"><State name="normal" offset="52 28 24 4"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="28 28 4 4" align="Right Bottom"><State name="normal" offset="76 28 4 4"/></BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyPopupSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 5 5" align="Left Top"><State name="normal" offset="80 0 5 5"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="5 0 22 5" align="HStretch Top"><State name="normal" offset="85 0 22 5"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="27 0 5 5" align="Right Top"><State name="normal" offset="107 0 5 5"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="0 5 5 22" align="Left VStretch"><State name="normal" offset="80 5 5 22"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="5 5 22 22" align="Stretch"><State name="normal" offset="85 5 22 22"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="27 5 5 22" align="Right VStretch"><State name="normal" offset="107 5 5 22"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="0 27 5 5" align="Left Bottom"><State name="normal" offset="80 27 5 5"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="5 27 22 5" align="HStretch Bottom"><State name="normal" offset="85 27 22 5"/></BasisSkin>
        <BasisSkin type="SubSkin" offset="27 27 5 5" align="Right Bottom"><State name="normal" offset="107 27 5 5"/></BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyTitleSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 32 32" align="Stretch">
            <State name="normal" offset="114 2 28 28"/>
            <State name="highlighted" offset="146 2 28 28"/>
            <State name="pushed" offset="146 2 28 28"/>
            <State name="disabled" offset="114 2 28 28"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyTitleActiveSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 32 32" align="Stretch">
            <State name="normal" offset="146 2 28 28"/>
            <State name="highlighted" offset="146 2 28 28"/>
            <State name="pushed" offset="146 2 28 28"/>
            <State name="disabled" offset="114 2 28 28"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyMenuBarSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 32 32" align="Stretch">
            <State name="normal" offset="178 2 28 28"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyFrameSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 4 4" align="Left Top">
            <State name="disabled" offset="0 32 4 4"/>
            <State name="normal" offset="0 32 4 4"/>
            <State name="highlighted" offset="32 32 4 4"/>
            <State name="pushed" offset="64 32 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 0 24 4" align="HStretch Top">
            <State name="disabled" offset="4 32 24 4"/>
            <State name="normal" offset="4 32 24 4"/>
            <State name="highlighted" offset="36 32 24 4"/>
            <State name="pushed" offset="68 32 24 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 0 4 4" align="Right Top">
            <State name="disabled" offset="28 32 4 4"/>
            <State name="normal" offset="28 32 4 4"/>
            <State name="highlighted" offset="60 32 4 4"/>
            <State name="pushed" offset="92 32 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="0 4 4 24" align="Left VStretch">
            <State name="disabled" offset="0 36 4 24"/>
            <State name="normal" offset="0 36 4 24"/>
            <State name="highlighted" offset="32 36 4 24"/>
            <State name="pushed" offset="64 36 4 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 4 24 24" align="Stretch">
            <State name="disabled" offset="4 36 24 24"/>
            <State name="normal" offset="4 36 24 24"/>
            <State name="highlighted" offset="36 36 24 24"/>
            <State name="pushed" offset="68 36 24 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 4 4 24" align="Right VStretch">
            <State name="disabled" offset="28 36 4 24"/>
            <State name="normal" offset="28 36 4 24"/>
            <State name="highlighted" offset="60 36 4 24"/>
            <State name="pushed" offset="92 36 4 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="0 28 4 4" align="Left Bottom">
            <State name="disabled" offset="0 60 4 4"/>
            <State name="normal" offset="0 60 4 4"/>
            <State name="highlighted" offset="32 60 4 4"/>
            <State name="pushed" offset="64 60 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 28 24 4" align="HStretch Bottom">
            <State name="disabled" offset="4 60 24 4"/>
            <State name="normal" offset="4 60 24 4"/>
            <State name="highlighted" offset="36 60 24 4"/>
            <State name="pushed" offset="68 60 24 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 28 4 4" align="Right Bottom">
            <State name="disabled" offset="28 60 4 4"/>
            <State name="normal" offset="28 60 4 4"/>
            <State name="highlighted" offset="60 60 4 4"/>
            <State name="pushed" offset="92 60 4 4"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyEditBoxSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 4 4" align="Left Top">
            <State name="disabled" offset="0 32 4 4"/>
            <State name="normal" offset="0 32 4 4"/>
            <State name="highlighted" offset="32 32 4 4"/>
            <State name="pushed" offset="64 32 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 0 24 4" align="HStretch Top">
            <State name="disabled" offset="4 32 24 4"/>
            <State name="normal" offset="4 32 24 4"/>
            <State name="highlighted" offset="36 32 24 4"/>
            <State name="pushed" offset="68 32 24 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 0 4 4" align="Right Top">
            <State name="disabled" offset="28 32 4 4"/>
            <State name="normal" offset="28 32 4 4"/>
            <State name="highlighted" offset="60 32 4 4"/>
            <State name="pushed" offset="92 32 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="0 4 4 24" align="Left VStretch">
            <State name="disabled" offset="0 36 4 24"/>
            <State name="normal" offset="0 36 4 24"/>
            <State name="highlighted" offset="32 36 4 24"/>
            <State name="pushed" offset="64 36 4 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 4 24 24" align="Stretch">
            <State name="disabled" offset="4 36 24 24"/>
            <State name="normal" offset="4 36 24 24"/>
            <State name="highlighted" offset="36 36 24 24"/>
            <State name="pushed" offset="68 36 24 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 4 4 24" align="Right VStretch">
            <State name="disabled" offset="28 36 4 24"/>
            <State name="normal" offset="28 36 4 24"/>
            <State name="highlighted" offset="60 36 4 24"/>
            <State name="pushed" offset="92 36 4 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="0 28 4 4" align="Left Bottom">
            <State name="disabled" offset="0 60 4 4"/>
            <State name="normal" offset="0 60 4 4"/>
            <State name="highlighted" offset="32 60 4 4"/>
            <State name="pushed" offset="64 60 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 28 24 4" align="HStretch Bottom">
            <State name="disabled" offset="4 60 24 4"/>
            <State name="normal" offset="4 60 24 4"/>
            <State name="highlighted" offset="36 60 24 4"/>
            <State name="pushed" offset="68 60 24 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 28 4 4" align="Right Bottom">
            <State name="disabled" offset="28 60 4 4"/>
            <State name="normal" offset="28 60 4 4"/>
            <State name="highlighted" offset="60 60 4 4"/>
            <State name="pushed" offset="92 60 4 4"/>
        </BasisSkin>
        <Child type="TextBox" skin="EditClientSkin" offset="5 2 22 28" align="Stretch" name="Client">
            <Property key="Pointer" value="beam"/>
        </Child>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyButtonSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 4 4" align="Left Top">
            <State name="disabled" offset="96 32 4 4"/>
            <State name="normal" offset="128 32 4 4"/>
            <State name="highlighted" offset="160 32 4 4"/>
            <State name="pushed" offset="192 32 4 4"/>
            <State name="disabled_checked" offset="96 32 4 4"/>
            <State name="normal_checked" offset="192 32 4 4"/>
            <State name="highlighted_checked" offset="160 32 4 4"/>
            <State name="pushed_checked" offset="192 32 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 0 24 4" align="HStretch Top">
            <State name="disabled" offset="100 32 24 4"/>
            <State name="normal" offset="132 32 24 4"/>
            <State name="highlighted" offset="164 32 24 4"/>
            <State name="pushed" offset="196 32 24 4"/>
            <State name="disabled_checked" offset="100 32 24 4"/>
            <State name="normal_checked" offset="196 32 24 4"/>
            <State name="highlighted_checked" offset="164 32 24 4"/>
            <State name="pushed_checked" offset="196 32 24 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 0 4 4" align="Right Top">
            <State name="disabled" offset="124 32 4 4"/>
            <State name="normal" offset="156 32 4 4"/>
            <State name="highlighted" offset="188 32 4 4"/>
            <State name="pushed" offset="220 32 4 4"/>
            <State name="disabled_checked" offset="124 32 4 4"/>
            <State name="normal_checked" offset="220 32 4 4"/>
            <State name="highlighted_checked" offset="188 32 4 4"/>
            <State name="pushed_checked" offset="220 32 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="0 4 4 24" align="Left VStretch">
            <State name="disabled" offset="96 36 4 24"/>
            <State name="normal" offset="128 36 4 24"/>
            <State name="highlighted" offset="160 36 4 24"/>
            <State name="pushed" offset="192 36 4 24"/>
            <State name="disabled_checked" offset="96 36 4 24"/>
            <State name="normal_checked" offset="192 36 4 24"/>
            <State name="highlighted_checked" offset="160 36 4 24"/>
            <State name="pushed_checked" offset="192 36 4 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 4 24 24" align="Stretch">
            <State name="disabled" offset="100 36 24 24"/>
            <State name="normal" offset="132 36 24 24"/>
            <State name="highlighted" offset="164 36 24 24"/>
            <State name="pushed" offset="196 36 24 24"/>
            <State name="disabled_checked" offset="100 36 24 24"/>
            <State name="normal_checked" offset="196 36 24 24"/>
            <State name="highlighted_checked" offset="164 36 24 24"/>
            <State name="pushed_checked" offset="196 36 24 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 4 4 24" align="Right VStretch">
            <State name="disabled" offset="124 36 4 24"/>
            <State name="normal" offset="156 36 4 24"/>
            <State name="highlighted" offset="188 36 4 24"/>
            <State name="pushed" offset="220 36 4 24"/>
            <State name="disabled_checked" offset="124 36 4 24"/>
            <State name="normal_checked" offset="220 36 4 24"/>
            <State name="highlighted_checked" offset="188 36 4 24"/>
            <State name="pushed_checked" offset="220 36 4 24"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="0 28 4 4" align="Left Bottom">
            <State name="disabled" offset="96 60 4 4"/>
            <State name="normal" offset="128 60 4 4"/>
            <State name="highlighted" offset="160 60 4 4"/>
            <State name="pushed" offset="192 60 4 4"/>
            <State name="disabled_checked" offset="96 60 4 4"/>
            <State name="normal_checked" offset="192 60 4 4"/>
            <State name="highlighted_checked" offset="160 60 4 4"/>
            <State name="pushed_checked" offset="192 60 4 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="4 28 24 4" align="HStretch Bottom">
            <State name="disabled" offset="100 60 24 4"/>
            <State name="normal" offset="132 60 24 4"/>
            <State name="highlighted" offset="164 60 24 4"/>
            <State name="pushed" offset="196 60 24 4"/>
            <State name="disabled_checked" offset="100 60 24 4"/>
            <State name="normal_checked" offset="196 60 24 4"/>
            <State name="highlighted_checked" offset="164 60 24 4"/>
            <State name="pushed_checked" offset="196 60 24 4"/>
        </BasisSkin>
        <BasisSkin type="SubSkin" offset="28 28 4 4" align="Right Bottom">
            <State name="disabled" offset="124 60 4 4"/>
            <State name="normal" offset="156 60 4 4"/>
            <State name="highlighted" offset="188 60 4 4"/>
            <State name="pushed" offset="220 60 4 4"/>
            <State name="disabled_checked" offset="124 60 4 4"/>
            <State name="normal_checked" offset="220 60 4 4"/>
            <State name="highlighted_checked" offset="188 60 4 4"/>
            <State name="pushed_checked" offset="220 60 4 4"/>
        </BasisSkin>
        <BasisSkin type="SimpleText" offset="4 2 24 28" align="Stretch">
            <State name="disabled" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal" colour="0.88 0.91 0.95" shift="0"/>
            <State name="highlighted" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed" colour="1.0 1.0 1.0" shift="0"/>
            <State name="disabled_checked" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal_checked" colour="0.96 0.98 1.0" shift="0"/>
            <State name="highlighted_checked" colour="1.0 1.0 1.0" shift="0"/>
            <State name="pushed_checked" colour="1.0 1.0 1.0" shift="0"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyFlatButtonSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 32 32" align="Stretch">
            <State name="disabled" offset="194 98 28 28"/>
            <State name="normal" offset="194 98 28 28"/>
            <State name="highlighted" offset="34 66 28 28"/>
            <State name="pushed" offset="66 66 28 28"/>
            <State name="disabled_checked" offset="194 98 28 28"/>
            <State name="normal_checked" offset="2 66 28 28"/>
            <State name="highlighted_checked" offset="34 66 28 28"/>
            <State name="pushed_checked" offset="66 66 28 28"/>
        </BasisSkin>
        <BasisSkin type="SimpleText" offset="6 2 20 28" align="Stretch">
            <State name="disabled" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal" colour="0.88 0.91 0.95" shift="0"/>
            <State name="highlighted" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed" colour="1.0 1.0 1.0" shift="0"/>
            <State name="disabled_checked" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal_checked" colour="0.96 0.98 1.0" shift="0"/>
            <State name="highlighted_checked" colour="1.0 1.0 1.0" shift="0"/>
            <State name="pushed_checked" colour="1.0 1.0 1.0" shift="0"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyHeaderSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 32 32" align="Stretch">
            <State name="disabled" offset="2 66 28 28"/>
            <State name="normal" offset="2 66 28 28"/>
            <State name="highlighted" offset="34 66 28 28"/>
            <State name="pushed" offset="66 66 28 28"/>
            <State name="normal_checked" offset="2 66 28 28"/>
            <State name="highlighted_checked" offset="34 66 28 28"/>
            <State name="pushed_checked" offset="66 66 28 28"/>
        </BasisSkin>
        <BasisSkin type="SimpleText" offset="6 2 20 28" align="Stretch">
            <State name="disabled" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal" colour="0.88 0.91 0.95" shift="0"/>
            <State name="highlighted" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed" colour="1.0 1.0 1.0" shift="0"/>
            <State name="normal_checked" colour="0.88 0.91 0.95" shift="0"/>
            <State name="highlighted_checked" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed_checked" colour="1.0 1.0 1.0" shift="0"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyTabSkin" size="32 32" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 32 32" align="Stretch">
            <State name="disabled" offset="98 66 28 28"/>
            <State name="normal" offset="98 66 28 28"/>
            <State name="highlighted" offset="130 66 28 28"/>
            <State name="pushed" offset="162 66 28 28"/>
            <State name="disabled_checked" offset="162 66 28 28"/>
            <State name="normal_checked" offset="162 66 28 28"/>
            <State name="highlighted_checked" offset="130 66 28 28"/>
            <State name="pushed_checked" offset="162 66 28 28"/>
        </BasisSkin>
        <BasisSkin type="SimpleText" offset="6 2 20 28" align="Stretch">
            <State name="disabled" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal" colour="0.82 0.86 0.92" shift="0"/>
            <State name="highlighted" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed" colour="1.0 1.0 1.0" shift="0"/>
            <State name="disabled_checked" colour="0.88 0.91 0.95" shift="0"/>
            <State name="normal_checked" colour="0.98 0.99 1.0" shift="0"/>
            <State name="highlighted_checked" colour="1.0 1.0 1.0" shift="0"/>
            <State name="pushed_checked" colour="1.0 1.0 1.0" shift="0"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyCheckBoxSkin" size="24 20" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 20 20" align="Left VCenter">
            <State name="disabled" offset="0 96 20 20"/>
            <State name="normal" offset="0 96 20 20"/>
            <State name="highlighted" offset="0 96 20 20"/>
            <State name="pushed" offset="0 96 20 20"/>
            <State name="disabled_checked" offset="24 96 20 20"/>
            <State name="normal_checked" offset="24 96 20 20"/>
            <State name="highlighted_checked" offset="24 96 20 20"/>
            <State name="pushed_checked" offset="24 96 20 20"/>
        </BasisSkin>
        <BasisSkin type="SimpleText" offset="25 0 1 20" align="Stretch">
            <State name="disabled" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal" colour="0.88 0.91 0.95" shift="0"/>
            <State name="highlighted" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed" colour="1.0 1.0 1.0" shift="0"/>
            <State name="disabled_checked" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal_checked" colour="0.88 0.91 0.95" shift="0"/>
            <State name="highlighted_checked" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed_checked" colour="1.0 1.0 1.0" shift="0"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyRadioSkin" size="24 20" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 20 20" align="Left VCenter">
            <State name="disabled" offset="48 96 20 20"/>
            <State name="normal" offset="48 96 20 20"/>
            <State name="highlighted" offset="48 96 20 20"/>
            <State name="pushed" offset="48 96 20 20"/>
            <State name="disabled_checked" offset="72 96 20 20"/>
            <State name="normal_checked" offset="72 96 20 20"/>
            <State name="highlighted_checked" offset="72 96 20 20"/>
            <State name="pushed_checked" offset="72 96 20 20"/>
        </BasisSkin>
        <BasisSkin type="SimpleText" offset="25 0 1 20" align="Stretch">
            <State name="disabled" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal" colour="0.88 0.91 0.95" shift="0"/>
            <State name="highlighted" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed" colour="1.0 1.0 1.0" shift="0"/>
            <State name="disabled_checked" colour="0.48 0.54 0.63" shift="0"/>
            <State name="normal_checked" colour="0.88 0.91 0.95" shift="0"/>
            <State name="highlighted_checked" colour="0.96 0.98 1.0" shift="0"/>
            <State name="pushed_checked" colour="1.0 1.0 1.0" shift="0"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyCloseSkin" size="20 20" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 20 20" align="Stretch">
            <State name="disabled" offset="96 96 20 20"/>
            <State name="normal" offset="96 96 20 20"/>
            <State name="highlighted" offset="120 96 20 20"/>
            <State name="pushed" offset="120 96 20 20"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyScrollTrackSkin" size="12 12" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 12 12" align="Stretch">
            <State name="normal" offset="194 66 28 28"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyScrollThumbSkin" size="12 24" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 12 24" align="Stretch">
            <State name="disabled" offset="225 65 14 30"/>
            <State name="normal" offset="225 65 14 30"/>
            <State name="highlighted" offset="241 65 14 30"/>
            <State name="pushed" offset="225 97 14 30"/>
        </BasisSkin>
    </Resource>
    <Resource type="ResourceSkin" name="LegacyProgressFillSkin" size="16 16" texture="LegacySkinAtlas.png">
        <BasisSkin type="SubSkin" offset="0 0 16 16" align="Stretch">
            <State name="normal" offset="162 98 28 28"/>
        </BasisSkin>
    </Resource>
</MyGUI>
)XML";

constexpr const char* kTemplatesXml = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<MyGUI type="Resource">
    <Resource type="ResourceLayout" name="LegacyTextBox" version="3.2.0">
        <Widget type="Widget" skin="TextBoxSkin" position="0 0 60 20" name="Root">
            <Property key="FontName" value="Default"/>
            <Property key="TextAlign" value="Left VCenter"/>
            <Property key="TextColour" value="0.88 0.91 0.95"/>
            <UserString key="LE_TargetWidgetType" value="TextBox"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyButton" version="3.2.0">
        <Widget type="Widget" skin="LegacyButtonSkin" position="0 0 60 24" name="Root">
            <Property key="FontName" value="Default"/>
            <Property key="TextAlign" value="Center"/>
            <UserString key="LE_TargetWidgetType" value="Button"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyFlatButton" version="3.2.0">
        <Widget type="Widget" skin="LegacyFlatButtonSkin" position="0 0 60 22" name="Root">
            <Property key="FontName" value="Default"/>
            <Property key="TextAlign" value="Left VCenter"/>
            <UserString key="LE_TargetWidgetType" value="Button"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyHeaderButton" version="3.2.0">
        <Widget type="Widget" skin="LegacyHeaderSkin" position="0 0 120 24" name="Root">
            <Property key="FontName" value="Default"/>
            <Property key="TextAlign" value="Left VCenter"/>
            <UserString key="LE_TargetWidgetType" value="Button"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyTabButton" version="3.2.0">
        <Widget type="Widget" skin="LegacyTabSkin" position="0 0 80 24" name="Root">
            <Property key="FontName" value="Default"/>
            <Property key="TextAlign" value="Center"/>
            <UserString key="LE_TargetWidgetType" value="Button"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyCheckBox" version="3.2.0">
        <Widget type="Widget" skin="LegacyCheckBoxSkin" position="0 0 100 22" name="Root">
            <Property key="FontName" value="Default"/>
            <Property key="TextAlign" value="Left VCenter"/>
            <UserString key="LE_TargetWidgetType" value="Button"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyRadioButton" version="3.2.0">
        <Widget type="Widget" skin="LegacyRadioSkin" position="0 0 100 22" name="Root">
            <Property key="FontName" value="Default"/>
            <Property key="TextAlign" value="Left VCenter"/>
            <UserString key="LE_TargetWidgetType" value="Button"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyScrollBarV" version="3.2.0">
        <Widget type="Widget" skin="LegacyScrollTrackSkin" position="0 0 12 100" name="Root">
            <Property key="VerticalAlignment" value="true"/>
            <Property key="MoveToClick" value="true"/>
            <UserString key="LE_TargetWidgetType" value="ScrollBar"/>
            <Widget type="Widget" skin="PanelEmpty" position="0 0 12 100" align="Stretch" name="TrackPlace">
                <Property key="NeedKey" value="false"/>
                <Property key="InheritsPick" value="true"/>
                <Widget type="Button" skin="LegacyScrollThumbSkin" position="1 0 10 24" align="HStretch Top" name="Track">
                    <UserString key="MinTrackSize" value="16"/>
                </Widget>
                <Widget type="Widget" skin="PanelEmpty" position="0 0 12 10" align="HStretch Top" name="FirstPart"/>
                <Widget type="Widget" skin="PanelEmpty" position="0 34 12 66" align="HStretch Top" name="SecondPart"/>
            </Widget>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyScrollBarH" version="3.2.0">
        <Widget type="Widget" skin="LegacyScrollTrackSkin" position="0 0 100 12" name="Root">
            <Property key="VerticalAlignment" value="false"/>
            <Property key="MoveToClick" value="true"/>
            <UserString key="LE_TargetWidgetType" value="ScrollBar"/>
            <Widget type="Widget" skin="PanelEmpty" position="0 0 100 12" align="Stretch" name="TrackPlace">
                <Property key="NeedKey" value="false"/>
                <Property key="InheritsPick" value="true"/>
                <Widget type="Button" skin="LegacyScrollThumbSkin" position="0 1 24 10" align="Left VStretch" name="Track">
                    <UserString key="MinTrackSize" value="16"/>
                </Widget>
                <Widget type="Widget" skin="PanelEmpty" position="0 0 10 12" align="Left VStretch" name="FirstPart"/>
                <Widget type="Widget" skin="PanelEmpty" position="34 0 66 12" align="Left VStretch" name="SecondPart"/>
            </Widget>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyEditBox" version="3.2.0">
        <Widget type="Widget" skin="LegacyFrameSkin" position="0 0 120 24" name="Root">
            <UserString key="LE_TargetWidgetType" value="EditBox"/>
            <Widget type="TextBox" skin="EditClientSkin" position="5 2 110 20" align="Stretch" name="Client">
                <Property key="TextAlign" value="Left VCenter"/>
                <Property key="FontName" value="Default"/>
                <Property key="TextColour" value="0.88 0.91 0.95"/>
            </Widget>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyEditBoxMulti" version="3.2.0">
        <Widget type="Widget" skin="LegacyFrameSkin" position="0 0 240 160" name="Root">
            <Property key="MultiLine" value="true"/>
            <UserString key="LE_TargetWidgetType" value="EditBox"/>
            <Widget type="TextBox" skin="EditClientSkin" position="6 4 216 140" align="Stretch" name="Client">
                <Property key="TextAlign" value="Left Top"/>
                <Property key="FontName" value="Default"/>
                <Property key="TextColour" value="0.88 0.91 0.95"/>
            </Widget>
            <Widget type="ScrollBar" skin="LegacyScrollBarH" position="2 146 224 12" align="HStretch Bottom" name="HScroll"/>
            <Widget type="ScrollBar" skin="LegacyScrollBarV" position="226 2 12 144" align="Right VStretch" name="VScroll"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyScrollView" version="3.2.0">
        <Widget type="Widget" skin="PanelEmpty" position="0 0 120 120" name="Root">
            <UserString key="LE_TargetWidgetType" value="ScrollView"/>
            <Widget type="Widget" skin="PanelEmpty" position="0 0 108 108" align="Stretch" name="Client"/>
            <Widget type="ScrollBar" skin="LegacyScrollBarH" position="0 108 108 12" align="HStretch Bottom" name="HScroll"/>
            <Widget type="ScrollBar" skin="LegacyScrollBarV" position="108 0 12 108" align="Right VStretch" name="VScroll"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyScrollViewBorder" version="3.2.0">
        <Widget type="Widget" skin="LegacyChildSkin" position="0 0 120 120" name="Root">
            <UserString key="LE_TargetWidgetType" value="ScrollView"/>
            <Widget type="Widget" skin="PanelEmpty" position="2 2 104 104" align="Stretch" name="Client"/>
            <Widget type="ScrollBar" skin="LegacyScrollBarH" position="2 106 104 12" align="HStretch Bottom" name="HScroll"/>
            <Widget type="ScrollBar" skin="LegacyScrollBarV" position="106 2 12 104" align="Right VStretch" name="VScroll"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyProgressBar" version="3.2.0">
        <Widget type="Widget" skin="LegacyFrameSkin" position="0 0 120 18" name="Root">
            <UserString key="LE_TargetWidgetType" value="ProgressBar"/>
            <UserString key="TrackSkin" value="LegacyProgressFillSkin"/>
            <UserString key="TrackWidth" value="1"/>
            <UserString key="TrackStep" value="0"/>
            <UserString key="TrackFill" value="true"/>
            <Widget type="Widget" skin="PanelEmpty" position="2 2 116 14" align="Stretch" name="TrackPlace"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyWindow" version="3.2.0">
        <Widget type="Widget" skin="LegacyPanelSkin" position="0 0 240 180" name="Root">
            <Property key="Snap" value="false"/>
            <UserString key="LE_TargetWidgetType" value="Window"/>
            <Widget type="Widget" skin="LegacyTitleActiveSkin" position="1 1 238 24" align="HStretch Top">
                <Widget type="TextBox" skin="LegacyTextBox" position="8 0 204 24" align="HStretch Top" name="Caption">
                    <Property key="TextAlign" value="Left VCenter"/>
                </Widget>
                <Widget type="Button" skin="LegacyCloseSkin" position="216 2 20 20" align="Right VCenter" name="Button">
                    <UserString key="Event" value="close"/>
                </Widget>
            </Widget>
            <Widget type="Widget" skin="PanelEmpty" position="1 25 238 154" align="Stretch" name="Client"/>
            <Widget type="Widget" skin="PanelEmpty" position="228 168 12 12" align="Right Bottom" name="ResizeRightBottom"/>
        </Widget>
    </Resource>
    <Resource type="ResourceLayout" name="LegacyWindowPlain" version="3.2.0">
        <Widget type="Widget" skin="LegacyPanelSkin" position="0 0 240 180" name="Root">
            <Property key="Snap" value="false"/>
            <Property key="Movable" value="false"/>
            <UserString key="LE_TargetWidgetType" value="Window"/>
            <Widget type="Widget" skin="PanelEmpty" position="1 1 238 178" align="Stretch" name="Client"/>
        </Widget>
    </Resource>
</MyGUI>
)XML";

constexpr std::array<unsigned int, 7> kGlModes = {
    GL_BLEND,
    GL_CULL_FACE,
    GL_DEPTH_TEST,
    GL_STENCIL_TEST,
    GL_SCISSOR_TEST,
    GL_RASTERIZER_DISCARD,
    GL_COLOR_LOGIC_OP,
};
constexpr std::array<unsigned int, 6> kGlBlendParams = {
    GL_BLEND_SRC_RGB,
    GL_BLEND_DST_RGB,
    GL_BLEND_SRC_ALPHA,
    GL_BLEND_DST_ALPHA,
    GL_BLEND_EQUATION_RGB,
    GL_BLEND_EQUATION_ALPHA,
};

unsigned compileGlShader(unsigned type, const char* source) {
    const unsigned shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MyGUI shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

uint32_t Color::toAbgr() const {
    return (static_cast<uint32_t>(clampByte(a)) << 24) |
           (static_cast<uint32_t>(clampByte(b)) << 16) |
           (static_cast<uint32_t>(clampByte(g)) << 8) | static_cast<uint32_t>(clampByte(r));
}

Color Color::fromAbgr(uint32_t abgr) {
    return Color(static_cast<float>(abgr & 0xFF) / 255.0f,
                 static_cast<float>((abgr >> 8) & 0xFF) / 255.0f,
                 static_cast<float>((abgr >> 16) & 0xFF) / 255.0f,
                 static_cast<float>((abgr >> 24) & 0xFF) / 255.0f);
}

// ---------------------------------------------------------------------------
// LegacyMyGuiDataManager
// ---------------------------------------------------------------------------

void LegacyMyGuiDataManager::registerMemoryFile(const std::string& name, std::string content) {
    memoryFiles_[name] = std::move(content);
}

void LegacyMyGuiDataManager::addSearchPath(const std::string& path) {
    searchPaths_.push_back(path);
}

MyGUI::IDataStream* LegacyMyGuiDataManager::getData(const std::string& name) const {
    if (const auto it = memoryFiles_.find(name); it != memoryFiles_.end()) {
        return new MemoryDataStream(it->second);
    }
    for (const auto& dir : searchPaths_) {
        const auto full = std::filesystem::path(dir) / name;
        std::ifstream in(full, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            return new MemoryDataStream(ss.str());
        }
    }
    return nullptr;
}

void LegacyMyGuiDataManager::freeData(MyGUI::IDataStream* data) {
    delete data;
}

bool LegacyMyGuiDataManager::isDataExist(const std::string& name) const {
    if (memoryFiles_.count(name)) {
        return true;
    }
    for (const auto& dir : searchPaths_) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(std::filesystem::path(dir) / name, ec)) {
            return true;
        }
    }
    return false;
}

MyGUI::VectorString LegacyMyGuiDataManager::getDataListNames(const std::string& pattern) const {
    MyGUI::VectorString result;
    for (const auto& [name, _] : memoryFiles_) {
        if (pattern.empty() || pattern == "*" || name == pattern) {
            result.push_back(name);
        }
    }
    return result;
}

std::string LegacyMyGuiDataManager::getDataPath(const std::string& name) const {
    if (memoryFiles_.count(name)) {
        return name;
    }
    for (const auto& dir : searchPaths_) {
        const auto full = std::filesystem::path(dir) / name;
        std::error_code ec;
        if (std::filesystem::is_regular_file(full, ec)) {
            return full.string();
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// LegacyMyGuiVertexBuffer
// ---------------------------------------------------------------------------

LegacyMyGuiVertexBuffer::LegacyMyGuiVertexBuffer(bool glEnabled) : glEnabled_(glEnabled) {}

LegacyMyGuiVertexBuffer::~LegacyMyGuiVertexBuffer() {
    destroyGlBuffers();
}

void LegacyMyGuiVertexBuffer::setVertexCount(size_t count) {
    requestedCount_ = count;
}

size_t LegacyMyGuiVertexBuffer::getVertexCount() const {
    return requestedCount_;
}

MyGUI::Vertex* LegacyMyGuiVertexBuffer::lock() {
    if (requestedCount_ > vertices_.size()) {
        vertices_.resize(requestedCount_ + 64);
    }
    if (vertices_.empty()) {
        vertices_.resize(64);
    }
    return vertices_.data();
}

void LegacyMyGuiVertexBuffer::unlock() {
    if (!glEnabled_ || requestedCount_ == 0) {
        return;
    }
    ensureGlBuffers(requestedCount_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER,
                    0,
                    static_cast<GLsizeiptr>(requestedCount_ * sizeof(MyGUI::Vertex)),
                    vertices_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void LegacyMyGuiVertexBuffer::uploadDirect(const MyGUI::Vertex* data, std::size_t count) {
    requestedCount_ = count;
    if (count == 0) {
        return;
    }
    vertices_.assign(data, data + count);
    if (!glEnabled_) {
        return;
    }
    ensureGlBuffers(count);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(
        GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(MyGUI::Vertex)), data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void LegacyMyGuiVertexBuffer::ensureGlBuffers(std::size_t count) {
    if (!glEnabled_) {
        return;
    }
    if (vao_ == 0) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MyGUI::Vertex), nullptr);
        glVertexAttribPointer(1,
                              4,
                              GL_UNSIGNED_BYTE,
                              GL_TRUE,
                              sizeof(MyGUI::Vertex),
                              reinterpret_cast<const void*>(offsetof(MyGUI::Vertex, colour)));
        glVertexAttribPointer(2,
                              2,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(MyGUI::Vertex),
                              reinterpret_cast<const void*>(offsetof(MyGUI::Vertex, u)));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    if (count > gpuCapacity_) {
        gpuCapacity_ = std::max(count, gpuCapacity_ * 2 + 256);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(gpuCapacity_ * sizeof(MyGUI::Vertex)),
                     nullptr,
                     GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void LegacyMyGuiVertexBuffer::destroyGlBuffers() {
    if (glEnabled_) {
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
        }
    }
    vbo_ = 0;
    vao_ = 0;
    gpuCapacity_ = 0;
}

// ---------------------------------------------------------------------------
// LegacyMyGuiTexture
// ---------------------------------------------------------------------------

LegacyMyGuiTexture::LegacyMyGuiTexture(std::string name, bool glEnabled)
    : name_(std::move(name)), glEnabled_(glEnabled) {}

LegacyMyGuiTexture::~LegacyMyGuiTexture() {
    destroy();
}

void LegacyMyGuiTexture::createManual(int width,
                                      int height,
                                      MyGUI::TextureUsage usage,
                                      MyGUI::PixelFormat format) {
    destroy();
    external_ = false;
    flipV_ = false;
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    usage_ = usage;
    format_ = format;
    elemBytes_ = (format == MyGUI::PixelFormat::R8G8B8) ? 3 : 4;
    pixels_.assign(static_cast<std::size_t>(width_) * height_ * elemBytes_, 0);
    if (glEnabled_) {
        uploadToGl();
    }
}

void LegacyMyGuiTexture::loadFromFile(const std::string&) {
    if (width_ == 0 || height_ == 0) {
        createManual(16, 16, MyGUI::TextureUsage::Static, MyGUI::PixelFormat::R8G8B8A8);
        std::fill(pixels_.begin(), pixels_.end(), uint8_t{255});
        if (glEnabled_) {
            uploadToGl();
        }
    }
}

void LegacyMyGuiTexture::saveToFile(const std::string&) {}

void LegacyMyGuiTexture::destroy() {
    if (glEnabled_ && !external_ && glId_ != 0) {
        glDeleteTextures(1, &glId_);
    }
    glId_ = 0;
    externalGlId_ = 0;
    external_ = false;
    locked_ = false;
    width_ = 0;
    height_ = 0;
    pixels_.clear();
}

void* LegacyMyGuiTexture::lock(MyGUI::TextureUsage) {
    locked_ = true;
    return pixels_.data();
}

void LegacyMyGuiTexture::unlock() {
    locked_ = false;
    if (glEnabled_ && !external_ && !pixels_.empty()) {
        uploadToGl();
    }
}

void LegacyMyGuiTexture::wrapExternalGl(unsigned glId, int width, int height, bool flipV) {
    if (glEnabled_ && !external_ && glId_ != 0) {
        glDeleteTextures(1, &glId_);
        glId_ = 0;
    }
    external_ = true;
    externalGlId_ = glId;
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    flipV_ = flipV;
    format_ = MyGUI::PixelFormat::R8G8B8A8;
    elemBytes_ = 4;
}

void LegacyMyGuiTexture::ensureGlUploaded() {
    glEnabled_ = true;
    if (!external_ && !pixels_.empty() && glId_ == 0) {
        uploadToGl();
    }
}

void LegacyMyGuiTexture::uploadToGl() {
    if (!glEnabled_ || external_ || pixels_.empty()) {
        return;
    }
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    if (glId_ == 0) {
        glGenTextures(1, &glId_);
    }
    glBindTexture(GL_TEXTURE_2D, glId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const GLenum fmt = (elemBytes_ == 3) ? GL_RGB : GL_RGBA;
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, fmt, GL_UNSIGNED_BYTE, pixels_.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
}

// ---------------------------------------------------------------------------
// LegacyMergedFont (FreeType Roboto + FontAwesome merged atlas for MyGUI::IFont)
// ---------------------------------------------------------------------------

LegacyMergedFont::~LegacyMergedFont() {
    if (texture_ && MyGUI::RenderManager::getInstancePtr()) {
        MyGUI::RenderManager::getInstance().destroyTexture(texture_);
        texture_ = nullptr;
    }
}

void LegacyMergedFont::deserialization(MyGUI::xml::ElementPtr node, MyGUI::Version version) {
    MyGUI::IResource::deserialization(node, version);
    float size = 18.0f;
    MyGUI::xml::ElementEnumerator prop = node->getElementEnumerator();
    while (prop.next("Property")) {
        const auto key = prop->findAttribute("key");
        const auto val = prop->findAttribute("value");
        if (key == "Size") {
            size = MyGUI::utility::parseFloat(val);
        }
    }
    buildEmbedded(size);
}

void LegacyMergedFont::buildEmbedded(float pixelSize) {
    pixelSize_ = std::max(8.0f, pixelSize);
    defaultHeight_ = static_cast<int>(std::ceil(pixelSize_));
    glyphs_.clear();

    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft) != 0) {
        return;
    }

    FT_Face textFace = nullptr;
    FT_Face iconFace = nullptr;
    const bool hasText = FT_New_Memory_Face(ft,
                                            editorFontData,
                                            static_cast<FT_Long>(sizeof(editorFontData)),
                                            0,
                                            &textFace) == 0;
    const bool hasIcons = FT_New_Memory_Face(ft,
                                             editorIconData,
                                             static_cast<FT_Long>(sizeof(editorIconData)),
                                             0,
                                             &iconFace) == 0;

    if (hasText) {
        FT_Set_Pixel_Sizes(textFace, 0, static_cast<FT_UInt>(pixelSize_));
    }
    if (hasIcons) {
        FT_Set_Pixel_Sizes(iconFace, 0, static_cast<FT_UInt>(pixelSize_));
    }

    const int ascender =
        hasText ? static_cast<int>(textFace->size->metrics.ascender >> 6) : static_cast<int>(pixelSize_ * 0.8f);
    const int lineHeight =
        hasText ? std::max(defaultHeight_, static_cast<int>(textFace->size->metrics.height >> 6))
                : defaultHeight_;
    defaultHeight_ = lineHeight;

    constexpr int kAtlasW = 1024;
    constexpr int kAtlasH = 1024;
    std::vector<uint8_t> atlas(static_cast<std::size_t>(kAtlasW) * kAtlasH * 4, 0);
    // Reserve top-left 4x4 as solid white pixel for underline/fallback if needed.
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * kAtlasW + x) * 4;
            atlas[idx + 0] = atlas[idx + 1] = atlas[idx + 2] = atlas[idx + 3] = 255;
        }
    }

    int penX = 6;
    int penY = 2;
    int shelfH = 4;

    auto packGlyph = [&](FT_Face face, uint32_t cp, float minAdvance, bool centerIcon) {
        if (!face) {
            return;
        }
        const FT_UInt glyphIndex = FT_Get_Char_Index(face, cp);
        if (glyphIndex == 0 && cp != ' ') {
            return;
        }
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER) != 0) {
            return;
        }
        const FT_GlyphSlot slot = face->glyph;
        const int bw = static_cast<int>(slot->bitmap.width);
        const int bh = static_cast<int>(slot->bitmap.rows);

        if (penX + bw + 2 >= kAtlasW) {
            penX = 2;
            penY += shelfH + 2;
            shelfH = 0;
        }
        if (penY + bh + 2 >= kAtlasH) {
            return;
        }

        for (int row = 0; row < bh; ++row) {
            const uint8_t* srcRow = slot->bitmap.buffer + row * slot->bitmap.pitch;
            uint8_t* dstRow =
                atlas.data() + (static_cast<std::size_t>(penY + row) * kAtlasW + penX) * 4;
            for (int col = 0; col < bw; ++col) {
                dstRow[col * 4 + 0] = 255;
                dstRow[col * 4 + 1] = 255;
                dstRow[col * 4 + 2] = 255;
                dstRow[col * 4 + 3] = srcRow[col];
            }
        }

        float advance = static_cast<float>(slot->advance.x) / 64.0f;
        float bearingX = static_cast<float>(slot->bitmap_left);
        if (minAdvance > 0.0f && advance < minAdvance) {
            if (centerIcon) {
                bearingX += (minAdvance - advance) * 0.5f;
            }
            advance = minAdvance;
        }
        const float bearingY = static_cast<float>(ascender - slot->bitmap_top);
        const MyGUI::FloatRect uv(static_cast<float>(penX) / kAtlasW,
                                  static_cast<float>(penY) / kAtlasH,
                                  static_cast<float>(penX + bw) / kAtlasW,
                                  static_cast<float>(penY + bh) / kAtlasH);
        glyphs_.emplace(cp,
                        MyGUI::GlyphInfo(static_cast<MyGUI::Char>(cp),
                                         static_cast<float>(bw),
                                         static_cast<float>(bh),
                                         advance,
                                         bearingX,
                                         bearingY,
                                         uv));

        penX += bw + 2;
        shelfH = std::max(shelfH, bh);
    };

    if (hasText) {
        for (uint32_t cp = 0x0020; cp <= 0x00FF; ++cp) {
            packGlyph(textFace, cp, 0.0f, false);
        }
        for (uint32_t cp = 0x2013; cp <= 0x2026; ++cp) {
            packGlyph(textFace, cp, 0.0f, false);
        }
        // Tab glyph
        if (const auto it = glyphs_.find(' '); it != glyphs_.end()) {
            MyGUI::GlyphInfo tab = it->second;
            tab.codePoint = '\t';
            tab.advance = it->second.advance * 4.0f;
            glyphs_.emplace('\t', tab);
        }
    }

    if (hasIcons) {
        FT_UInt gindex = 0;
        FT_ULong charcode = FT_Get_First_Char(iconFace, &gindex);
        while (gindex != 0) {
            if (charcode >= 0xf000 && charcode <= 0xf8ff) {
                packGlyph(iconFace, static_cast<uint32_t>(charcode), 20.0f, true);
            }
            charcode = FT_Get_Next_Char(iconFace, charcode, &gindex);
        }
    }

    if (const auto it = glyphs_.find('?'); it != glyphs_.end()) {
        fallbackGlyph_ = it->second;
    } else if (const auto sp = glyphs_.find(' '); sp != glyphs_.end()) {
        fallbackGlyph_ = sp->second;
    }

    if (hasText) {
        FT_Done_Face(textFace);
    }
    if (hasIcons) {
        FT_Done_Face(iconFace);
    }
    FT_Done_FreeType(ft);

    auto& rm = MyGUI::RenderManager::getInstance();
    if (texture_) {
        rm.destroyTexture(texture_);
        texture_ = nullptr;
    }
    const std::string texName = "LegacyMergedFontAtlas_" + getResourceName();
    if (auto* existing = rm.getTexture(texName)) {
        rm.destroyTexture(existing);
    }
    texture_ = rm.createTexture(texName);
    texture_->createManual(
        kAtlasW, kAtlasH, MyGUI::TextureUsage::Static, MyGUI::PixelFormat::R8G8B8A8);
    if (void* dst = texture_->lock(MyGUI::TextureUsage::Write)) {
        std::memcpy(dst, atlas.data(), atlas.size());
        texture_->unlock();
    }
}

const MyGUI::GlyphInfo* LegacyMergedFont::getGlyphInfo(MyGUI::Char id) const {
    if (const auto it = glyphs_.find(static_cast<uint32_t>(id)); it != glyphs_.end()) {
        return &it->second;
    }
    return &fallbackGlyph_;
}

bool LegacyMergedFont::hasGlyph(uint32_t codepoint) const {
    return glyphs_.find(codepoint) != glyphs_.end();
}

float LegacyMergedFont::measureWidth(std::string_view utf8, float fontSize) const {
    const float scale = (fontSize > 0.0f && pixelSize_ > 0.0f) ? (fontSize / pixelSize_) : 1.0f;
    float width = 0.0f;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    while (p < end) {
        const uint32_t cp = nextUtf8(p, end);
        if (cp == '\n') {
            break;
        }
        const auto* g = getGlyphInfo(static_cast<MyGUI::Char>(cp));
        if (g) {
            width += g->advance * scale;
        }
    }
    return width;
}

// ---------------------------------------------------------------------------
// LegacyMyGuiRenderManager
// ---------------------------------------------------------------------------

LegacyMyGuiRenderManager::LegacyMyGuiRenderManager(bool headless) : headless_(headless) {
    setViewSize(1280, 720);
}

LegacyMyGuiRenderManager::~LegacyMyGuiRenderManager() {
    shutdown();
}

bool LegacyMyGuiRenderManager::initGl() {
    if (glReady_) {
        return true;
    }
    headless_ = false;
    const unsigned vs = compileGlShader(GL_VERTEX_SHADER, kMyGuiVertexShader);
    const unsigned fs = compileGlShader(GL_FRAGMENT_SHADER, kMyGuiFragmentShader);
    if (!vs || !fs) {
        if (vs) {
            glDeleteShader(vs);
        }
        if (fs) {
            glDeleteShader(fs);
        }
        return false;
    }
    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, vs);
    glAttachShader(shaderProgram_, fs);
    glLinkProgram(shaderProgram_);
    glDetachShader(shaderProgram_, vs);
    glDetachShader(shaderProgram_, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    int linked = 0;
    glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024]{};
        glGetProgramInfoLog(shaderProgram_, sizeof(log), nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MyGUI shader link error: %s", log);
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
        return false;
    }

    const int uTex = glGetUniformLocation(shaderProgram_, "Texture");
    uYScale_ = glGetUniformLocation(shaderProgram_, "YScale");
    uFlipV_ = glGetUniformLocation(shaderProgram_, "FlipV");
    GLint prev = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev);
    glUseProgram(shaderProgram_);
    glUniform1i(uTex, 0);
    glUniform1f(uYScale_, 1.0f);
    glUniform1f(uFlipV_, 0.0f);
    glUseProgram(static_cast<GLuint>(prev));

    glReady_ = true;
    overlayVb_ = std::make_unique<LegacyMyGuiVertexBuffer>(true);
    for (auto& [_, tex] : textures_) {
        tex->ensureGlUploaded();
    }
    needsUpdate_ = true;
    return true;
}

void LegacyMyGuiRenderManager::shutdown() {
    overlayVb_.reset();
    textures_.clear();
    if (glReady_ && shaderProgram_ != 0) {
        glDeleteProgram(shaderProgram_);
    }
    shaderProgram_ = 0;
    glReady_ = false;
}

MyGUI::IVertexBuffer* LegacyMyGuiRenderManager::createVertexBuffer() {
    return new LegacyMyGuiVertexBuffer(glReady_);
}

void LegacyMyGuiRenderManager::destroyVertexBuffer(MyGUI::IVertexBuffer* buffer) {
    delete buffer;
}

MyGUI::ITexture* LegacyMyGuiRenderManager::createTexture(const std::string& name) {
    auto tex = std::make_unique<LegacyMyGuiTexture>(name, glReady_);
    auto* ptr = tex.get();
    textures_[name] = std::move(tex);
    return ptr;
}

void LegacyMyGuiRenderManager::destroyTexture(MyGUI::ITexture* texture) {
    if (!texture) {
        return;
    }
    textures_.erase(texture->getName());
}

MyGUI::ITexture* LegacyMyGuiRenderManager::getTexture(const std::string& name) {
    const auto it = textures_.find(name);
    return it != textures_.end() ? it->second.get() : nullptr;
}

bool LegacyMyGuiRenderManager::isFormatSupported(MyGUI::PixelFormat format, MyGUI::TextureUsage) {
    return format == MyGUI::PixelFormat::R8G8B8A8 || format == MyGUI::PixelFormat::R8G8B8;
}

void LegacyMyGuiRenderManager::setViewSize(int width, int height) {
    viewSize_.set(std::max(1, width), std::max(1, height));
    if (fbWidth_ <= 1 || fbHeight_ <= 1 || headless_) {
        fbWidth_ = viewSize_.width;
        fbHeight_ = viewSize_.height;
    }
    info_.maximumDepth = 1.0f;
    info_.hOffset = 0.0f;
    info_.vOffset = 0.0f;
    info_.aspectCoef = static_cast<float>(viewSize_.height) / static_cast<float>(viewSize_.width);
    info_.pixScaleX = 1.0f / static_cast<float>(viewSize_.width);
    info_.pixScaleY = 1.0f / static_cast<float>(viewSize_.height);
    if (MyGUI::LayerManager::getInstancePtr()) {
        onResizeView(viewSize_);
    }
    needsUpdate_ = true;
}

void LegacyMyGuiRenderManager::setFramebufferSize(int fbWidth, int fbHeight) {
    fbWidth_ = std::max(1, fbWidth);
    fbHeight_ = std::max(1, fbHeight);
    fbScaleX_ = static_cast<float>(fbWidth_) / static_cast<float>(std::max(1, viewSize_.width));
    fbScaleY_ = static_cast<float>(fbHeight_) / static_cast<float>(std::max(1, viewSize_.height));
}

void LegacyMyGuiRenderManager::setFramebufferScale(float sx, float sy) {
    fbScaleX_ = std::max(0.25f, sx);
    fbScaleY_ = std::max(0.25f, sy);
    fbWidth_ = std::max(1, static_cast<int>(std::lround(viewSize_.width * fbScaleX_)));
    fbHeight_ = std::max(1, static_cast<int>(std::lround(viewSize_.height * fbScaleY_)));
}

void LegacyMyGuiRenderManager::registerShader(const std::string&,
                                              const std::string&,
                                              const std::string&) {}

MyGUI::ITexture*
LegacyMyGuiRenderManager::wrapExternalTexture(unsigned glId, int width, int height, bool flipV) {
    const std::string key =
        "__ext_gl_" + std::to_string(glId) + (flipV ? "_flip" : "_norm");
    auto it = textures_.find(key);
    LegacyMyGuiTexture* tex = nullptr;
    if (it == textures_.end()) {
        auto created = std::make_unique<LegacyMyGuiTexture>(key, glReady_);
        tex = created.get();
        textures_[key] = std::move(created);
    } else {
        tex = it->second.get();
    }
    tex->wrapExternalGl(glId, width, height, flipV);
    return tex;
}

MyGUI::ITexture* LegacyMyGuiRenderManager::whiteTexture() {
    auto* tex = getTexture("LegacyWhitePixel");
    if (!tex) {
        tex = createTexture("LegacyWhitePixel");
        tex->createManual(4, 4, MyGUI::TextureUsage::Static, MyGUI::PixelFormat::R8G8B8A8);
        if (auto* p = static_cast<uint8_t*>(tex->lock(MyGUI::TextureUsage::Write))) {
            std::memset(p, 255, 4 * 4 * 4);
            tex->unlock();
        }
    }
    return tex;
}

void LegacyMyGuiRenderManager::updateSkinAtlas(const SkinAtlasPalette& p) {
    constexpr int kSize = 256;
    auto* tex = getTexture("LegacySkinAtlas.png");
    if (!tex) {
        tex = createTexture("LegacySkinAtlas.png");
        tex->createManual(kSize, kSize, MyGUI::TextureUsage::Dynamic, MyGUI::PixelFormat::R8G8B8A8);
    }
    auto* buf = static_cast<uint8_t*>(tex->lock(MyGUI::TextureUsage::Write));
    if (!buf) {
        return;
    }
    std::memset(buf, 0, static_cast<std::size_t>(kSize) * kSize * 4);

    auto setPixel = [&](int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) {
            return;
        }
        const std::size_t idx = (static_cast<std::size_t>(y) * kSize + x) * 4;
        buf[idx + 0] = clampByte(c.r);
        buf[idx + 1] = clampByte(c.g);
        buf[idx + 2] = clampByte(c.b);
        buf[idx + 3] = clampByte(c.a);
    };

    auto applyGloss = [](Color base, float t) {
        const float highlight = 0.15f * (1.0f - t);
        const float shade = 0.13f * t;
        return Color(std::clamp(base.r + (1.0f - base.r) * highlight - base.r * shade, 0.0f, 1.0f),
                     std::clamp(base.g + (1.0f - base.g) * highlight - base.g * shade, 0.0f, 1.0f),
                     std::clamp(base.b + (1.0f - base.b) * highlight - base.b * shade, 0.0f, 1.0f),
                     base.a);
    };

    auto fillBox = [&](int rx,
                       int ry,
                       int rw,
                       int rh,
                       Color fill,
                       Color borderCol,
                       float radius,
                       bool hasBorder,
                       bool glossy) {
        for (int y = 0; y < rh; ++y) {
            const float t = rh > 1 ? static_cast<float>(y) / static_cast<float>(rh - 1) : 0.5f;
            const Color rowFill = glossy ? applyGloss(fill, t) : fill;
            for (int x = 0; x < rw; ++x) {
                float dx = 0.0f;
                float dy = 0.0f;
                const float r = std::max(0.0f, std::min(radius, std::min(rw, rh) * 0.45f));
                if (x < r) {
                    dx = r - x - 0.5f;
                } else if (x >= rw - r) {
                    dx = x - (rw - r) + 0.5f;
                }
                if (y < r) {
                    dy = r - y - 0.5f;
                } else if (y >= rh - r) {
                    dy = y - (rh - r) + 0.5f;
                }
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (r > 0.5f && dist > r) {
                    continue;
                }
                const bool edge = hasBorder && (x == 0 || y == 0 || x == rw - 1 || y == rh - 1 ||
                                                (r > 0.5f && dist >= r - 1.25f));
                setPixel(rx + x, ry + y, edge ? borderCol : rowFill);
            }
        }
    };

    // (0,0,16,16) Solid white
    fillBox(0, 0, 16, 16, Color(1, 1, 1, 1), Color(1, 1, 1, 1), 0.0f, false, false);
    // (16,0,32,32) Window panel
    fillBox(16, 0, 32, 32, p.windowBg, p.border, 0.0f, true, false);
    // (48,0,32,32) Child panel
    fillBox(48, 0, 32, 32, p.childBg, p.border, 3.0f, true, false);
    // (80,0,32,32) Popup panel
    fillBox(80, 0, 32, 32, p.popupBg, p.border, 4.0f, true, false);
    // (112,0,32,32) Title inactive
    fillBox(112, 0, 32, 32, p.titleBg, p.border, 0.0f, false, false);
    // (144,0,32,32) Title active
    fillBox(144, 0, 32, 32, p.titleBgActive, p.border, 0.0f, false, false);
    // (176,0,32,32) Menu bar
    fillBox(176, 0, 32, 32, p.menuBarBg, p.border, 0.0f, false, false);

    // Row 2: Frames & Buttons
    const bool frameBorder = p.frameBorderSize > 0.5f;
    fillBox(0, 32, 32, 32, p.frameBg, p.border, p.frameRounding, frameBorder, p.glossy);
    fillBox(32, 32, 32, 32, p.frameBgHovered, p.border, p.frameRounding, frameBorder, p.glossy);
    fillBox(64, 32, 32, 32, p.frameBgActive, p.border, p.frameRounding, true, p.glossy);
    Color disabledBtn = p.button;
    disabledBtn.a *= 0.55f;
    fillBox(96, 32, 32, 32, disabledBtn, p.border, p.frameRounding, frameBorder, false);
    fillBox(128, 32, 32, 32, p.button, p.border, p.frameRounding, frameBorder, p.glossy);
    fillBox(160, 32, 32, 32, p.buttonHovered, p.border, p.frameRounding, frameBorder, p.glossy);
    fillBox(192, 32, 32, 32, p.buttonActive, p.border, p.frameRounding, frameBorder, p.glossy);

    // Row 3: Headers, Tabs, Scrollbars
    fillBox(0, 64, 32, 32, p.header, p.border, 2.0f, false, false);
    fillBox(32, 64, 32, 32, p.headerHovered, p.border, 2.0f, false, false);
    fillBox(64, 64, 32, 32, p.headerActive, p.border, 2.0f, false, false);
    fillBox(96, 64, 32, 32, p.tab, p.border, 3.0f, false, false);
    fillBox(128, 64, 32, 32, p.tabHovered, p.border, 3.0f, false, false);
    fillBox(160, 64, 32, 32, p.tabSelected, p.border, 3.0f, false, false);
    fillBox(192, 64, 32, 32, p.scrollbarBg, p.border, 0.0f, false, false);
    fillBox(224, 64, 16, 32, p.scrollbarGrab, p.border, 4.0f, false, false);
    fillBox(240, 64, 16, 32, p.scrollbarGrabHovered, p.border, 4.0f, false, false);
    fillBox(224, 96, 16, 32, p.scrollbarGrabActive, p.border, 4.0f, false, false);

    // Row 4: CheckBox, Radio, Close, ProgressFill
    fillBox(0, 96, 20, 20, p.frameBg, p.border, 3.0f, true, p.glossy);
    fillBox(24, 96, 20, 20, p.frameBg, p.border, 3.0f, true, p.glossy);
    // Crisp checkmark inside (24, 96, 20, 20)
    auto thickLine = [&](int ox, int oy, float x0, float y0, float x1, float y1, Color c) {
        const int steps = 24;
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            const int px = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
            const int py = static_cast<int>(std::lround(y0 + (y1 - y0) * t));
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx * dx + dy * dy <= 1) {
                        setPixel(ox + px + dx, oy + py + dy, c);
                    }
                }
            }
        }
    };
    thickLine(24, 96, 4.5f, 10.5f, 8.5f, 14.5f, p.checkMark);
    thickLine(24, 96, 8.5f, 14.5f, 15.5f, 5.5f, p.checkMark);

    // Radio buttons (48,96) unchecked, (72,96) checked
    auto drawCircle = [&](int ox, int oy, float radius, Color fill, Color ring) {
        for (int y = 0; y < 20; ++y) {
            for (int x = 0; x < 20; ++x) {
                const float dx = x - 9.5f;
                const float dy = y - 9.5f;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d <= radius) {
                    setPixel(ox + x, oy + y, d >= radius - 1.3f ? ring : fill);
                }
            }
        }
    };
    drawCircle(48, 96, 8.5f, p.frameBg, p.border);
    drawCircle(72, 96, 8.5f, p.frameBg, p.border);
    drawCircle(72, 96, 4.2f, p.checkMark, p.checkMark);

    // Close button (96,96) normal, (120,96) hover
    fillBox(96, 96, 20, 20, Color(0, 0, 0, 0), Color(0, 0, 0, 0), 3.0f, false, false);
    thickLine(96, 96, 6, 6, 14, 14, p.textDisabled);
    thickLine(96, 96, 14, 6, 6, 14, p.textDisabled);
    fillBox(120, 96, 20, 20, p.buttonHovered, p.border, 3.0f, false, false);
    thickLine(120, 96, 6, 6, 14, 14, Color(1, 1, 1, 1));
    thickLine(120, 96, 14, 6, 6, 14, Color(1, 1, 1, 1));

    // Progress fill (160,96,32,32)
    fillBox(160, 96, 32, 32, p.checkMark, p.checkMark, 2.0f, false, p.glossy);

    tex->unlock();
    needsUpdate_ = true;
}

void LegacyMyGuiRenderManager::begin() {
    if (!glReady_) {
        ++beginDepth_;
        return;
    }
    if (beginDepth_++ == 0) {
        for (std::size_t i = 0; i < kGlModes.size(); ++i) {
            savedState_.enabled[i] = glIsEnabled(kGlModes[i]) == GL_TRUE;
        }
        glGetIntegerv(GL_POLYGON_MODE, savedState_.polygonMode.data());
        for (std::size_t i = 0; i < kGlBlendParams.size(); ++i) {
            glGetIntegerv(kGlBlendParams[i], &savedState_.blend[i]);
        }
        glGetBooleanv(GL_COLOR_WRITEMASK, savedState_.colorMask.data());
        glGetBooleanv(GL_DEPTH_WRITEMASK, &savedState_.depthMask);
        glGetIntegerv(GL_CURRENT_PROGRAM, &savedState_.program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &savedState_.vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedState_.vbo);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &savedState_.activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedState_.texture);
        glGetIntegerv(GL_VIEWPORT, savedState_.viewport.data());
        glGetIntegerv(GL_SCISSOR_BOX, savedState_.scissor.data());
    }

    glViewport(0, 0, fbWidth_, fbHeight_);
    glUseProgram(shaderProgram_);
    glActiveTexture(GL_TEXTURE0);
    for (std::size_t i = 1; i < kGlModes.size(); ++i) {
        glDisable(kGlModes[i]);
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void LegacyMyGuiRenderManager::end() {
    if (--beginDepth_ != 0 || !glReady_) {
        return;
    }
    for (std::size_t i = 0; i < kGlModes.size(); ++i) {
        if (savedState_.enabled[i]) {
            glEnable(kGlModes[i]);
        } else {
            glDisable(kGlModes[i]);
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, savedState_.polygonMode[0]);
    glDepthMask(savedState_.depthMask);
    glColorMask(savedState_.colorMask[0],
                savedState_.colorMask[1],
                savedState_.colorMask[2],
                savedState_.colorMask[3]);
    glBlendFuncSeparate(
        savedState_.blend[0], savedState_.blend[1], savedState_.blend[2], savedState_.blend[3]);
    glBlendEquationSeparate(savedState_.blend[4], savedState_.blend[5]);
    glViewport(savedState_.viewport[0],
               savedState_.viewport[1],
               savedState_.viewport[2],
               savedState_.viewport[3]);
    glScissor(savedState_.scissor[0],
              savedState_.scissor[1],
              savedState_.scissor[2],
              savedState_.scissor[3]);
    glUseProgram(static_cast<GLuint>(savedState_.program));
    glBindVertexArray(static_cast<GLuint>(savedState_.vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(savedState_.vbo));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(savedState_.texture));
    glActiveTexture(static_cast<GLenum>(savedState_.activeTexture));
}

void LegacyMyGuiRenderManager::doRender(MyGUI::IVertexBuffer* buffer,
                                        MyGUI::ITexture* texture,
                                        size_t count) {
    lastFrameVertexCount_ += count;
    if (!glReady_ || !buffer || count == 0) {
        return;
    }
    auto* vb = static_cast<LegacyMyGuiVertexBuffer*>(buffer);
    if (vb->vao() == 0) {
        return;
    }
    const auto* tex = static_cast<const LegacyMyGuiTexture*>(texture);
    glUseProgram(shaderProgram_);
    glUniform1f(uYScale_, 1.0f);
    glUniform1f(uFlipV_, (tex && tex->flipV()) ? 1.0f : 0.0f);
    glBindTexture(GL_TEXTURE_2D, tex ? tex->glTextureId() : 0);
    glBindVertexArray(vb->vao());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void LegacyMyGuiRenderManager::clearOverlays() {
    overlays_.clear();
}

void LegacyMyGuiRenderManager::submitOverlay(OverlayDrawBatch batch) {
    if (!batch.vertices.empty() && !batch.commands.empty()) {
        overlays_.push_back(std::move(batch));
    }
}

void LegacyMyGuiRenderManager::renderOverlayBatches(int targetLayerOrder) {
    if (overlays_.empty()) {
        return;
    }
    if (!overlayVb_) {
        overlayVb_ = std::make_unique<LegacyMyGuiVertexBuffer>(glReady_);
    }
    for (const auto& batch : overlays_) {
        if (batch.layerOrder != targetLayerOrder || batch.vertices.empty()) {
            continue;
        }
        overlayVb_->uploadDirect(batch.vertices.data(), batch.vertices.size());
        for (const auto& cmd : batch.commands) {
            if (cmd.vertexCount == 0) {
                continue;
            }
            lastFrameVertexCount_ += cmd.vertexCount;
            if (!glReady_) {
                continue;
            }
            if (cmd.hasClip) {
                const int sx0 = std::max(0, static_cast<int>(std::floor(cmd.clip.minX * fbScaleX_)));
                const int sy0 = std::max(0, static_cast<int>(std::floor(cmd.clip.minY * fbScaleY_)));
                const int sx1 =
                    std::min(fbWidth_, static_cast<int>(std::ceil(cmd.clip.maxX * fbScaleX_)));
                const int sy1 =
                    std::min(fbHeight_, static_cast<int>(std::ceil(cmd.clip.maxY * fbScaleY_)));
                if (sx1 <= sx0 || sy1 <= sy0) {
                    continue;
                }
                glEnable(GL_SCISSOR_TEST);
                glScissor(sx0, fbHeight_ - sy1, sx1 - sx0, sy1 - sy0);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }
            unsigned glTex = cmd.externalGlTexture;
            bool flipV = cmd.flipV;
            if (glTex == 0 && cmd.texture) {
                const auto* t = static_cast<const LegacyMyGuiTexture*>(cmd.texture);
                glTex = t->glTextureId();
                flipV = flipV || t->flipV();
            }
            if (glTex == 0) {
                const auto* wt = static_cast<const LegacyMyGuiTexture*>(whiteTexture());
                glTex = wt ? wt->glTextureId() : 0;
            }
            glUseProgram(shaderProgram_);
            glUniform1f(uYScale_, 1.0f);
            glUniform1f(uFlipV_, flipV ? 1.0f : 0.0f);
            glBindTexture(GL_TEXTURE_2D, glTex);
            glBindVertexArray(overlayVb_->vao());
            glDrawArrays(GL_TRIANGLES,
                         static_cast<GLint>(cmd.vertexOffset),
                         static_cast<GLsizei>(cmd.vertexCount));
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (glReady_) {
            glDisable(GL_SCISSOR_TEST);
        }
    }
}

void LegacyMyGuiRenderManager::drawOneFrame(float dt) {
    if (!MyGUI::Gui::getInstancePtr()) {
        return;
    }
    lastFrameVertexCount_ = 0;
    onFrameEvent(dt);
    begin();
    try {
        renderOverlayBatches(0);
        onRenderToTarget(this, needsUpdate_);
        renderOverlayBatches(1);
        renderOverlayBatches(2);
    } catch (...) {
        end();
        throw;
    }
    end();
    needsUpdate_ = false;
}

void LegacyMyGuiRenderManager::renderStandaloneOverlay(int fbWidth,
                                                       int fbHeight,
                                                       const OverlayDrawBatch& batch) {
    if (!glReady_ || batch.vertices.empty() || batch.commands.empty()) {
        return;
    }
    const int prevW = fbWidth_;
    const int prevH = fbHeight_;
    const float prevSX = fbScaleX_;
    const float prevSY = fbScaleY_;
    fbWidth_ = std::max(1, fbWidth);
    fbHeight_ = std::max(1, fbHeight);
    fbScaleX_ = 1.0f;
    fbScaleY_ = 1.0f;
    begin();
    if (!overlayVb_) {
        overlayVb_ = std::make_unique<LegacyMyGuiVertexBuffer>(true);
    }
    overlayVb_->uploadDirect(batch.vertices.data(), batch.vertices.size());
    for (const auto& cmd : batch.commands) {
        if (cmd.vertexCount == 0) {
            continue;
        }
        unsigned glTex = cmd.externalGlTexture;
        bool flipV = cmd.flipV;
        if (glTex == 0 && cmd.texture) {
            const auto* texture = static_cast<const LegacyMyGuiTexture*>(cmd.texture);
            glTex = texture->glTextureId();
            flipV = flipV || texture->flipV();
        }
        if (glTex == 0) {
            glTex = static_cast<const LegacyMyGuiTexture*>(whiteTexture())->glTextureId();
        }
        glUseProgram(shaderProgram_);
        glUniform1f(uYScale_, 1.0f);
        glUniform1f(uFlipV_, flipV ? 1.0f : 0.0f);
        glBindTexture(GL_TEXTURE_2D, glTex);
        glBindVertexArray(overlayVb_->vao());
        glDrawArrays(GL_TRIANGLES,
                     static_cast<GLint>(cmd.vertexOffset),
                     static_cast<GLsizei>(cmd.vertexCount));
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    end();
    fbWidth_ = prevW;
    fbHeight_ = prevH;
    fbScaleX_ = prevSX;
    fbScaleY_ = prevSY;
}

// ---------------------------------------------------------------------------
// LegacyMyGuiPlatform
// ---------------------------------------------------------------------------

LegacyMyGuiPlatform::LegacyMyGuiPlatform(bool headless) {
    logManager_ = std::make_unique<MyGUI::LogManager>();
    logManager_->setSTDOutputEnabled(false);
    static SdlLogListener sdlLog;
    auto* source = new MyGUI::LogSource();
    source->addLogListener(&sdlLog);
    logManager_->addLogSource(source);

    dataManager_ = std::make_unique<LegacyMyGuiDataManager>();
    renderManager_ = std::make_unique<LegacyMyGuiRenderManager>(headless);
    registerEmbeddedResources();

    gui_ = std::make_unique<MyGUI::Gui>();
    gui_->initialise("MyGUI_Core.xml");

    MyGUI::FactoryManager::getInstance().registerFactory<LegacyMergedFont>("Resource");

    renderManager_->updateSkinAtlas(SkinAtlasPalette{});
    MyGUI::ResourceManager::getInstance().load("MyGUI_Layers.xml");
    MyGUI::ResourceManager::getInstance().load("LegacyFonts.xml");
    MyGUI::ResourceManager::getInstance().load("LegacySkins.xml");
    MyGUI::ResourceManager::getInstance().load("LegacyTemplates.xml");

    if (auto* font = MyGUI::FontManager::getInstance().getByName("Default")) {
        defaultFont_ = font->castType<LegacyMergedFont>(false);
    }
    MyGUI::PointerManager::getInstance().setVisible(false);

    MyGUI::ClipboardManager::getInstance().eventClipboardChanged +=
        MyGUI::newDelegate(this, &LegacyMyGuiPlatform::onClipboardChanged);
    MyGUI::ClipboardManager::getInstance().eventClipboardRequested +=
        MyGUI::newDelegate(this, &LegacyMyGuiPlatform::onClipboardRequested);
}

LegacyMyGuiPlatform::~LegacyMyGuiPlatform() {
    if (MyGUI::ClipboardManager::getInstancePtr()) {
        MyGUI::ClipboardManager::getInstance().eventClipboardChanged -=
            MyGUI::newDelegate(this, &LegacyMyGuiPlatform::onClipboardChanged);
        MyGUI::ClipboardManager::getInstance().eventClipboardRequested -=
            MyGUI::newDelegate(this, &LegacyMyGuiPlatform::onClipboardRequested);
    }
    if (gui_) {
        gui_->shutdown();
        gui_.reset();
    }
    if (renderManager_) {
        renderManager_->shutdown();
        renderManager_.reset();
    }
    dataManager_.reset();
    logManager_.reset();
}

void LegacyMyGuiPlatform::onClipboardChanged(std::string_view type, std::string_view data) {
    if (type == "Text") {
        const std::string text(data);
        if (clipboardSet_) {
            clipboardSet_(text);
        } else {
            SDL_SetClipboardText(text.c_str());
        }
    }
}

void LegacyMyGuiPlatform::onClipboardRequested(std::string_view type, std::string& data) {
    if (type == "Text") {
        if (clipboardGet_) {
            data = clipboardGet_();
        } else if (char* s = SDL_GetClipboardText()) {
            data = s;
            SDL_free(s);
        }
    }
}

void LegacyMyGuiPlatform::registerEmbeddedResources() {
    dataManager_->registerMemoryFile(
        "MyGUI_Core.xml", R"XML(<?xml version="1.0" encoding="UTF-8"?><MyGUI type="List"/>)XML");
    dataManager_->registerMemoryFile("MyGUI_Layers.xml", kLayersXml);
    dataManager_->registerMemoryFile("LegacyFonts.xml", kFontsXml);
    dataManager_->registerMemoryFile("LegacySkins.xml", kSkinsXml);
    dataManager_->registerMemoryFile("LegacyTemplates.xml", kTemplatesXml);
}

bool LegacyMyGuiPlatform::initGl(SDL_Window* window) {
    if (!renderManager_->initGl()) {
        return false;
    }
    if (window) {
        int ww = 1280, wh = 720, fw = 1280, fh = 720;
        SDL_GetWindowSize(window, &ww, &wh);
        SDL_GetWindowSizeInPixels(window, &fw, &fh);
        setViewSize(ww, wh, fw, fh);
    }
    return true;
}

void LegacyMyGuiPlatform::setClipboardCallbacks(std::function<void(const std::string&)> setter,
                                                std::function<std::string()> getter) {
    clipboardSet_ = std::move(setter);
    clipboardGet_ = std::move(getter);
}

void LegacyMyGuiPlatform::setViewSize(int width, int height, int fbWidth, int fbHeight) {
    renderManager_->setViewSize(std::max(1, width), std::max(1, height));
    if (fbWidth > 0 && fbHeight > 0) {
        renderManager_->setFramebufferSize(fbWidth, fbHeight);
    } else {
        renderManager_->setFramebufferSize(std::max(1, width), std::max(1, height));
    }
}

void LegacyMyGuiPlatform::updateSkinAtlas(const SkinAtlasPalette& palette) {
    renderManager_->updateSkinAtlas(palette);
}

} // namespace ui
