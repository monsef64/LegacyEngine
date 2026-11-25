#pragma once
#include "animation/animator_controller.hpp"
#include "editor/asset_picker.hpp"
#include <algorithm>
#include <cstdio>
#include <imgui.h>

class AnimatorWindow {
    AnimatorController graph_;
    std::filesystem::path root_;
    std::string path_ = "Animations/NewAnimator.controller", error_, linking_;
    Entity owner_;
    bool dirty_ = false, assign_ = false;
    int state_ = -1, transition_ = -1;
    ImVec2 pan_{220, 80}, contextPosition_{};
    static bool text(const char* label, std::string& value, std::size_t capacity = 129) {
        std::vector<char> buffer(capacity);
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
        if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
            return false;
        }
        value = buffer.data();
        return true;
    }
    static bool number(const char* label, AnimatorValue& value) {
        if (value.type == AnimatorParameterType::Bool) {
            bool v = value.number != 0;
            if (ImGui::Checkbox(label, &v)) {
                value.number = v;
                return true;
            }
        } else if (value.type == AnimatorParameterType::Int) {
            int v = int(value.number);
            if (ImGui::InputInt(label, &v)) {
                value.number = std::clamp(v, -1000000, 1000000);
                return true;
            }
        } else {
            float v = float(value.number);
            if (ImGui::InputFloat(label, &v, .1f, 1.f)) {
                value.number = std::isfinite(v) ? std::clamp(v, -1000000.f, 1000000.f) : 0;
                return true;
            }
        }
        return false;
    }
    void addState(const std::string& clip, ImVec2 position) {
        if (graph_.states.size() >= 256) {
            error_ = "Maximum 256 states";
            return;
        }
        unsigned n = 1;
        std::string id;
        do {
            id = "state-" + std::to_string(n++);
        } while (Animators::findState(graph_, id));
        std::string name = clip.empty() ? "State" : std::filesystem::path(clip).stem().string();
        name = name.substr(0, 120);
        const auto base = name;
        n = 1;
        while (Animators::findState(graph_, name)) {
            name = base + " " + std::to_string(n++);
        }
        graph_.states.push_back({id, name, clip, true, position.x, position.y});
        if (graph_.entry.empty()) {
            graph_.entry = id;
        }
        state_ = int(graph_.states.size() - 1);
        transition_ = -1;
        dirty_ = true;
    }
    void addTransition(const std::string& from, const std::string& to) {
        if (graph_.transitions.size() >= 1024) {
            error_ = "Maximum 1024 transitions";
            return;
        }
        graph_.transitions.push_back({from, to, .2f, true, 1, {}});
        transition_ = int(graph_.transitions.size() - 1);
        state_ = -1;
        linking_.clear();
        dirty_ = true;
    }
    void details(AssetManager& assets, Animator* live, bool runtime) {
        ImGui::SeparatorText(runtime ? "Parameters (live instance)" : "Parameters (defaults)");
        int removeParameter = -1;
        for (std::size_t i = 0; i < graph_.parameters.size(); ++i) {
            auto& p = graph_.parameters[i];
            ImGui::PushID(int(i));
            ImGui::BeginDisabled(runtime);
            const auto before = p.name;
            ImGui::SetNextItemWidth(150);
            if (text("##Parameter", p.name)) {
                for (auto& t : graph_.transitions) {
                    for (auto& c : t.conditions) {
                        if (c.parameter == before) {
                            c.parameter = p.name;
                        }
                    }
                }
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                removeParameter = int(i);
            }
            int type = int(p.value.type);
            if (ImGui::Combo("Type", &type, "Bool\0Int\0Float\0")) {
                p.value.type = AnimatorParameterType(type);
                p.value.number = 0;
                for (auto& transition : graph_.transitions) {
                    for (auto& condition : transition.conditions) {
                        if (condition.parameter == p.name) {
                            condition.comparison = "==";
                            condition.value = 0;
                        }
                    }
                }
                dirty_ = true;
            }
            ImGui::EndDisabled();
            if (runtime) {
                if (live) {
                    auto found = live->values.find(p.name);
                    if (found != live->values.end()) {
                        number("Value", found->second);
                    } else {
                        ImGui::TextDisabled("Not initialized");
                    }
                }
            } else {
                dirty_ |= number("Default", p.value);
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeParameter >= 0) {
            const auto name = graph_.parameters[removeParameter].name;
            bool referenced = false;
            for (const auto& t : graph_.transitions) {
                for (const auto& c : t.conditions) {
                    referenced |= c.parameter == name;
                }
            }
            if (referenced) {
                error_ = "Remove this parameter's transition conditions first";
            } else {
                graph_.parameters.erase(graph_.parameters.begin() + removeParameter);
                dirty_ = true;
            }
        }
        ImGui::BeginDisabled(runtime);
        if (graph_.parameters.size() < 64 && ImGui::Button("Add parameter")) {
            unsigned n = 1;
            std::string name;
            do {
                name = "Parameter" + std::to_string(n++);
            } while (std::any_of(graph_.parameters.begin(),
                                 graph_.parameters.end(),
                                 [&](const auto& p) { return p.name == name; }));
            graph_.parameters.push_back({name, {}});
            dirty_ = true;
        }
        if (ImGui::CollapsingHeader("All transitions")) {
            for (std::size_t i = 0; i < graph_.transitions.size(); ++i) {
                const auto& t = graph_.transitions[i];
                const auto* from = Animators::findState(graph_, t.from);
                const auto* to = Animators::findState(graph_, t.to);
                const auto label = std::string(from ? from->name : "Any State") + " -> " +
                                   (to ? to->name : "Missing") + "##" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), transition_ == int(i))) {
                    transition_ = int(i);
                    state_ = -1;
                }
            }
        }
        if (state_ >= 0 && state_ < int(graph_.states.size())) {
            auto& state = graph_.states[state_];
            ImGui::SeparatorText("State");
            dirty_ |= text("Name", state.name);
            dirty_ |= assetPicker("Animation clip", state.clip, &assets, [](const std::string& p) {
                return AssetManager::extension(p) == ".leanim";
            });
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    const std::string path = static_cast<const char*>(p->Data);
                    if (AssetManager::extension(path) == ".leanim") {
                        state.clip = path;
                        dirty_ = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            dirty_ |= ImGui::Checkbox("Loop", &state.loop);
            if (ImGui::Button("Set as entry")) {
                graph_.entry = state.id;
                dirty_ = true;
            }
            if (ImGui::Button("Create transition")) {
                linking_ = state.id;
            }
            if (ImGui::Button("Delete state")) {
                const auto id = state.id;
                graph_.transitions.erase(
                    std::remove_if(graph_.transitions.begin(),
                                   graph_.transitions.end(),
                                   [&](const auto& t) { return t.from == id || t.to == id; }),
                    graph_.transitions.end());
                graph_.states.erase(graph_.states.begin() + state_);
                state_ = transition_ = -1;
                linking_.clear();
                if (graph_.entry == id) {
                    graph_.entry = graph_.states.empty() ? "" : graph_.states.front().id;
                }
                dirty_ = true;
            }
        }
        if (transition_ >= 0 && transition_ < int(graph_.transitions.size())) {
            if (ImGui::SmallButton("Priority up") && transition_ > 0) {
                std::swap(graph_.transitions[transition_], graph_.transitions[transition_ - 1]);
                --transition_;
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Priority down") &&
                transition_ + 1 < int(graph_.transitions.size())) {
                std::swap(graph_.transitions[transition_], graph_.transitions[transition_ + 1]);
                ++transition_;
                dirty_ = true;
            }
            auto& t = graph_.transitions[transition_];
            ImGui::SeparatorText("Transition");
            const auto* from = Animators::findState(graph_, t.from);
            const auto* to = Animators::findState(graph_, t.to);
            ImGui::TextWrapped("%s -> %s",
                               from ? from->name.c_str() : "Any State",
                               to ? to->name.c_str() : "Missing");
            dirty_ |= ImGui::DragFloat(
                "Fade seconds", &t.fade, .01f, 0, 3600, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            dirty_ |= ImGui::Checkbox("Has exit time", &t.hasExitTime);
            if (t.hasExitTime) {
                dirty_ |= ImGui::SliderFloat("Normalized exit time", &t.exitTime, 0, 1);
            }
            ImGui::TextWrapped(
                "All conditions must pass. First matching transition wins; automatic transitions wait for the current fade to finish.");
            int remove = -1;
            for (std::size_t i = 0; i < t.conditions.size(); ++i) {
                auto& condition = t.conditions[i];
                ImGui::PushID(int(i));
                if (ImGui::BeginCombo("Parameter", condition.parameter.c_str())) {
                    for (const auto& p : graph_.parameters) {
                        if (ImGui::Selectable(p.name.c_str(), p.name == condition.parameter)) {
                            condition.parameter = p.name;
                            condition.comparison = "==";
                            condition.value = 0;
                            dirty_ = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                auto parameter =
                    std::find_if(graph_.parameters.begin(),
                                 graph_.parameters.end(),
                                 [&](const auto& p) { return p.name == condition.parameter; });
                if (ImGui::BeginCombo("Compare", condition.comparison.c_str())) {
                    for (const char* op : {"==", "!=", "<", ">", "<=", ">="}) {
                        if (parameter != graph_.parameters.end() &&
                            parameter->value.type == AnimatorParameterType::Bool &&
                            std::string(op) != "==" && std::string(op) != "!=") {
                            continue;
                        }
                        if (ImGui::Selectable(op, condition.comparison == op)) {
                            condition.comparison = op;
                            dirty_ = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                AnimatorValue value{parameter == graph_.parameters.end()
                                        ? AnimatorParameterType::Float
                                        : parameter->value.type,
                                    condition.value};
                if (number("Threshold", value)) {
                    condition.value = value.number;
                    dirty_ = true;
                }
                if (ImGui::SmallButton("Remove condition")) {
                    remove = int(i);
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (remove >= 0) {
                t.conditions.erase(t.conditions.begin() + remove);
                dirty_ = true;
            }
            if (!graph_.parameters.empty() && t.conditions.size() < 32 &&
                ImGui::Button("Add condition")) {
                t.conditions.push_back({graph_.parameters.front().name, "==", 0});
                dirty_ = true;
            }
            if (ImGui::Button("Delete transition")) {
                graph_.transitions.erase(graph_.transitions.begin() + transition_);
                transition_ = -1;
                dirty_ = true;
            }
        }
        ImGui::EndDisabled();
        if (runtime && live) {
            ImGui::SeparatorText("Runtime");
            const auto* state =
                live->definition ? Animators::findState(*live->definition, live->state) : nullptr;
            ImGui::Text("State: %s", state ? state->name.c_str() : "None");
            ImGui::Text("State time: %.3f", live->stateTime);
            if (!live->error.empty()) {
                ImGui::TextWrapped("%s", live->error.c_str());
            }
            if (state_ >= 0 && state_ < int(graph_.states.size()) &&
                ImGui::Button("Play selected state")) {
                live->request(AnimatorAction::Play, graph_.states[state_].id);
            }
        }
    }
    void canvas(AssetManager&, Animator* live, bool runtime) {
        const auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
        const ImVec2 end{origin.x + std::max(size.x, 1.f), origin.y + std::max(size.y, 1.f)};
        auto point = [&](float x, float y) {
            return ImVec2{origin.x + pan_.x + x, origin.y + pan_.y + y};
        };
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, end, IM_COL32(44, 46, 49, 255));
        draw->PushClipRect(origin, end, true);
        for (float x = std::fmod(pan_.x, 24.f); x < size.x; x += 24) {
            draw->AddLine(
                {origin.x + x, origin.y}, {origin.x + x, end.y}, IM_COL32(61, 63, 66, 255));
        }
        for (float y = std::fmod(pan_.y, 24.f); y < size.y; y += 24) {
            draw->AddLine(
                {origin.x, origin.y + y}, {end.x, origin.y + y}, IM_COL32(61, 63, 66, 255));
        }
        auto centre = [&](const std::string& id) {
            const auto* state = Animators::findState(graph_, id);
            return state ? point(state->x + 85, state->y + 30) : point(-110, id == "*" ? 140 : 30);
        };
        auto arrow = [&](ImVec2 a, ImVec2 b, ImU32 color) {
            const float dx = b.x - a.x, dy = b.y - a.y, length = std::sqrt(dx * dx + dy * dy);
            if (length < 1) {
                return;
            }
            const float ux = dx / length, uy = dy / length;
            const ImVec2 tip{a.x + dx * .65f, a.y + dy * .65f};
            draw->AddLine(a, b, color, 2);
            draw->AddTriangleFilled(tip,
                                    {tip.x - ux * 10 - uy * 5, tip.y - uy * 10 + ux * 5},
                                    {tip.x - ux * 10 + uy * 5, tip.y - uy * 10 - ux * 5},
                                    color);
        };
        if (!graph_.entry.empty()) {
            arrow(centre(""), centre(graph_.entry), IM_COL32(225, 180, 55, 255));
        }
        int edgeHit = -1;
        const auto mouse = ImGui::GetIO().MousePos;
        for (std::size_t i = 0; i < graph_.transitions.size(); ++i) {
            const auto& t = graph_.transitions[i];
            const auto a = centre(t.from), b = centre(t.to);
            if (t.from == t.to) {
                draw->AddBezierCubic({a.x - 35, a.y - 25},
                                     {a.x - 65, a.y - 100},
                                     {a.x + 65, a.y - 100},
                                     {a.x + 35, a.y - 25},
                                     IM_COL32(215, 220, 230, 255),
                                     2);
            }
            arrow(a,
                  b,
                  int(i) == transition_ ? IM_COL32(255, 195, 65, 255)
                                        : IM_COL32(215, 220, 230, 255));
            const float dx = b.x - a.x, dy = b.y - a.y, den = dx * dx + dy * dy;
            const float u =
                den > 0 ? std::clamp(((mouse.x - a.x) * dx + (mouse.y - a.y) * dy) / den, 0.f, 1.f)
                        : 0;
            const float ex = mouse.x - a.x - u * dx, ey = mouse.y - a.y - u * dy;
            if (ex * ex + ey * ey < 36) {
                edgeHit = int(i);
            }
        }
        for (const auto& special :
             {std::pair<const char*, float>{"Entry", 0}, {"Any State", 110}}) {
            const auto low = point(-180, special.second), high = point(-40, special.second + 60);
            draw->AddRectFilled(low,
                                high,
                                special.second == 0 ? IM_COL32(30, 155, 65, 255)
                                                    : IM_COL32(45, 105, 165, 255),
                                6);
            draw->AddText({low.x + 20, low.y + 20}, IM_COL32_WHITE, special.first);
        }
        bool nodeHovered = false;
        for (std::size_t i = 0; i < graph_.states.size(); ++i) {
            auto& s = graph_.states[i];
            const auto low = point(s.x, s.y), high = point(s.x + 170, s.y + 60);
            const bool active = live && live->state == s.id && live->playing;
            draw->AddRectFilled(
                {low.x + 4, low.y + 5}, {high.x + 4, high.y + 5}, IM_COL32(0, 0, 0, 95), 7);
            const ImU32 top = active                 ? IM_COL32(65, 145, 115, 255)
                              : s.id == graph_.entry ? IM_COL32(180, 124, 62, 255)
                                                     : IM_COL32(100, 119, 148, 255);
            const ImU32 bottom = active                 ? IM_COL32(28, 76, 65, 255)
                                 : s.id == graph_.entry ? IM_COL32(101, 65, 33, 255)
                                                        : IM_COL32(48, 60, 80, 255);
            draw->AddRectFilledMultiColor(low, high, top, top, bottom, bottom);
            draw->AddLine(
                {low.x + 2, low.y + 2}, {high.x - 2, low.y + 2}, IM_COL32(255, 255, 255, 65), 1);
            draw->AddLine(
                {low.x + 1, low.y + 28}, {high.x - 1, low.y + 28}, IM_COL32(0, 0, 0, 70), 1);
            draw->AddRect(low,
                          high,
                          int(i) == state_ ? IM_COL32(255, 210, 70, 255)
                                           : IM_COL32(175, 180, 185, 255),
                          6,
                          0,
                          2);
            draw->PushClipRect(low, high, true);
            draw->AddText({low.x + 9, low.y + 9}, IM_COL32_WHITE, s.name.c_str());
            draw->AddText(
                {low.x + 9, low.y + 32}, IM_COL32(220, 220, 225, 255), s.loop ? "Loop" : "Once");
            draw->PopClipRect();
            ImGui::SetCursorScreenPos(low);
            ImGui::PushID(int(i));
            ImGui::InvisibleButton("State node", {170, 60});
            nodeHovered |= ImGui::IsItemHovered();
            if (ImGui::IsItemClicked(0)) {
                if (!runtime && !linking_.empty()) {
                    addTransition(linking_, s.id);
                } else {
                    state_ = int(i);
                    transition_ = -1;
                }
            }
            if (!runtime && ImGui::IsItemActive() && ImGui::IsMouseDragging(0) &&
                linking_.empty()) {
                s.x = std::clamp(s.x + ImGui::GetIO().MouseDelta.x, -100000.f, 100000.f);
                s.y = std::clamp(s.y + ImGui::GetIO().MouseDelta.y, -100000.f, 100000.f);
                dirty_ = true;
            }
            if (ImGui::BeginPopupContextItem("State menu")) {
                if (!runtime && ImGui::MenuItem("Create transition")) {
                    linking_ = s.id;
                }
                if (!runtime && ImGui::MenuItem("Set as entry")) {
                    graph_.entry = s.id;
                    dirty_ = true;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!linking_.empty()) {
            arrow(centre(linking_), mouse, IM_COL32(255, 210, 70, 255));
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("Graph background",
                               {std::max(size.x, 1.f), std::max(size.y, 1.f)},
                               ImGuiButtonFlags_MouseButtonMiddle |
                                   ImGuiButtonFlags_MouseButtonRight);
        const bool background = ImGui::IsItemHovered() && !nodeHovered;
        if (background && ImGui::IsMouseDragging(2)) {
            pan_.x += ImGui::GetIO().MouseDelta.x;
            pan_.y += ImGui::GetIO().MouseDelta.y;
        }
        if (background && ImGui::IsMouseClicked(0)) {
            transition_ = edgeHit;
            state_ = -1;
            if (edgeHit < 0) {
                linking_.clear();
            }
        }
        if (background && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            contextPosition_ = {mouse.x - origin.x - pan_.x, mouse.y - origin.y - pan_.y};
            ImGui::OpenPopup("Graph actions");
        }
        if (ImGui::BeginPopup("Graph actions")) {
            ImGui::BeginDisabled(runtime);
            if (ImGui::MenuItem("Add animation state")) {
                addState("", contextPosition_);
            }
            if (ImGui::MenuItem("Link from Any State")) {
                linking_ = "*";
            }
            ImGui::EndDisabled();
            if (ImGui::MenuItem("Unselect")) {
                state_ = transition_ = -1;
                linking_.clear();
            }
            if (ImGui::MenuItem("Reset graph view")) {
                pan_ = {220, 80};
            }
            if (ImGui::MenuItem("Cancel transition", nullptr, false, !linking_.empty())) {
                linking_.clear();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (!runtime) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    const std::string path = static_cast<const char*>(payload->Data);
                    if (AssetManager::extension(path) == ".leanim") {
                        addState(path, {mouse.x - origin.x - pan_.x, mouse.y - origin.y - pan_.y});
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        draw->PopClipRect();
    }

  public:
    bool visible = false;
    bool hasUnsavedChanges() const {
        return dirty_;
    }
    const std::string& error() const {
        return error_;
    }
    bool saveIfDirty(AssetManager& assets, World* world = nullptr) {
        if (!dirty_) {
            return true;
        }
        try {
            if (root_ != assets.root) {
                throw std::runtime_error(
                    "Save/discard the Animator controller before changing projects");
            }
            if (!Animators::controllerAsset(path_)) {
                throw std::runtime_error(
                    "Controller path must be project-relative and end in .controller");
            }
            const auto doc = Animators::document(graph_);
            (void)Animators::parseController(doc);
            writeJsonFile(assets.resolve(path_), doc);
            dirty_ = false;
            error_.clear();
            if (assign_ && world && world->alive(owner_)) {
                if (auto* animator = world->tryGet<Animator>(owner_)) {
                    animator->controller = path_;
                    assign_ = false;
                }
            }
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    void remapAssetPaths(const std::string& old, const std::string& next) {
        auto remap = [&](std::string& p) {
            if (p == old || (p.size() > old.size() && p.compare(0, old.size(), old) == 0 &&
                             (p[old.size()] == '/' || p[old.size()] == '#'))) {
                p = next + p.substr(old.size());
                dirty_ = true;
            }
        };
        remap(path_);
        for (auto& state : graph_.states) {
            remap(state.clip);
        }
    }
    void open(AssetManager& assets, const std::string& path, Entity owner = {}) {
        visible = true;
        if (dirty_) {
            if (root_ == assets.root && path == path_) {
                owner_ = owner;
                return;
            }
            error_ = "Save or discard the current controller before opening another";
            return;
        }
        try {
            if (path.empty()) {
                graph_ = {};
                path_ = "Animations/NewAnimator.controller";
                dirty_ = true;
                assign_ = true;
            } else {
                if (!Animators::controllerAsset(path)) {
                    throw std::runtime_error("Expected a .controller asset");
                }
                graph_ =
                    Animators::parseController(readJsonFile(assets.resolve(path), 1024 * 1024));
                path_ = path;
                dirty_ = false;
                assign_ = false;
            }
            root_ = assets.root;
            owner_ = owner;
            state_ = transition_ = -1;
            pan_ = {220, 80};
            error_.clear();
            linking_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void draw(World& world, Entity selected, AssetManager& assets, bool runtime) {
        if (!visible) {
            return;
        }
        ImGui::SetNextWindowSize({1100, 650}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Animator Controller", &visible, ImGuiWindowFlags_MenuBar)) {
            ImGui::End();
            return;
        }
        if (root_.empty()) {
            root_ = assets.root;
        }
        if (root_ != assets.root && ImGui::Button("Discard previous project's controller draft")) {
            dirty_ = false;
            assign_ = false;
            graph_ = {};
            root_ = assets.root;
            path_ = "Animations/NewAnimator.controller";
            state_ = transition_ = -1;
            linking_.clear();
            error_.clear();
        }
        ImGui::BeginDisabled(runtime || root_ != assets.root);
        ImGui::SetNextItemWidth(360);
        const auto previousPath = path_;
        if (text("Controller file", path_, 4097)) {
            dirty_ = true;
            if (const auto* owner = world.tryGet<Animator>(owner_)) {
                assign_ |= owner->controller.empty() || owner->controller == previousPath;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            if (saveIfDirty(assets, &world) && assign_ && world.alive(owner_)) {
                if (auto* a = world.tryGet<Animator>(owner_)) {
                    a->controller = path_;
                    assign_ = false;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload") && !dirty_) {
            open(assets, path_, owner_);
        }
        ImGui::SameLine();
        if (ImGui::Button("New") && !dirty_) {
            open(assets, "", selected);
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard draft")) {
            dirty_ = false;
            error_.clear();
            state_ = transition_ = -1;
            linking_.clear();
            try {
                if (Animators::controllerAsset(path_) &&
                    std::filesystem::exists(assets.resolve(path_))) {
                    open(assets, path_, owner_);
                } else {
                    graph_ = {};
                    path_ = "Animations/NewAnimator.controller";
                    root_ = assets.root;
                    assign_ = false;
                }
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        if (ImGui::Button("Add animation state")) {
            addState("", {220, 100.f * float(graph_.states.size())});
        }
        ImGui::SameLine();
        if (ImGui::Button("Link from Any State")) {
            linking_ = "*";
        }
        ImGui::EndDisabled();
        if (root_ != assets.root) {
            ImGui::TextWrapped(
                "Controller belongs to another project; reopen Animator for the current project.");
        }
        if (!linking_.empty()) {
            ImGui::TextUnformatted("Click a destination state to create a transition.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel link")) {
                linking_.clear();
            }
        } else {
            ImGui::TextDisabled(
                "Drop .leanim clips onto the graph. Right-click a node to link it. Middle-drag to pan. Click arrows to edit fade/conditions.");
        }
        if (dirty_) {
            ImGui::TextUnformatted("Unsaved controller changes");
        }
        if (!error_.empty()) {
            ImGui::TextWrapped("%s", error_.c_str());
        }
        Animator* live = world.tryGet<Animator>(world.alive(owner_) ? owner_ : selected);
        if (live && live->controller != path_) {
            live = nullptr;
        }
        ImGui::BeginChild("Animator properties", {290, 0}, ImGuiChildFlags_Borders);
        details(assets, live, runtime || root_ != assets.root);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Animator graph",
                          {0, 0},
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        canvas(assets, live, runtime || root_ != assets.root);
        ImGui::EndChild();
        ImGui::End();
    }
};
