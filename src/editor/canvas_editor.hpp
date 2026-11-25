#pragma once
#include "assets/assets.hpp"
#include "editor/asset_picker.hpp"
#include "rendering/scene_surface.hpp"
#include "ui/game_ui.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <cstring>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <utility>
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
    ImGuiID numberEdit_ = 0;
    bool numberFocus_ = false, moving_ = false;
    int resize_ = -1;
    ImVec2 dragMouse_{}, dragPosition_{}, dragSize_{};
    Rml::Element* inspected_ = nullptr;
    Rml::Element *pendingNode_ = nullptr, *pendingParent_ = nullptr;
    bool pendingWrap_ = false;
    int pendingOrder_ = 0;
    std::string pendingTag_;
    static const char* title(const std::string& tag) {
        return tag == "button"   ? "Button"
               : tag == "img"    ? "Image"
               : tag == "p"      ? "Text"
               : tag == "ul"     ? "List"
               : tag == "select" ? "Dropdown"
               : tag == "input"  ? "Input"
               : tag == "div"    ? "Panel"
               : tag == "li"     ? "List Item"
               : tag == "option" ? "Option"
                                 : nullptr;
    }
    bool number(const char* label, float& value, float low, float high) {
        const auto id = ImGui::GetID(label);
        bool changed = false;
        if (numberEdit_ == id) {
            const bool focusing = numberFocus_;
            if (focusing) {
                ImGui::SetKeyboardFocusHere();
                numberFocus_ = false;
            }
            changed =
                ImGui::InputFloat(label, &value, 0, 0, "%.1f", ImGuiInputTextFlags_AutoSelectAll);
            if ((!focusing && !ImGui::IsItemActive()) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                numberEdit_ = 0;
            }
        } else {
            changed = ImGui::SliderFloat(label, &value, low, high, "%.1f");
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                numberEdit_ = id;
                numberFocus_ = true;
            }
        }
        if (changed) {
            value = std::isfinite(value) ? std::clamp(value, low, high) : low;
        }
        return changed;
    }
    void pixels(Rml::Element* element, const char* property, float value) {
        element->SetProperty(property, std::to_string(value) + "px");
    }
    ImVec2 localPosition(Rml::Element* e) {
        const auto p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
        auto* parent = e->GetParentNode();
        const auto origin =
            parent ? parent->GetAbsoluteOffset(Rml::BoxArea::Padding) : Rml::Vector2f{};
        const auto& box = e->GetBox();
        return {p.x - origin.x - box.GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Left),
                p.y - origin.y - box.GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Top)};
    }
    bool contains(Rml::Element* node, Rml::Element* candidate, int depth = 0) {
        if (!node || depth > 128) {
            return false;
        }
        if (node == candidate) {
            return true;
        }
        for (int i = 0; i < node->GetNumChildren(); ++i) {
            if (contains(node->GetChild(i), candidate, depth + 1)) {
                return true;
            }
        }
        return false;
    }
    bool canParent(Rml::Element* parent) {
        if (!parent) {
            return false;
        }
        const auto tag = parent->GetTagName();
        return tag != "#text" && tag != "img" && tag != "input" && tag != "select" &&
               tag != "option";
    }
    void reparent(Rml::Element* node, Rml::Element* parent) {
        if (!node || node == doc_ || !canParent(parent) || !contains(doc_, node) ||
            !contains(doc_, parent) || contains(node, parent)) {
            error_ = "Cannot parent an element to itself, its child, or a non-container";
            return;
        }
        const auto absolute = node->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = node->GetBox().GetSize(Rml::BoxArea::Content);
        auto* old = node->GetParentNode();
        parent->AppendChild(old->RemoveChild(node));
        node->SetProperty("position", "absolute");
        pixels(node, "width", size.x);
        pixels(node, "height", size.y);
        doc_->GetContext()->Update();
        const auto origin = parent->GetAbsoluteOffset(Rml::BoxArea::Padding);
        pixels(node,
               "left",
               absolute.x - origin.x -
                   node->GetBox().GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Left));
        pixels(node,
               "top",
               absolute.y - origin.y -
                   node->GetBox().GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Top));
        selected_ = node;
        capture();
    }
    static Rml::Element* sibling(Rml::Element* node, bool next) {
        auto* result = next ? node->GetNextSibling() : node->GetPreviousSibling();
        while (result && result->GetTagName() == "#text") {
            result = next ? result->GetNextSibling() : result->GetPreviousSibling();
        }
        return result;
    }
    void applyTreeAction() {
        if (!pendingTag_.empty()) {
            auto tag = std::exchange(pendingTag_, {});
            add(tag, 20, 20, pendingParent_);
        } else if (pendingNode_ && contains(doc_, pendingNode_)) {
            if (pendingWrap_ && pendingNode_ != doc_) {
                auto* child = pendingNode_;
                auto* parent = child->GetParentNode();
                const auto position = localPosition(child);
                const auto size = child->GetBox().GetSize(Rml::BoxArea::Border);
                auto wrapper = doc_->CreateElement("div");
                wrapper->SetProperty("position", "absolute");
                pixels(wrapper.get(), "left", position.x);
                pixels(wrapper.get(), "top", position.y);
                pixels(wrapper.get(), "width", size.x);
                pixels(wrapper.get(), "height", size.y);
                auto* inserted = parent->InsertBefore(std::move(wrapper), child);
                doc_->GetContext()->Update();
                reparent(child, inserted);
                selected_ = inserted;
            } else if (pendingOrder_ && pendingNode_ != doc_) {
                auto* node = pendingNode_;
                auto* parent = node->GetParentNode();
                auto* adjacent = sibling(node, pendingOrder_ > 0);
                if (adjacent) {
                    auto* before = pendingOrder_ < 0 ? adjacent : adjacent->GetNextSibling();
                    auto owned = parent->RemoveChild(node);
                    if (before) {
                        parent->InsertBefore(std::move(owned), before);
                    } else {
                        parent->AppendChild(std::move(owned));
                    }
                    capture();
                }
            } else if (pendingParent_) {
                reparent(pendingNode_, pendingParent_);
            }
        }
        pendingNode_ = pendingParent_ = nullptr;
        pendingWrap_ = false;
        pendingOrder_ = 0;
    }

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
        const auto* label = title(e->GetTagName());
        std::string name = e == doc_ ? "Canvas" : label ? label : e->GetTagName();
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
        if (e != doc_ && ImGui::BeginDragDropSource()) {
            auto* pointer = e;
            ImGui::SetDragDropPayload("CANVAS_NODE", &pointer, sizeof(pointer));
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        if (canParent(e) && ImGui::BeginDragDropTarget()) {
            if (auto* payload = ImGui::AcceptDragDropPayload("CANVAS_NODE")) {
                pendingNode_ = *static_cast<Rml::Element* const*>(payload->Data);
                pendingParent_ = e;
            }
            if (auto* payload = ImGui::AcceptDragDropPayload("CANVAS_ELEMENT")) {
                pendingTag_ = static_cast<const char*>(payload->Data);
                pendingParent_ = e;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("Element actions")) {
            if (canParent(e) && ImGui::BeginMenu("Create Child")) {
                for (const auto* tag : {"button", "img", "p", "ul", "select", "input", "div"}) {
                    if (ImGui::MenuItem(title(tag))) {
                        pendingTag_ = tag;
                        pendingParent_ = e;
                    }
                }
                ImGui::EndMenu();
            }
            if (e != doc_) {
                if (ImGui::MenuItem(
                        "Create Parent (Panel)", nullptr, false, canParent(e->GetParentNode()))) {
                    pendingNode_ = e;
                    pendingWrap_ = true;
                }
                if (ImGui::MenuItem("Move to Canvas", nullptr, false, e->GetParentNode() != doc_)) {
                    pendingNode_ = e;
                    pendingParent_ = doc_;
                }
                if (ImGui::MenuItem("Move Up", nullptr, false, sibling(e, false) != nullptr)) {
                    pendingNode_ = e;
                    pendingOrder_ = -1;
                }
                if (ImGui::MenuItem("Move Down", nullptr, false, sibling(e, true) != nullptr)) {
                    pendingNode_ = e;
                    pendingOrder_ = 1;
                }
            }
            ImGui::EndPopup();
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
    void add(const std::string& tag, float x, float y, Rml::Element* parent = nullptr) {
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
        if (!parent || !contains(doc_, parent) || !canParent(parent)) {
            parent = doc_;
        }
        selected_ = parent->AppendChild(std::move(node));
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
        if (!ImGui::Begin("Canvas - Visual UI Editor", &visible)) {
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
        ImGui::SetNextItemWidth(230);
        float canvasWidth = float(width_), canvasHeight = float(height_);
        if (number("Canvas Width", canvasWidth, 160, 4096)) {
            width_ = int(std::round(canvasWidth));
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(230);
        if (number("Canvas Height", canvasHeight, 120, 4096)) {
            height_ = int(std::round(canvasHeight));
        }
        ImGui::TextDisabled("Double-click a slider for numeric input. Layout values are pixels.");
        width_ = std::clamp(width_, 160, 4096);
        height_ = std::clamp(height_, 120, 4096);
        if (reload_ && root_ == assets.root) {
            try {
                doc_ = ui.previewLoad(source_,
                                      css_,
                                      assets.resolve(rml_).generic_string(),
                                      assets.resolve(rcss_).generic_string(),
                                      window,
                                      assets.root);
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
            const char* label = title(tag);
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
            applyTreeAction();
        }
        if (inspected_ != selected_) {
            numberEdit_ = 0;
            resize_ = -1;
            moving_ = false;
            inspected_ = selected_;
        }
        if (selected_) {
            ImGui::PushID(selected_);
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
            auto position = localPosition(selected_);
            if (ImGui::InputFloat("X", &position.x, 0, 0, "%.1f")) {
                selected_->SetProperty("position", "absolute");
                pixels(selected_, "left", position.x);
                capture();
            }
            if (ImGui::InputFloat("Y", &position.y, 0, 0, "%.1f")) {
                selected_->SetProperty("position", "absolute");
                pixels(selected_, "top", position.y);
                capture();
            }
            auto size = selected_->GetBox().GetSize(Rml::BoxArea::Content);
            if (number("Width", size.x, 1, 4096)) {
                pixels(selected_, "width", size.x);
                capture();
            }
            if (number("Height", size.y, 1, 4096)) {
                pixels(selected_, "height", size.y);
                capture();
            }
            float fontSize = selected_->GetComputedValues().font_size();
            if (number("Font Size", fontSize, 1, 256)) {
                pixels(selected_, "font-size", fontSize);
                capture();
            }
            for (const auto* prop : {"color", "background-color"}) {
                const auto* property = selected_->GetProperty(prop);
                const auto color =
                    property ? property->Get<Rml::Colourb>() : Rml::Colourb(0, 0, 0, 0);
                float rgba[]{color.red / 255.f,
                             color.green / 255.f,
                             color.blue / 255.f,
                             color.alpha / 255.f};
                if (EditorValues::ColorEdit4(std::strcmp(prop, "color") == 0 ? "Text Color"
                                                                             : "Background Color",
                                             rgba)) {
                    char hex[10];
                    std::snprintf(hex,
                                  sizeof(hex),
                                  "#%02x%02x%02x%02x",
                                  int(rgba[0] * 255 + .5f),
                                  int(rgba[1] * 255 + .5f),
                                  int(rgba[2] * 255 + .5f),
                                  int(rgba[3] * 255 + .5f));
                    selected_->SetProperty(prop, hex);
                    capture();
                }
            }
            auto font = selected_->GetAttribute<Rml::String>("data-canvas-font", "");
            if (assetPicker("Font", font, &assets, [](const std::string& path) {
                    auto ext = AssetManager::extension(path);
                    return ext == ".ttf" || ext == ".otf";
                })) {
                try {
                    if (font.empty()) {
                        selected_->RemoveAttribute("data-canvas-font");
                        selected_->RemoveProperty("font-family");
                    } else {
                        auto family = ui.canvasFont(assets.resolve(font));
                        selected_->SetAttribute("data-canvas-font", font);
                        selected_->SetProperty("font-family", family);
                    }
                    capture();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::TextDisabled("No font override inherits the parent's font (default Roboto).");
            if (auto* control = dynamic_cast<Rml::ElementFormControl*>(selected_)) {
                auto value = control->GetValue();
                if (text("Value", value)) {
                    control->SetValue(value);
                    selected_->SetAttribute("value", value);
                    capture();
                }
            } else if (selected_->GetTagName() != "img") {
                bool plain = true;
                std::string value;
                for (int i = 0; i < selected_->GetNumChildren(); ++i) {
                    auto* child = dynamic_cast<Rml::ElementText*>(selected_->GetChild(i));
                    if (!child) {
                        plain = false;
                        break;
                    }
                    value += child->GetText();
                }
                if (plain && text("Text", value, true)) {
                    selected_->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
                    capture();
                }
                if (!plain) {
                    ImGui::TextWrapped(
                        "Select a text child to edit its text, or edit Content RML below.");
                }
            }
            if (ImGui::CollapsingHeader("Advanced Properties / Content RML")) {
                auto content = selected_->GetInnerRML();
                if (text("Content RML", content, true)) {
                    selected_->SetInnerRML(content);
                    capture();
                }
                for (const auto* attribute : {"name", "title", "value", "placeholder"}) {
                    auto value = selected_->GetAttribute<Rml::String>(attribute, "");
                    const auto label = std::string("Attribute ") + attribute;
                    if (text(label.c_str(), value)) {
                        if (value.empty()) {
                            selected_->RemoveAttribute(attribute);
                        } else {
                            selected_->SetAttribute(attribute, value);
                        }
                        capture();
                    }
                }
                for (const auto* prop : {"display",
                                         "position",
                                         "margin",
                                         "padding",
                                         "border-width",
                                         "border-color",
                                         "border-radius",
                                         "text-align",
                                         "font-weight",
                                         "font-style",
                                         "opacity"}) {
                    const auto* property = selected_->GetLocalProperty(prop);
                    std::string value = property ? property->ToString() : "";
                    if (text(prop, value)) {
                        if (value.empty()) {
                            selected_->RemoveProperty(prop);
                        } else {
                            selected_->SetProperty(prop, value);
                        }
                        capture();
                    }
                }
            }
            if (selected_ != doc_ && ImGui::Button("Delete element")) {
                auto* parent = selected_->GetParentNode();
                parent->RemoveChild(selected_);
                selected_ = nullptr;
                capture();
            }
            ImGui::PopID();
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
            const bool imageHovered = ImGui::IsItemHovered();
            static const ImVec2 directions[] = {
                {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};
            int hoveredHandle = -1;
            if (selected_ && selected_ != doc_ && ImGui::IsWindowHovered()) {
                const auto pos = selected_->GetAbsoluteOffset(Rml::BoxArea::Border),
                           box = selected_->GetBox().GetSize(Rml::BoxArea::Border);
                for (int i = 0; i < 8; ++i) {
                    const ImVec2 handle{
                        origin.x + (pos.x + box.x * (directions[i].x + 1) * .5f) * scale,
                        origin.y + (pos.y + box.y * (directions[i].y + 1) * .5f) * scale};
                    if (std::abs(mouse.x - handle.x) <= 7 && std::abs(mouse.y - handle.y) <= 7) {
                        hoveredHandle = i;
                    }
                }
            }
            if (hoveredHandle >= 0 || resize_ >= 0) {
                const int index = resize_ >= 0 ? resize_ : hoveredHandle;
                const auto d = directions[index];
                ImGui::SetMouseCursor(d.x == 0     ? ImGuiMouseCursor_ResizeNS
                                      : d.y == 0   ? ImGuiMouseCursor_ResizeEW
                                      : d.x == d.y ? ImGuiMouseCursor_ResizeNWSE
                                                   : ImGuiMouseCursor_ResizeNESW);
            }
            if (ImGui::IsMouseClicked(0) && (imageHovered || hoveredHandle >= 0)) {
                resize_ = hoveredHandle;
                moving_ = false;
                if (resize_ < 0) {
                    selected_ = hit(doc_, x, y);
                    moving_ = selected_ && selected_ != doc_;
                }
                inspected_ = selected_;
                numberEdit_ = 0;
                if (selected_ && selected_ != doc_) {
                    dragMouse_ = mouse;
                    dragPosition_ = localPosition(selected_);
                    const auto size = selected_->GetBox().GetSize(Rml::BoxArea::Content);
                    dragSize_ = {size.x, size.y};
                }
            }
            if (selected_ && selected_ != doc_ && ImGui::IsMouseDown(0) &&
                (moving_ || resize_ >= 0)) {
                const float dx = (mouse.x - dragMouse_.x) / scale,
                            dy = (mouse.y - dragMouse_.y) / scale;
                if (resize_ >= 0) {
                    const auto d = directions[resize_];
                    const float w = std::clamp(dragSize_.x + d.x * dx, 1.f, 4096.f),
                                h = std::clamp(dragSize_.y + d.y * dy, 1.f, 4096.f);
                    selected_->SetProperty("position", "absolute");
                    if (d.x) {
                        pixels(selected_, "width", w);
                        pixels(
                            selected_, "left", dragPosition_.x + (d.x < 0 ? dragSize_.x - w : 0));
                    }
                    if (d.y) {
                        pixels(selected_, "height", h);
                        pixels(selected_, "top", dragPosition_.y + (d.y < 0 ? dragSize_.y - h : 0));
                    }
                    if (dx || dy) {
                        capture();
                    }
                } else if (ImGui::IsMouseDragging(0)) {
                    selected_->SetProperty("position", "absolute");
                    pixels(selected_, "left", dragPosition_.x + dx);
                    pixels(selected_, "top", dragPosition_.y + dy);
                    capture();
                }
            }
            if (ImGui::IsMouseReleased(0)) {
                resize_ = -1;
                moving_ = false;
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
                if (selected_ != doc_) {
                    for (int i = 0; i < 8; ++i) {
                        const ImVec2 handle{
                            origin.x + (p.x + s.x * (directions[i].x + 1) * .5f) * scale,
                            origin.y + (p.y + s.y * (directions[i].y + 1) * .5f) * scale};
                        auto* draw = ImGui::GetWindowDrawList();
                        draw->AddRectFilled({handle.x - 4, handle.y - 4},
                                            {handle.x + 4, handle.y + 4},
                                            i == hoveredHandle || i == resize_
                                                ? IM_COL32(255, 195, 65, 255)
                                                : IM_COL32(245, 245, 250, 255));
                        draw->AddRect({handle.x - 4, handle.y - 4},
                                      {handle.x + 4, handle.y + 4},
                                      IM_COL32(30, 35, 45, 255));
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
        ImGui::End();
    }
};
