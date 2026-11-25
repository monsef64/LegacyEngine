#pragma once
#include "assets/assets.hpp"
#include "rendering/scene_surface.hpp"
#include "ui/game_ui.hpp"
#include <RmlUi/Core.h>
#include <cstring>
#include <fstream>
#include <functional>
#include <imgui.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Edits RmlUi's own DOM, not a parallel layout format. Preview uses the same
// OpenGL backend, fonts, stylesheets and image loader as the player.
class CanvasEditor {
    std::filesystem::path root_;
    std::string rml_ = "UI/Canvas.rml", rcss_ = "UI/Canvas.rcss", error_;
    std::string head_ = "<rml><head><title>Canvas</title></head>";
    std::string source_ = "<rml><head><title>Canvas</title></head><body></body></rml>";
    std::string css_ =
        "body { width: 100%; height: 100%; font-family: Roboto; font-size: 18px; color: #eeeeee; }\nbutton, select, input { background-color: #35445a; padding: 8px; }\n";
    Rml::ElementDocument* doc_ = nullptr;
    Rml::Element* selected_ = nullptr;
    SceneSurface surface_;
    bool dirty_ = false, reload_ = true;
    int width_ = 1280, height_ = 720, nextId_ = 1;
    static bool text(const char* label, std::string& value, bool multiline = false) {
        std::vector<char> buffer(
            std::max<std::size_t>(value.size() + 4096, multiline ? 65536 : 4096));
        std::copy(value.begin(), value.end(), buffer.begin());
        bool changed =
            multiline ? ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), {-1, 160})
                      : ImGui::InputText(label, buffer.data(), buffer.size());
        if (changed) {
            value = buffer.data();
        }
        return changed;
    }
    static std::string read(const std::filesystem::path& path) {
        if (std::filesystem::file_size(path) > 4 * 1024 * 1024) {
            throw std::runtime_error("Canvas source exceeds 4 MiB");
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Cannot read Canvas source");
        }
        return {std::istreambuf_iterator<char>(input), {}};
    }
    static void write(const std::filesystem::path& path, const std::string& data) {
        std::filesystem::create_directories(path.parent_path());
        auto temp = path;
        temp += ".canvas-tmp-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        try {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(data.data(), std::streamsize(data.size()));
            out.close();
            if (!out) {
                throw std::runtime_error("Cannot save Canvas file");
            }
            // C++ filesystem rename cannot replace an existing file on Windows.
#ifdef _WIN32
            if (!MoveFileExW(temp.c_str(),
                             path.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                throw std::runtime_error("Cannot replace Canvas file");
            }
#else
            std::filesystem::rename(temp, path);
#endif
        } catch (...) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            throw;
        }
    }
    void capture() {
        if (!doc_) {
            return;
        }
        std::string body = "<body";
        for (const auto& attribute : doc_->GetAttributes()) {
            if (attribute.first == "style") {
                continue;
            }
            body += " " + attribute.first + "=\"" +
                    Rml::StringUtilities::EncodeRml(attribute.second.Get<Rml::String>()) + "\"";
        }
        std::string style;
        for (const auto& property : doc_->GetLocalStyleProperties()) {
            style += Rml::StyleSheetSpecification::GetPropertyName(property.first) + ": " +
                     property.second.ToString() + "; ";
        }
        if (!style.empty()) {
            body += " style=\"" + Rml::StringUtilities::EncodeRml(style) + "\"";
        }
        body += ">" + doc_->GetInnerRML() + "</body>";
        source_ = head_ + body + "</rml>";
        dirty_ = true;
    }
    void rememberHead() {
        std::string lower = source_;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        auto body = lower.find("<body");
        if (body == std::string::npos || lower.find("<rml") == std::string::npos) {
            throw std::runtime_error("Expected an RML document with <rml> and <body>");
        }
        head_ = source_.substr(0, body);
    }
    void tree(Rml::Element* e) {
        if (!e || e->GetTagName() == "#text") {
            return;
        }
        ImGui::PushID(e);
        std::string name = e == doc_ ? "Canvas" : e->GetTagName();
        if (!e->GetId().empty()) {
            name += " #" + e->GetId();
        }
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
        if (e == selected_) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (!e->GetNumChildren()) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        bool open = ImGui::TreeNodeEx(name.c_str(), flags);
        if (ImGui::IsItemClicked()) {
            selected_ = e;
        }
        if (open) {
            for (int i = 0; i < e->GetNumChildren(); ++i) {
                tree(e->GetChild(i));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    Rml::Element* hit(Rml::Element* e, float x, float y) {
        for (int i = e->GetNumChildren() - 1; i >= 0; --i) {
            auto* child = e->GetChild(i);
            if (child->GetTagName() == "#text") {
                continue;
            }
            if (auto* found = hit(child, x, y)) {
                return found;
            }
        }
        const auto p = e->GetAbsoluteOffset(Rml::BoxArea::Border),
                   size = e->GetBox().GetSize(Rml::BoxArea::Border);
        return x >= p.x && y >= p.y && x <= p.x + size.x && y <= p.y + size.y ? e : nullptr;
    }
    void add(const std::string& tag, float x, float y) {
        if (!doc_ || doc_->GetNumChildren() > 2048) {
            return;
        }
        auto node = doc_->CreateElement(tag);
        if (!node) {
            return;
        }
        std::string id;
        do {
            id = "element" + std::to_string(nextId_++);
        } while (doc_->GetElementById(id));
        node->SetId(id);
        node->SetProperty("position", "absolute");
        node->SetProperty("left", std::to_string(x) + "px");
        node->SetProperty("top", std::to_string(y) + "px");
        node->SetProperty("width", tag == "img" ? "160px" : "200px");
        node->SetProperty("height", tag == "img" ? "100px" : "44px");
        if (tag == "button") {
            node->SetInnerRML("Button");
        } else if (tag == "p") {
            node->SetInnerRML("Text");
        } else if (tag == "select") {
            node->SetInnerRML(
                "<option value=\"one\">Option one</option><option value=\"two\">Option two</option>");
        } else if (tag == "ul") {
            node->SetInnerRML("<li>Item one</li><li>Item two</li>");
            node->SetProperty("height", "100px");
        } else if (tag == "input") {
            node->SetAttribute("type", "text");
        } else if (tag == "div") {
            node->SetProperty("background-color", "#263445");
            node->SetProperty("height", "140px");
        }
        selected_ = doc_->AppendChild(std::move(node));
        capture();
    }

  public:
    bool visible = false;
    bool hasUnsavedChanges() const {
        return dirty_;
    }
    const std::string& error() const {
        return error_;
    }
    bool saveIfDirty(AssetManager& assets) {
        if (!dirty_) {
            return true;
        }
        try {
            if (root_ != assets.root) {
                throw std::runtime_error("Save/discard Canvas draft before changing projects");
            }
            if (AssetManager::extension(rml_) != ".rml" ||
                AssetManager::extension(rcss_) != ".rcss") {
                throw std::runtime_error("Canvas paths must end in .rml and .rcss");
            }
            if (source_.size() > 4 * 1024 * 1024 || css_.size() > 4 * 1024 * 1024) {
                throw std::runtime_error("Canvas exceeds 4 MiB");
            }
            const auto rml = assets.resolve(rml_), rcss = assets.resolve(rcss_);
            const bool existed = std::filesystem::exists(rml);
            const auto previous = existed ? read(rml) : std::string{};
            write(rml, source_);
            try {
                write(rcss, css_);
            } catch (...) {
                if (existed) {
                    write(rml, previous);
                } else {
                    std::filesystem::remove(rml);
                }
                throw;
            }
            dirty_ = false;
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    void draw(AssetManager& assets,
              GameUI& ui,
              SDL_Window* window,
              World& world,
              Entity selected,
              bool runtime) {
        if (!visible) {
            return;
        }
        ImGui::SetNextWindowSize({1200, 780}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Canvas - Visual UI Maker", &visible)) {
            ImGui::End();
            return;
        }
        if (root_.empty()) {
            root_ = assets.root;
        }
        if (root_ != assets.root && !dirty_) {
            root_ = assets.root;
            doc_ = nullptr;
            selected_ = nullptr;
            ui.previewClose();
            rml_ = "UI/Canvas.rml";
            rcss_ = "UI/Canvas.rcss";
            source_ = "<rml><head><title>Canvas</title></head><body></body></rml>";
            rememberHead();
            reload_ = true;
        }
        ImGui::BeginDisabled(runtime || root_ != assets.root);
        text("RML file", rml_);
        text("RCSS file", rcss_);
        if (ImGui::Button("New Canvas") && !dirty_) {
            rml_ = "UI/NewCanvas.rml";
            rcss_ = "UI/NewCanvas.rcss";
            source_ = "<rml><head><title>Canvas</title></head><body></body></rml>";
            rememberHead();
            reload_ = true;
            dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save RML + RCSS")) {
            dirty_ = true;
            saveIfDirty(assets);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load") && !dirty_) {
            try {
                auto source = read(assets.resolve(rml_)), css = read(assets.resolve(rcss_));
                source_ = std::move(source);
                css_ = std::move(css);
                rememberHead();
                reload_ = true;
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Use selected Canvas") && !dirty_) {
            if (auto* g = world.tryGet<UI>(selected)) {
                rml_ = g->rml;
                rcss_ = g->rcss;
                try {
                    source_ = read(assets.resolve(rml_));
                    css_ = read(assets.resolve(rcss_));
                    rememberHead();
                    reload_ = true;
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Assign to selected") && saveIfDirty(assets)) {
            if (auto* g = world.tryGet<UI>(selected)) {
                g->rml = rml_;
                g->rcss = rcss_;
            } else {
                error_ = "Select a GameObject with a Canvas component";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard draft")) {
            dirty_ = false;
            root_ = assets.root;
            selected_ = nullptr;
            doc_ = nullptr;
            ui.previewClose();
            source_ = "<rml><head><title>Canvas</title></head><body></body></rml>";
            try {
                if (std::filesystem::exists(assets.resolve(rml_))) {
                    source_ = read(assets.resolve(rml_));
                }
                if (std::filesystem::exists(assets.resolve(rcss_))) {
                    css_ = read(assets.resolve(rcss_));
                }
                rememberHead();
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
            reload_ = true;
        }
        if (dirty_) {
            ImGui::TextUnformatted(
                "Unsaved Canvas changes - save or discard before loading another document.");
        }
        if (root_ != assets.root) {
            ImGui::TextWrapped(
                "Draft belongs to another project. Discard it to edit this project.");
        }
        if (!error_.empty()) {
            ImGui::TextWrapped("%s", error_.c_str());
        }
        ImGui::BeginDisabled(runtime || root_ != assets.root);
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Width", &width_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Height", &height_);
        width_ = std::clamp(width_, 160, 4096);
        height_ = std::clamp(height_, 120, 4096);
        if (reload_ && root_ == assets.root) {
            try {
                doc_ = ui.previewLoad(source_,
                                      css_,
                                      assets.resolve(rml_).generic_string(),
                                      assets.resolve(rcss_).generic_string(),
                                      window);
                selected_ = nullptr;
                reload_ = false;
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
                reload_ = false;
            }
        }
        ImGui::BeginChild("Canvas toolbox", {260, 0}, ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Drag elements onto the Canvas");
        for (const auto* tag : {"button", "img", "p", "ul", "select", "input", "div"}) {
            const char* label = std::string(tag) == "img"      ? "Image"
                                : std::string(tag) == "p"      ? "Text"
                                : std::string(tag) == "ul"     ? "List"
                                : std::string(tag) == "select" ? "Dropdown"
                                : std::string(tag) == "div"    ? "Panel"
                                                               : tag;
            if (ImGui::Button(label, {-1, 0})) {
                add(tag, 30, 30);
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("CANVAS_ELEMENT", tag, std::strlen(tag) + 1);
                ImGui::TextUnformatted(label);
                ImGui::EndDragDropSource();
            }
        }
        if (doc_) {
            tree(doc_);
        }
        if (selected_) {
            ImGui::SeparatorText("Element");
            auto id = selected_->GetId();
            if (text("ID", id)) {
                auto* existing = id.empty() ? nullptr : doc_->GetElementById(id);
                if (existing && existing != selected_) {
                    error_ = "Element ID already exists in this Canvas";
                } else {
                    selected_->SetId(id);
                    capture();
                }
            }
            auto classes = selected_->GetClassNames();
            if (text("Classes", classes)) {
                selected_->SetClassNames(classes);
                capture();
            }
            if (selected_->GetTagName() == "img") {
                auto src = selected_->GetAttribute<Rml::String>("src", "");
                if (text("Image source", src)) {
                    selected_->SetAttribute("src", src);
                    capture();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        try {
                            const std::string path = static_cast<const char*>(payload->Data);
                            if (materialTexturePath(path)) {
                                const auto relative = assets.resolve(path).lexically_relative(
                                    assets.resolve(rml_).parent_path());
                                selected_->SetAttribute("src", relative.generic_string());
                                capture();
                            }
                        } catch (const std::exception& e) {
                            error_ = e.what();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::TextWrapped("Image paths are relative to the RML file.");
            }
            for (const char* prop :
                 {"left", "top", "width", "height", "color", "background-color", "font-size"}) {
                auto* p = selected_->GetLocalProperty(prop);
                std::string value = p ? p->ToString() : "";
                if (text(prop, value)) {
                    if (value.empty()) {
                        selected_->RemoveProperty(prop);
                    } else {
                        selected_->SetProperty(prop, value);
                    }
                    capture();
                }
            }
            auto content = selected_->GetInnerRML();
            if (text("Content RML", content, true)) {
                selected_->SetInnerRML(content);
                capture();
            }
            if (selected_ != doc_ && ImGui::Button("Delete element")) {
                auto* parent = selected_->GetParentNode();
                parent->RemoveChild(selected_);
                selected_ = nullptr;
                capture();
            }
        }
        if (ImGui::CollapsingHeader("RCSS source")) {
            if (text("##CanvasCSS", css_, true)) {
                dirty_ = true;
                capture();
            }
            if (ImGui::Button("Apply RCSS preview")) {
                reload_ = true;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Canvas preview", {0, 0}, ImGuiChildFlags_Borders);
        if (doc_) {
            GLint framebuffer, viewport[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
            glGetIntegerv(GL_VIEWPORT, viewport);
            surface_.resize(width_, height_);
            glBindFramebuffer(GL_FRAMEBUFFER, surface_.framebuffer);
            const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
            GLboolean colorMask[4];
            glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glViewport(0, 0, width_, height_);
            glClearColor(.055f, .065f, .08f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ui.previewRender(width_, height_);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
            if (scissor) {
                glEnable(GL_SCISSOR_TEST);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }
            const auto room = ImGui::GetContentRegionAvail();
            const float scale = std::max(.01f, std::min(room.x / width_, room.y / height_));
            const auto origin = ImGui::GetCursorScreenPos();
            ImGui::Image(ImTextureRef(ImTextureID(surface_.texture)),
                         {width_ * scale, height_ * scale},
                         {0, 1},
                         {1, 0});
            const auto mouse = ImGui::GetIO().MousePos;
            const float x = (mouse.x - origin.x) / scale, y = (mouse.y - origin.y) / scale;
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                selected_ = hit(doc_, x, y);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(0) && selected_ &&
                selected_ != doc_) {
                auto p = selected_->GetAbsoluteOffset(Rml::BoxArea::Border);
                auto parent = selected_->GetParentNode()->GetAbsoluteOffset();
                selected_->SetProperty("position", "absolute");
                selected_->SetProperty(
                    "left",
                    std::to_string(p.x - parent.x + ImGui::GetIO().MouseDelta.x / scale) + "px");
                selected_->SetProperty(
                    "top",
                    std::to_string(p.y - parent.y + ImGui::GetIO().MouseDelta.y / scale) + "px");
                capture();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("CANVAS_ELEMENT")) {
                    add(static_cast<const char*>(payload->Data), x, y);
                }
                ImGui::EndDragDropTarget();
            }
            if (selected_) {
                const auto p = selected_->GetAbsoluteOffset(Rml::BoxArea::Border),
                           s = selected_->GetBox().GetSize(Rml::BoxArea::Border);
                ImGui::GetWindowDrawList()->AddRect(
                    {origin.x + p.x * scale, origin.y + p.y * scale},
                    {origin.x + (p.x + s.x) * scale, origin.y + (p.y + s.y) * scale},
                    IM_COL32(255, 195, 65, 255),
                    0,
                    0,
                    2);
            }
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
        ImGui::End();
    }
};
