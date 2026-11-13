#pragma once

#include "assets/assets.hpp"
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "core/hierarchy.hpp"
#include "editor/create_menu.hpp"
#include "editor/editor_layout.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <imgui.h>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

struct LevelObject {
    Entity entity;
    std::string name;
    bool hasCollider;
};

inline std::vector<LevelObject> levelObjects(World& world) {
    std::vector<LevelObject> objects;
    world.each<Transform>([&](Entity e, Transform&) {
        const auto* name = world.tryGet<ObjectName>(e);
        objects.push_back(
            {e, name ? name->value : "Object " + std::to_string(e.index), world.has<Collider>(e)});
    });
    std::sort(objects.begin(), objects.end(), [](const LevelObject& a, const LevelObject& b) {
        return a.entity.index < b.entity.index;
    });
    return objects;
}

inline std::string copiedObjectName(World& world, const std::string& original) {
    std::string base = original;
    std::uint64_t next = 2;
    const auto start = original.rfind(" (");
    if (start != std::string::npos && original.back() == ')') {
        std::uint64_t number = 0;
        const char* first = original.data() + start + 2;
        const char* last = original.data() + original.size() - 1;
        const auto parsed = std::from_chars(first, last, number);
        if (parsed.ec == std::errc{} && parsed.ptr == last && number >= 1 &&
            number < std::numeric_limits<std::uint64_t>::max()) {
            base = original.substr(0, start);
            next = number + 1;
        }
    }
    std::unordered_set<std::string> used;
    world.each<ObjectName>([&](Entity, ObjectName& name) { used.insert(name.value); });
    for (;;) {
        std::string candidate = base + " (" + std::to_string(next) + ")";
        if (!used.count(candidate)) {
            return candidate;
        }
        if (next == std::numeric_limits<std::uint64_t>::max()) {
            base = original;
            next = 2;
        } else {
            ++next;
        }
    }
}

// Copy values before adding components: pool growth can invalidate references.
inline Entity
copyLevelObject(World& sourceWorld, Entity source, World& world, bool preserveParent = false) {
    if (!sourceWorld.alive(source)) {
        return {};
    }
    const Entity copy = world.create();
    auto component = [&](auto value) {
        using T = decltype(value);
        if (auto* original = sourceWorld.tryGet<T>(source)) {
            T data = *original;
            world.add(copy, data);
        }
    };
    component(Transform{});
    component(MeshRenderer{});
    component(Collider{});
    component(Skybox{});
    component(ObjectActivity{});
    component(PrefabInstance{});
    component(DirectionalLight{});
    component(PointLight{});
    component(SpotLight{});
    component(AudioListener{});
    component(AudioSource{});
    component(RigidBody{});
    component(ObjectName{});
    component(LuaScript{});
    component(Camera{});
    component(CharacterController{});
    component(FPSCamera{});
    component(Raycast{});
    component(ObjectLayer{});
    component(InspectorOrder{});
    if (auto* scripts = world.tryGet<LuaScript>(copy)) {
        auto remap = [&](std::vector<ScriptField>& fields) {
            for (auto& field : fields) {
                if (field.entity == source) {
                    field.entity = copy;
                }
            }
        };
        remap(scripts->fields);
        for (auto& extra : scripts->additional) {
            remap(extra.fields);
        }
    }
    if (auto* audio = world.tryGet<AudioSource>(copy)) {
        audio->stop();
    }
    if (auto* camera = world.tryGet<Camera>(copy)) {
        camera->active = false;
    }
    if (preserveParent) {
        component(Parent{});
    }
    if (auto* name = world.tryGet<ObjectName>(copy)) {
        name->value = copiedObjectName(world, name->value);
    }
    return copy;
}
inline Entity duplicateLevelObject(World& world, Entity source) {
    return copyLevelObject(world, source, world, true);
}
// Clipboard owns a detached copy, not a live reference to the source entity.
class ObjectClipboard {
  public:
    bool available() const {
        return data_ != nullptr;
    }
    void copy(World& world, Entity source) {
        if (!world.alive(source)) {
            return;
        }
        data_ = std::make_unique<World>();
        entity_ = copyLevelObject(world, source, *data_);
        // Copy suffix is applied when pasting, not while storing the clipboard.
        if (auto* name = world.tryGet<ObjectName>(source)) {
            data_->add(entity_, *name);
        }
    }
    Entity paste(World& world) {
        return data_ ? copyLevelObject(*data_, entity_, world) : Entity{};
    }

