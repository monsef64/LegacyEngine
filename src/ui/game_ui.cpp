#include "game_ui.hpp"
#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_GL3.h"
#include "editor_font.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/EventListener.h>
#include <algorithm>
#include <filesystem>
#include <set>
#include <stb_image.h>
#include <stdexcept>
#include <vector>

namespace {
GameUI* instance = nullptr;
// Share the engine image decoder rather than the sample backend's TGA-only
// loader. Generated UI textures use premultiplied RGBA, as required by GL3.
class GameRenderer : public RenderInterface_GL3 {
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        auto* files = Rml::GetFileInterface();
        const auto file = files->Open(source);
        if (!file) {
            return 0;
        }
        const auto length = files->Length(file);
        if (length == 0 || length > 64 * 1024 * 1024) {
            files->Close(file);
            return 0;
        }
        std::vector<unsigned char> encoded(length);
        const auto read = files->Read(encoded.data(), length, file);
        files->Close(file);
        if (read != length) {
            return 0;
        }
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(encoded.data(), int(length), &w, &h, &channels) || w <= 0 ||
            h <= 0 || w > 8192 || h > 8192) {
            return 0;
        }
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(encoded.data(), int(length), &w, &h, &channels, 4),
            stbi_image_free);
        if (!pixels) {
            return 0;
        }
        for (size_t i = 0; i < size_t(w) * size_t(h) * 4; i += 4) {
            for (int channel = 0; channel < 3; ++channel) {
                pixels.get()[i + channel] =
                    (unsigned(pixels.get()[i + channel]) * pixels.get()[i + 3] + 127) / 255;
            }
        }
        dimensions = {w, h};
        return GenerateTexture({pixels.get(), size_t(w) * size_t(h) * 4}, dimensions);
    }
};
} // namespace
struct GameUI::Impl : Rml::EventListener {
    SystemInterface_SDL system;
    std::unique_ptr<GameRenderer> renderer;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    bool initialized = false, attempted = false;
    std::filesystem::path root;
    std::set<std::string> clicks;
    float x = 0, y = 0, w = 1, h = 1;
    int pw = 1, ph = 1;
    void ProcessEvent(Rml::Event& event) override {
        // Bubble nested label clicks to the nearest element with an id.
        for (auto* element = event.GetTargetElement(); element;
             element = element->GetParentNode()) {
            if (!element->GetId().empty()) {
                if (clicks.size() < 256) {
                    clicks.insert(element->GetId());
                }
                break;
            }
        }
    }
    void close() {
        if (context) {
            Rml::RemoveContext("legacy-game");
            Rml::ReleaseTextures();
        }
        context = nullptr;
        document = nullptr;
        clicks.clear();
        attempted = false;
    }
};
GameUI::GameUI() : impl_(std::make_unique<Impl>()) {
    instance = this;
}
GameUI::~GameUI() {
    impl_->system.SetWindow(nullptr);
    impl_->close();
    if (impl_->initialized) {
        Rml::Shutdown();
    }
    if (instance == this) {
        instance = nullptr;
    }
}
GameUI* GameUI::current() {
    return instance;
}
bool GameUI::loaded() const {
    return impl_->document != nullptr;
}
void GameUI::sync(bool active, const std::filesystem::path& assetRoot, SDL_Window* window) {
    auto& ui = *impl_;
    ui.system.SetWindow(window);
    if (!active || ui.root != assetRoot) {
        ui.close();
        ui.root = assetRoot;
    }
    if (!active) {
        return;
    }
    if (ui.attempted) {
        return;
    }
    ui.attempted = true;
    auto path = std::filesystem::absolute(std::filesystem::path(assetRoot) / "UI/main.rml");
    if (!std::filesystem::is_regular_file(path)) {
        return;
    }
    if (!ui.initialized) {
        ui.renderer = std::make_unique<GameRenderer>();
        if (!*ui.renderer) {
            throw std::runtime_error("RmlUi OpenGL renderer initialization failed");
        }
        Rml::SetSystemInterface(&ui.system);
        Rml::SetRenderInterface(ui.renderer.get());
        if (!Rml::Initialise()) {
            throw std::runtime_error("RmlUi initialization failed");
        }
        ui.initialized = true;
        if (!Rml::LoadFontFace({editorFontData, sizeof(editorFontData)},
                               "Roboto",
                               Rml::Style::FontStyle::Normal)) {
            throw std::runtime_error("RmlUi embedded font could not load");
        }
    }
    ui.context = Rml::CreateContext("legacy-game", {ui.pw, ui.ph});
    if (!ui.context) {
        throw std::runtime_error("RmlUi context creation failed");
    }
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
    ui.document = ui.context->LoadDocument(path.generic_string());
    if (!ui.document) {
        SDL_Log("RmlUi: cannot load %s; check RML/RCSS and restart Play", path.string().c_str());
        return;
    }
    ui.document->AddEventListener("click", &ui);
    ui.document->Show();
}
void GameUI::viewport(float x, float y, float w, float h, int pw, int ph) {
    auto& ui = *impl_;
    ui.x = x;
    ui.y = y;
    ui.w = std::max(w, 1.f);
    ui.h = std::max(h, 1.f);
    ui.pw = std::max(pw, 1);
    ui.ph = std::max(ph, 1);
    if (ui.context) {
        ui.context->SetDimensions({ui.pw, ui.ph});
        ui.context->SetDensityIndependentPixelRatio(float(ui.pw) / ui.w);
    }
}
bool GameUI::event(SDL_Event event, SDL_Window* window, bool focused, bool mouseLocked) {
    auto& ui = *impl_;
    if (!ui.document) {
        return false;
    }
    if (!focused || mouseLocked || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        ui.context->ProcessMouseLeave();
        for (int button = 0; button < 3; ++button) {
            ui.context->ProcessMouseButtonUp(button, 0);
        }
        if (auto* focus = ui.context->GetFocusElement()) {
            focus->Blur();
        }
        return false;
    }
    const bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
    const bool button =
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    const bool wheel = event.type == SDL_EVENT_MOUSE_WHEEL;
    if (motion || button || wheel) {
        float x = 0, y = 0;
        if (motion) {
            x = event.motion.x;
            y = event.motion.y;
        } else if (button) {
            x = event.button.x;
            y = event.button.y;
        } else {
            SDL_GetMouseState(&x, &y);
        }
        if (x < ui.x || y < ui.y || x >= ui.x + ui.w || y >= ui.y + ui.h) {
            ui.context->ProcessMouseLeave();
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                ui.context->ProcessMouseButtonUp(RmlSDL::ConvertMouseButton(event.button.button),
                                                 RmlSDL::GetKeyModifierState());
            }
            ui.context->ProcessMouseLeave();
            return false;
        }
        const auto px = int((x - ui.x) * ui.pw / ui.w);
        const auto py = int((y - ui.y) * ui.ph / ui.h);
        ui.context->ProcessMouseMove(px, py, RmlSDL::GetKeyModifierState());
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !ui.context->IsMouseInteracting()) {
            if (auto* focus = ui.context->GetFocusElement()) {
                focus->Blur();
            }
        }
        if (motion) {
            return ui.context->IsMouseInteracting();
        }
    }
    const bool keyboard = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
                          event.type == SDL_EVENT_TEXT_INPUT;
    // Do not let SDL window resize events override the embedded Game viewport.
    if (!motion && !button && !wheel && !keyboard) {
        return false;
    }
    const bool editing = capturesKeyboard();
    return !RmlSDL::InputEventHandler(ui.context, window, event) || (keyboard && editing);
}
void GameUI::render(int width, int height, bool update) {
    auto& ui = *impl_;
    if (!ui.document) {
        return;
    }
    ui.context->SetDimensions({width, height});
    if (update) {
        ui.context->Update();
    }
    ui.renderer->SetViewport(width, height);
    ui.renderer->BeginFrame();
    ui.context->Render();
    ui.renderer->EndFrame();
}
bool GameUI::setText(const std::string& id, const std::string& text) {
    if (!impl_->document) {
        return false;
    }
    auto* element = impl_->document->GetElementById(id);
    if (!element) {
        return false;
    }
    // Plain text, not markup injection.
    std::string escaped;
    for (char c : text) {
        if (c == '&') {
            escaped += "&amp;";
        } else if (c == '<') {
            escaped += "&lt;";
        } else if (c == '>') {
            escaped += "&gt;";
        } else {
            escaped += c;
        }
    }
    element->SetInnerRML(escaped);
    return true;
}
bool GameUI::takeClick(const std::string& id) {
    return impl_->clicks.erase(id) != 0;
}

bool GameUI::capturesKeyboard() const {
    if (!impl_->document) {
        return false;
    }
    auto* focus = impl_->context->GetFocusElement();
    if (!focus) {
        return false;
    }
    const auto& tag = focus->GetTagName();
    return tag == "input" || tag == "textarea" || tag == "select";
}

bool GameUI::capturesMouse() const {
    return impl_->document && impl_->context->IsMouseInteracting();
}