  private:
    std::unique_ptr<World> data_;
    Entity entity_;
};
inline void deleteLevelObject(World& world, Entity object) {
    destroyHierarchy(world, object);
}

// Primary selection drives the Inspector; the remaining rows are a batch selection.
inline std::vector<Entity> hierarchySelection;
inline Entity hierarchyPrimary;
inline void syncHierarchySelection(World& world, Entity primary) {
    hierarchySelection.erase(std::remove_if(hierarchySelection.begin(),
                                            hierarchySelection.end(),
                                            [&](Entity e) { return !world.alive(e); }),
                             hierarchySelection.end());
    if (primary != hierarchyPrimary) {
        hierarchySelection.clear();
        if (world.alive(primary)) {
            hierarchySelection.push_back(primary);
        }
    }
    hierarchyPrimary = primary;
}
inline std::vector<Entity> hierarchyBatch(World& world, Entity fallback) {
    if (std::find(hierarchySelection.begin(), hierarchySelection.end(), fallback) ==
        hierarchySelection.end()) {
        return {fallback};
    }
    std::vector<Entity> roots;
    for (auto e : hierarchySelection) {
        bool nested = false;
        for (auto parent = getParent(world, e); parent != Entity{};
             parent = getParent(world, parent)) {
            if (std::find(hierarchySelection.begin(), hierarchySelection.end(), parent) !=
                hierarchySelection.end()) {
                nested = true;
                break;
            }
        }
        if (!nested && world.alive(e)) {
            roots.push_back(e);
        }
    }
    return roots;
}
inline void deleteHierarchySelection(World& world, Entity primary) {
    for (auto e : hierarchyBatch(world, primary)) {
        deleteLevelObject(world, e);
    }
    hierarchySelection.clear();
    hierarchyPrimary = {};
}

// Search includes ancestors but never mutates the ECS hierarchy.
inline std::unordered_set<std::uint64_t>
hierarchyMatches(World& world, const std::vector<LevelObject>& objects, std::string query) {
    auto lower = [](std::string text) {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    };
    query = lower(query);
    std::unordered_set<std::uint64_t> visible;
    for (const auto& object : objects) {
        if (query.empty() || lower(object.name).find(query) != std::string::npos) {
            Entity e = object.entity;
            for (std::size_t depth = 0; depth <= objects.size() && world.alive(e); ++depth) {
                const auto key = (std::uint64_t(e.generation) << 32) | e.index;
                if (!visible.insert(key).second) {
                    break;
                }
                e = getParent(world, e);
            }
        }
    }
    return visible;
}
inline std::uint64_t hierarchyKey(Entity e) {
    return (std::uint64_t(e.generation) << 32) | e.index;
}
inline void hierarchyIcon(World& world, Entity e, ImVec2 p) {
    auto* draw = ImGui::GetWindowDrawList();
    const auto color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    auto at = [&](float x, float y) { return ImVec2(p.x + x, p.y + y); };
    if (world.has<Camera>(e)) {
        draw->AddRect(at(1, 4), at(10, 12), color, 1);
        draw->AddTriangle(at(10, 6), at(15, 3), at(15, 13), color);
    } else if (world.has<DirectionalLight>(e) || world.has<PointLight>(e) ||
               world.has<SpotLight>(e)) {
        draw->AddCircle(at(8, 8), 3, color, 12);
        for (int i = 0; i < 8; ++i) {
            float a = i * kPi / 4;
            draw->AddLine(at(8 + 5 * std::cos(a), 8 + 5 * std::sin(a)),
                          at(8 + 8 * std::cos(a), 8 + 8 * std::sin(a)),
                          color);
        }
    } else if (world.has<AudioSource>(e)) {
        draw->AddRect(at(1, 6), at(5, 11), color);
        draw->AddTriangle(at(5, 6), at(10, 2), at(10, 15), color);
        draw->AddLine(at(13, 4), at(15, 8), color);
        draw->AddLine(at(15, 8), at(13, 13), color);
    } else if (world.has<MeshRenderer>(e)) {
        draw->AddQuad(at(1, 4), at(8, 1), at(15, 4), at(8, 8), color);
        draw->AddLine(at(1, 4), at(1, 12), color);
        draw->AddLine(at(15, 4), at(15, 12), color);
        draw->AddLine(at(1, 12), at(8, 16), color);
        draw->AddLine(at(15, 12), at(8, 16), color);
        draw->AddLine(at(8, 8), at(8, 16), color);
    } else {
        draw->AddRect(at(2, 2), at(14, 14), color, 2);
        draw->AddLine(at(5, 8), at(11, 8), color);
        draw->AddLine(at(8, 5), at(8, 11), color);
    }
}

// Returns true when selection changes. The caller can end any active gizmo gesture.
inline bool drawLevelObjects(World& world,
                             Entity& selected,
                             bool selectionEnabled,
                             const EditorRect* panel = nullptr,
                             bool* focused = nullptr,
                             int* createRequest = nullptr,
                             const char* levelName = "Untitled",
                             bool* renameLevel = nullptr) {
    if (focused) {
        *focused = false;
    }
    if (dockedEditor && !panelOpen[0]) {
        return false;
    }
    bool changed = false;
    if (!world.alive(selected) && selected != Entity{}) {
        selected = {};
        changed = true;
    }
    if (panel) {
        placeEditorPanel(*panel);
    } else {
        ImGui::SetNextWindowPos(ImVec2(16, 360), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(250, 240), ImGuiCond_FirstUseEver);
    }
    if (beginEditorPanel(panel ? "Hierarchy" : "Level Objects", panel ? fixedPanelFlags : 0)) {
        if (focused) {
            *focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        }
        static Entity renameTarget;
        static char renameName[256]{};
        static bool renameFocus = false;
        bool renameRequested = false;
        const auto objects = levelObjects(world);
        syncHierarchySelection(world, selected);
        static std::unordered_set<std::uint64_t> collapsed;
        static std::vector<Entity> previousRows;
        std::vector<Entity> drawnRows;
        auto reparentDrop = [&](Entity child, Entity parent) {
            const auto batch = hierarchyBatch(world, child);
            for (auto e : batch) {
                if (!canParent(world, e, parent)) {
                    return;
                }
            }
            for (auto e : batch) {
                setParent(world, e, parent);
            }
            changed = true;
        };
        static ImGuiTextFilter search;
        search.Draw("Search", -60);
        ImGui::SameLine();
        if (ImGui::SmallButton("x##ClearSearch")) {
            search.Clear();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear search");
        }
        const auto visible = hierarchyMatches(world, objects, search.InputBuf);

        if (!selectionEnabled) {
            ImGui::TextDisabled("Selection is currently disabled");
        }
        ImGui::BeginDisabled(!selectionEnabled);
        if (!panel && world.alive(selected)) {
            const Entity parent = getParent(world, selected);
            const auto* label = world.tryGet<ObjectName>(parent);
            if (ImGui::BeginCombo("Parent",
                                  parent == Entity{} ? "None"
                                  : label            ? label->value.c_str()
                                                     : "Object")) {
                if (ImGui::Selectable("None", parent == Entity{})) {
                    setParent(world, selected, {});
                }
                for (const auto& object : objects) {
                    if (!canParent(world, selected, object.entity)) {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(object.entity.index));
                    if (ImGui::Selectable(object.name.c_str(), parent == object.entity)) {
                        setParent(world, selected, object.entity);
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();
        ImGui::BeginChild("Objects", ImVec2(0, 0));
        auto row = [&](auto&& self, const LevelObject& object, int depth) -> void {
            if (!world.alive(object.entity) || !visible.count(hierarchyKey(object.entity))) {
                return;
            }
            ImGui::PushID(static_cast<int>(object.entity.index));
            ImGui::PushID(static_cast<int>(object.entity.generation));
            if (depth) {
                ImGui::Indent(static_cast<float>(depth) * 12);
            }
            bool prefab = false;
            for (Entity ancestor = object.entity; world.alive(ancestor);
                 ancestor = getParent(world, ancestor)) {
                if (world.has<PrefabInstance>(ancestor)) {
                    prefab = true;
                    break;
                }
            }
            const bool inactive = !isActive(world, object.entity);
            if (inactive) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            } else if (prefab) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.35f, .65f, 1, 1));
            }
            const bool hasChildren =
                std::any_of(objects.begin(), objects.end(), [&](const LevelObject& other) {
                    return getParent(world, other.entity) == object.entity;
                });
            const auto key = hierarchyKey(object.entity);
            if (hasChildren) {
                // Let the Hierarchy background show through in every button state.
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
                if (ImGui::ArrowButton("##Fold",
                                       collapsed.count(key) ? ImGuiDir_Right : ImGuiDir_Down)) {
                    if (!collapsed.erase(key)) {
                        collapsed.insert(key);
                    }
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine(0, 2);
            } else {
                ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), 1));
                ImGui::SameLine(0, 2);
            }
            const auto iconPos = ImGui::GetCursorScreenPos();
            drawnRows.push_back(object.entity);
            // Reserve pixels rather than spaces (font-dependent widths can overlap the icon).
            if (ImGui::Selectable(("##object" + std::to_string(object.entity.index)).c_str(),
                                  std::find(hierarchySelection.begin(),
                                            hierarchySelection.end(),
                                            object.entity) != hierarchySelection.end(),
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0, std::max(20.0f, ImGui::GetTextLineHeight() + 4)))) {
                if (ImGui::GetIO().KeyShift && world.alive(selected)) {
                    auto first = std::find(previousRows.begin(), previousRows.end(), selected),
                         last = std::find(previousRows.begin(), previousRows.end(), object.entity);
                    if (first != previousRows.end() && last != previousRows.end()) {
                        if (first > last) {
                            std::swap(first, last);
                        }
                        hierarchySelection.assign(first, last + 1);
                    }
                } else if (ImGui::GetIO().KeyCtrl) {
                    auto found = std::find(
                        hierarchySelection.begin(), hierarchySelection.end(), object.entity);
                    if (found != hierarchySelection.end()) {
                        hierarchySelection.erase(found);
                    } else {
                        hierarchySelection.push_back(object.entity);
                    }
                } else {
                    hierarchySelection = {object.entity};
                }
                selected = hierarchySelection.empty() ? Entity{} : hierarchySelection.back();
                hierarchyPrimary = selected;
                changed = true;
            }
            if (selectionEnabled && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                renameTarget = object.entity;
                renameRequested = true;
            }
            if (ImGui::BeginPopupContextItem("Object actions")) {
                if (selected != object.entity) {
                    if (std::find(hierarchySelection.begin(),
                                  hierarchySelection.end(),
                                  object.entity) == hierarchySelection.end()) {
                        hierarchySelection = {object.entity};
                    }
                    selected = object.entity;
                    hierarchyPrimary = selected;
                    changed = true;
                }
                ImGui::TextUnformatted(object.name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Rename...", "F2")) {
                    renameTarget = object.entity;
                    renameRequested = true;
                }
                if (ImGui::MenuItem("Duplicate")) {
                    selected = duplicateLevelObject(world, object.entity);
                    changed = true;
                }
                if (ImGui::MenuItem(
                        "Unparent", nullptr, false, getParent(world, object.entity) != Entity{})) {
                    for (auto e : hierarchyBatch(world, object.entity)) {
                        setParent(world, e, {});
                    }
                    changed = true;
                }
                if (ImGui::MenuItem("Delete")) {
                    deleteHierarchySelection(world, object.entity);
                    selected = {};
                    changed = true;
                }
                ImGui::EndPopup();
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("LEVEL_OBJECT", &object.entity, sizeof(Entity));
                ImGui::TextUnformatted(object.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* asset = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    if (attachScriptAsset(
                            world, object.entity, static_cast<const char*>(asset->Data))) {
                        changed = true;
                    }
                }
                if (const auto* payload = ImGui::AcceptDragDropPayload("LEVEL_OBJECT")) {
                    const Entity child = *static_cast<const Entity*>(payload->Data);
                    reparentDrop(child, object.entity);
                }
                ImGui::EndDragDropTarget();
            }
            hierarchyIcon(world, object.entity, ImVec2(iconPos.x, iconPos.y + 2));
            ImGui::GetWindowDrawList()->AddText(ImVec2(iconPos.x + 24, iconPos.y + 2),
                                                ImGui::GetColorU32(ImGuiCol_Text),
                                                object.name.c_str());
            if (depth) {
                ImGui::Unindent(static_cast<float>(depth) * 12);
            }
            if (prefab || inactive) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
            ImGui::PopID();
            if (depth < 128 && (!collapsed.count(key) || search.InputBuf[0])) {
                for (const auto& child : objects) {
                    if (getParent(world, child.entity) == object.entity) {
                        self(self, child, depth + 1);
                    }
                }
            }
        };
        // Level root is not an entity; dropping here unparents selected roots.
        ImGui::SetNextItemOpen(true, search.InputBuf[0] ? ImGuiCond_Always : ImGuiCond_Once);
        const auto rootPosition = ImGui::GetCursorScreenPos();
        const bool rootOpen = ImGui::TreeNodeEx(
            "##LevelRoot", ImGuiTreeNodeFlags_SpanAvailWidth, "    %s", levelName);
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("LEVEL_OBJECT")) {
                reparentDrop(*static_cast<const Entity*>(payload->Data), {});
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("Level root")) {
            if (ImGui::MenuItem("Rename Level...", nullptr, false, renameLevel != nullptr) &&
                renameLevel) {
                *renameLevel = true;
            }
            ImGui::EndPopup();
        }
        auto* rootDraw = ImGui::GetWindowDrawList();
        rootDraw->AddQuad(ImVec2(rootPosition.x + 22, rootPosition.y + 3),
                          ImVec2(rootPosition.x + 28, rootPosition.y),
                          ImVec2(rootPosition.x + 34, rootPosition.y + 3),
                          ImVec2(rootPosition.x + 28, rootPosition.y + 7),
                          ImGui::GetColorU32(ImGuiCol_Text));
        rootDraw->AddLine(ImVec2(rootPosition.x + 22, rootPosition.y + 7),
                          ImVec2(rootPosition.x + 28, rootPosition.y + 11),
                          ImGui::GetColorU32(ImGuiCol_Text));
        rootDraw->AddLine(ImVec2(rootPosition.x + 28, rootPosition.y + 11),
                          ImVec2(rootPosition.x + 34, rootPosition.y + 7),
                          ImGui::GetColorU32(ImGuiCol_Text));
        if (rootOpen) {
            ImGui::Indent(8);
            for (const auto& object : objects) {
                if (getParent(world, object.entity) == Entity{}) {
                    row(row, object, 0);
                }
            }
            if (visible.empty() && search.InputBuf[0]) {
                ImGui::TextDisabled("No matching objects");
            }
            ImGui::Unindent(8);
            ImGui::TreePop();
        }
        previousRows = drawnRows;
        ImGui::InvisibleButton("##HierarchyBackground",
                               ImVec2(std::max(1.f, ImGui::GetContentRegionAvail().x),
                                      std::max(32.f, ImGui::GetContentRegionAvail().y)));
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("LEVEL_OBJECT")) {
                reparentDrop(*static_cast<const Entity*>(payload->Data), {});
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            changed = selected != Entity{};
            selected = {};
            hierarchyPrimary = {};
            hierarchySelection.clear();
        }
        if (createRequest && ImGui::BeginPopupContextItem("Hierarchy empty actions")) {
            if (ImGui::BeginMenu("Create")) {
                int item = drawCreateItems();
                if (item >= 0) {
                    *createRequest = item;
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        if (selectionEnabled && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && world.alive(selected) &&
            ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            renameTarget = selected;
            renameRequested = true;
        }
        if (renameRequested && world.alive(renameTarget)) {
            const auto* name = world.tryGet<ObjectName>(renameTarget);
            std::snprintf(
                renameName, sizeof(renameName), "%s", name ? name->value.c_str() : "Object");
            ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
            ImGui::OpenPopup("Rename GameObject");
            renameFocus = true;
        }
        if (ImGui::BeginPopup("Rename GameObject", ImGuiWindowFlags_AlwaysAutoResize)) {
            if (renameFocus) {
                ImGui::SetKeyboardFocusHere();
                renameFocus = false;
            }
            ImGui::SetNextItemWidth(220);
            const bool enter = ImGui::InputText("##Name",
                                                renameName,
                                                sizeof(renameName),
                                                ImGuiInputTextFlags_EnterReturnsTrue |
                                                    ImGuiInputTextFlags_AutoSelectAll);
            if (enter || ImGui::Button("Rename")) {
                if (world.alive(renameTarget) &&
                    std::string(renameName).find_first_not_of(" \t\r\n") != std::string::npos) {
                    world.add(renameTarget, ObjectName{renameName});
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered()) {
            changed = selected != Entity{};
            selected = {};
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
    }
    ImGui::End();
    return changed;
}
