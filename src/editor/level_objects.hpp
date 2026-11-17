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
#include "ui/mygui_ui.hpp"
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
            if constexpr (std::is_base_of_v<LightSettings, T>) {
                data.bake.clear();
            }
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
    component(Animation{});
    component(ParticleSystem{});
    component(LightShafts{});
    component(CameraEffects{});
    if (const auto* terrain = sourceWorld.tryGet<Terrain>(source)) {
        world.add(copy, *terrain);
    }
    if (const auto* original = sourceWorld.tryGet<Laser>(source)) {
        Laser laser;
        static_cast<LaserSettings&>(laser) = *original;
        world.add(copy, laser);
    }
    component(ObjectLayer{});
    component(InspectorOrder{});
    if (auto* animation = world.tryGet<Animation>(copy)) {
        *animation = Animation{animation->clip, animation->loop, animation->playOnStart};
    }
    if (auto* particles = world.tryGet<ParticleSystem>(copy)) {
        particles->stop();
        particles->started = false;
        particles->time = 0;
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
// Snapshot descendants before creating anything (source and destination may be identical).
inline Entity
copyLevelHierarchy(World& sourceWorld, Entity source, World& world, bool preserveParent = false) {
    if (!sourceWorld.alive(source)) {
        return {};
    }
    std::vector<Entity> originals{source}, copies;
    const auto entities = sourceWorld.entities();
    for (std::size_t i = 0; i < originals.size(); ++i) {
        for (auto e : entities) {
            if (getParent(sourceWorld, e) == originals[i] &&
                std::find(originals.begin(), originals.end(), e) == originals.end()) {
                originals.push_back(e);
            }
        }
    }
    for (auto e : originals) {
        copies.push_back(copyLevelObject(sourceWorld, e, world));
    }
    auto remap = [&](Entity e) {
        auto it = std::find(originals.begin(), originals.end(), e);
        return it == originals.end() ? (&sourceWorld == &world ? e : Entity{})
                                     : copies[std::size_t(it - originals.begin())];
    };
    for (std::size_t i = 0; i < originals.size(); ++i) {
        if (i > 0) {
            if (const auto* name = sourceWorld.tryGet<ObjectName>(originals[i])) {
                const auto value = *name;
                world.add(copies[i], value);
            }
        }
        const auto parent = getParent(sourceWorld, originals[i]);
        if (i || preserveParent) {
            setParent(world, copies[i], remap(parent));
        }
        if (auto* camera = world.tryGet<Camera>(copies[i])) {
            camera->targetEntity = remap(camera->targetEntity);
        }
        if (auto* scripts = world.tryGet<LuaScript>(copies[i])) {
            auto fields = [&](auto& entries) {
                for (auto& f : entries) {
                    f.entity = remap(f.entity);
                }
            };
            fields(scripts->fields);
            for (auto& extra : scripts->additional) {
                fields(extra.fields);
            }
        }
    }
    return copies.front();
}
inline Entity duplicateLevelObject(World& world, Entity source) {
    return copyLevelHierarchy(world, source, world, true);
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
        entity_ = copyLevelHierarchy(world, source, *data_);
        // Copy suffix is applied when pasting, not while storing the clipboard.
        if (auto* name = world.tryGet<ObjectName>(source)) {
            data_->add(entity_, *name);
        }
    }
    Entity paste(World& world) {
        return data_ ? copyLevelHierarchy(*data_, entity_, world) : Entity{};
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
inline void hierarchyIcon(World& world, Entity e, ui::Vec2 p) {
    auto* draw = ui::windowDrawList();
    const auto color = ui::colorU32(ui::Col_TextDisabled);
    auto at = [&](float x, float y) { return ui::Vec2(p.x + x, p.y + y); };
    if (world.has<Camera>(e)) {
        draw->addRect(at(1, 4), at(10, 12), color, 1);
        draw->addTriangle(at(10, 6), at(15, 3), at(15, 13), color);
    } else if (world.has<DirectionalLight>(e) || world.has<PointLight>(e) ||
               world.has<SpotLight>(e)) {
        draw->addCircle(at(8, 8), 3, color, 12);
        for (int i = 0; i < 8; ++i) {
            float a = i * kPi / 4;
            draw->addLine(at(8 + 5 * std::cos(a), 8 + 5 * std::sin(a)),
                          at(8 + 8 * std::cos(a), 8 + 8 * std::sin(a)),
                          color);
        }
    } else if (world.has<AudioSource>(e)) {
        draw->addRect(at(1, 6), at(5, 11), color);
        draw->addTriangle(at(5, 6), at(10, 2), at(10, 15), color);
        draw->addLine(at(13, 4), at(15, 8), color);
        draw->addLine(at(15, 8), at(13, 13), color);
    } else if (world.has<MeshRenderer>(e)) {
        draw->addQuad(at(1, 4), at(8, 1), at(15, 4), at(8, 8), color);
        draw->addLine(at(1, 4), at(1, 12), color);
        draw->addLine(at(15, 4), at(15, 12), color);
        draw->addLine(at(1, 12), at(8, 16), color);
        draw->addLine(at(15, 12), at(8, 16), color);
        draw->addLine(at(8, 8), at(8, 16), color);
    } else {
        draw->addRect(at(2, 2), at(14, 14), color, 2);
        draw->addLine(at(5, 8), at(11, 8), color);
        draw->addLine(at(8, 5), at(8, 11), color);
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
                             bool* renameLevel = nullptr,
                             Entity* createParent = nullptr) {
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
        ui::setNextWindowPos(ui::Vec2(16, 360), ui::Cond_FirstUseEver);
        ui::setNextWindowSize(ui::Vec2(250, 240), ui::Cond_FirstUseEver);
    }
    if (beginEditorPanel(panel ? "Hierarchy" : "Level Objects", panel ? fixedPanelFlags : 0)) {
        if (focused) {
            *focused = ui::isWindowFocused(ui::FocusedFlags_RootAndChildWindows);
        }
        static std::unordered_set<std::uint64_t> collapsed;
        if (createRequest) {
            ui::beginDisabled(!selectionEnabled);
            if (ui::button(world.alive(selected) ? "Create child..." : "Create...")) {
                ui::openPopup("Hierarchy create");
            }
            if (ui::beginPopup("Hierarchy create")) {
                int item = drawCreateItems();
                if (item >= 0) {
                    *createRequest = item;
                    collapsed.erase(hierarchyKey(selected));
                    if (createParent) {
                        *createParent = selected;
                    }
                }
                ui::endPopup();
            }
            ui::endDisabled();
        }
        static Entity renameTarget;
        static char renameName[256]{};
        static bool renameFocus = false;
        bool renameRequested = false;
        const auto objects = levelObjects(world);
        syncHierarchySelection(world, selected);
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
        static ui::TextFilter search;
        search.draw("Search", -60);
        ui::sameLine();
        if (ui::smallButton("x##ClearSearch")) {
            search.clear();
        }
        if (ui::isItemHovered()) {
            ui::setTooltip("Clear search");
        }
        const auto visible = hierarchyMatches(world, objects, search.inputBuf);

        if (!selectionEnabled) {
            ui::textDisabled("Selection is currently disabled");
        }
        ui::beginDisabled(!selectionEnabled);
        if (!panel && world.alive(selected)) {
            const Entity parent = getParent(world, selected);
            const auto* label = world.tryGet<ObjectName>(parent);
            if (ui::beginCombo("Parent",
                                  parent == Entity{} ? "None"
                                  : label            ? label->value.c_str()
                                                     : "Object")) {
                if (ui::selectable("None", parent == Entity{})) {
                    setParent(world, selected, {});
                }
                for (const auto& object : objects) {
                    if (!canParent(world, selected, object.entity)) {
                        continue;
                    }
                    ui::pushId(static_cast<int>(object.entity.index));
                    if (ui::selectable(object.name.c_str(), parent == object.entity)) {
                        setParent(world, selected, object.entity);
                    }
                    ui::popId();
                }
                ui::endCombo();
            }
        }
        ui::separator();
        ui::beginChild("Objects", ui::Vec2(0, 0));
        auto row = [&](auto&& self, const LevelObject& object, int depth) -> void {
            if (!world.alive(object.entity) || !visible.count(hierarchyKey(object.entity))) {
                return;
            }
            ui::pushId(static_cast<int>(object.entity.index));
            ui::pushId(static_cast<int>(object.entity.generation));
            if (depth) {
                ui::indent(static_cast<float>(depth) * 12);
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
                ui::pushStyleColor(ui::Col_Text,
                                      ui::styleColor(ui::Col_TextDisabled));
            } else if (prefab) {
                ui::pushStyleColor(ui::Col_Text, ui::Color(.35f, .65f, 1, 1));
            }
            const bool hasChildren =
                std::any_of(objects.begin(), objects.end(), [&](const LevelObject& other) {
                    return getParent(world, other.entity) == object.entity;
                });
            const auto key = hierarchyKey(object.entity);
            if (hasChildren) {
                // Let the Hierarchy background show through in every button state.
                ui::pushStyleColor(ui::Col_Button, ui::Color(0, 0, 0, 0));
                ui::pushStyleColor(ui::Col_ButtonHovered, ui::Color(0, 0, 0, 0));
                ui::pushStyleColor(ui::Col_ButtonActive, ui::Color(0, 0, 0, 0));
                if (ui::arrowButton("##Fold",
                                       collapsed.count(key) ? ui::Dir_Right : ui::Dir_Down)) {
                    if (!collapsed.erase(key)) {
                        collapsed.insert(key);
                    }
                }
                ui::popStyleColor(3);
                ui::sameLine(0, 2);
            } else {
                ui::dummy(ui::Vec2(ui::frameHeight(), 1));
                ui::sameLine(0, 2);
            }
            const auto iconPos = ui::cursorScreenPos();
            drawnRows.push_back(object.entity);
            // Reserve pixels rather than spaces (font-dependent widths can overlap the icon).
            if (ui::selectable(("##object" + std::to_string(object.entity.index)).c_str(),
                                  std::find(hierarchySelection.begin(),
                                            hierarchySelection.end(),
                                            object.entity) != hierarchySelection.end(),
                                  ui::SelectableFlags_AllowDoubleClick,
                                  ui::Vec2(0, std::max(20.0f, ui::textLineHeight() + 4)))) {
                if (ui::io().KeyShift && world.alive(selected)) {
                    auto first = std::find(previousRows.begin(), previousRows.end(), selected),
                         last = std::find(previousRows.begin(), previousRows.end(), object.entity);
                    if (first != previousRows.end() && last != previousRows.end()) {
                        if (first > last) {
                            std::swap(first, last);
                        }
                        hierarchySelection.assign(first, last + 1);
                    }
                } else if (ui::io().KeyCtrl) {
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
            if (selectionEnabled && ui::isItemHovered() && ui::isMouseDoubleClicked(0)) {
                renameTarget = object.entity;
                renameRequested = true;
            }
            if (ui::beginPopupContextItem("Object actions")) {
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
                if (createRequest && ui::beginMenu("Create child")) {
                    int item = drawCreateItems();
                    if (item >= 0) {
                        *createRequest = item;
                        collapsed.erase(hierarchyKey(object.entity));
                        if (createParent) {
                            *createParent = object.entity;
                        }
                    }
                    ui::endMenu();
                }
                ui::textUnformatted(object.name.c_str());
                ui::separator();
                if (ui::menuItem("Rename...", "F2")) {
                    renameTarget = object.entity;
                    renameRequested = true;
                }
                if (ui::menuItem("Duplicate")) {
                    selected = duplicateLevelObject(world, object.entity);
                    changed = true;
                }
                if (ui::menuItem(
                        "Unparent", nullptr, false, getParent(world, object.entity) != Entity{})) {
                    for (auto e : hierarchyBatch(world, object.entity)) {
                        setParent(world, e, {});
                    }
                    changed = true;
                }
                if (ui::menuItem("Delete")) {
                    deleteHierarchySelection(world, object.entity);
                    selected = {};
                    changed = true;
                }
                ui::endPopup();
            }
            if (ui::beginDragDropSource()) {
                ui::setDragDropPayload("LEVEL_OBJECT", &object.entity, sizeof(Entity));
                ui::textUnformatted(object.name.c_str());
                ui::endDragDropSource();
            }
            if (ui::beginDragDropTarget()) {
                if (const auto* asset = ui::acceptDragDropPayload("ASSET_PATH")) {
                    if (attachScriptAsset(
                            world, object.entity, static_cast<const char*>(asset->data()))) {
                        changed = true;
                    }
                }
                if (const auto* payload = ui::acceptDragDropPayload("LEVEL_OBJECT")) {
                    const Entity child = *static_cast<const Entity*>(payload->data());
                    reparentDrop(child, object.entity);
                }
                ui::endDragDropTarget();
            }
            hierarchyIcon(world, object.entity, ui::Vec2(iconPos.x, iconPos.y + 2));
            ui::windowDrawList()->addText(ui::Vec2(iconPos.x + 24, iconPos.y + 2),
                                                ui::colorU32(ui::Col_Text),
                                                object.name.c_str());
            if (depth) {
                ui::unindent(static_cast<float>(depth) * 12);
            }
            if (prefab || inactive) {
                ui::popStyleColor();
            }
            ui::popId();
            ui::popId();
            if (depth < 128 && (!collapsed.count(key) || search.inputBuf[0])) {
                for (const auto& child : objects) {
                    if (getParent(world, child.entity) == object.entity) {
                        self(self, child, depth + 1);
                    }
                }
            }
        };
        // Level root is not an entity; dropping here unparents selected roots.
        ui::setNextItemOpen(true, search.inputBuf[0] ? ui::Cond_Always : ui::Cond_Once);
        const auto rootPosition = ui::cursorScreenPos();
        const bool rootOpen = ui::treeNodeEx(
            "##LevelRoot", ui::TreeNodeFlags_SpanAvailWidth, "    %s", levelName);
        if (ui::beginDragDropTarget()) {
            if (const auto* payload = ui::acceptDragDropPayload("LEVEL_OBJECT")) {
                reparentDrop(*static_cast<const Entity*>(payload->data()), {});
            }
            ui::endDragDropTarget();
        }
        if (ui::beginPopupContextItem("Level root")) {
            if (ui::menuItem("Rename Level...", nullptr, false, renameLevel != nullptr) &&
                renameLevel) {
                *renameLevel = true;
            }
            ui::endPopup();
        }
        auto* rootDraw = ui::windowDrawList();
        rootDraw->addQuad(ui::Vec2(rootPosition.x + 22, rootPosition.y + 3),
                          ui::Vec2(rootPosition.x + 28, rootPosition.y),
                          ui::Vec2(rootPosition.x + 34, rootPosition.y + 3),
                          ui::Vec2(rootPosition.x + 28, rootPosition.y + 7),
                          ui::colorU32(ui::Col_Text));
        rootDraw->addLine(ui::Vec2(rootPosition.x + 22, rootPosition.y + 7),
                          ui::Vec2(rootPosition.x + 28, rootPosition.y + 11),
                          ui::colorU32(ui::Col_Text));
        rootDraw->addLine(ui::Vec2(rootPosition.x + 28, rootPosition.y + 11),
                          ui::Vec2(rootPosition.x + 34, rootPosition.y + 7),
                          ui::colorU32(ui::Col_Text));
        if (rootOpen) {
            ui::indent(8);
            for (const auto& object : objects) {
                if (getParent(world, object.entity) == Entity{}) {
                    row(row, object, 0);
                }
            }
            if (visible.empty() && search.inputBuf[0]) {
                ui::textDisabled("No matching objects");
            }
            ui::unindent(8);
            ui::treePop();
        }
        previousRows = drawnRows;
        ui::invisibleButton("##HierarchyBackground",
                               ui::Vec2(std::max(1.f, ui::contentRegionAvail().x),
                                      std::max(32.f, ui::contentRegionAvail().y)));
        if (ui::beginDragDropTarget()) {
            if (const auto* payload = ui::acceptDragDropPayload("LEVEL_OBJECT")) {
                reparentDrop(*static_cast<const Entity*>(payload->data()), {});
            }
            ui::endDragDropTarget();
        }
        if (ui::isItemClicked(ui::MouseButton_Left)) {
            changed = selected != Entity{};
            selected = {};
            hierarchyPrimary = {};
            hierarchySelection.clear();
        }
        if (createRequest && ui::beginPopupContextItem("Hierarchy empty actions")) {
            if (ui::beginMenu("Create")) {
                int item = drawCreateItems();
                if (item >= 0) {
                    *createRequest = item;
                }
                ui::endMenu();
            }
            ui::endPopup();
        }
        if (selectionEnabled && ui::isWindowFocused(ui::FocusedFlags_RootAndChildWindows) &&
            !ui::io().WantTextInput && !ui::isAnyItemActive() &&
            !ui::isPopupOpen("", ui::PopupFlags_AnyPopupId) && world.alive(selected) &&
            ui::isKeyPressed(ui::Key_F2, false)) {
            renameTarget = selected;
            renameRequested = true;
        }
        if (renameRequested && world.alive(renameTarget)) {
            const auto* name = world.tryGet<ObjectName>(renameTarget);
            std::snprintf(
                renameName, sizeof(renameName), "%s", name ? name->value.c_str() : "Object");
            ui::setNextWindowPos(ui::mousePos(), ui::Cond_Appearing);
            ui::openPopup("Rename GameObject");
            renameFocus = true;
        }
        if (ui::beginPopup("Rename GameObject", ui::WindowFlags_AlwaysAutoResize)) {
            if (renameFocus) {
                ui::setKeyboardFocusHere();
                renameFocus = false;
            }
            ui::setNextItemWidth(220);
            const bool enter = ui::inputText("##Name",
                                                renameName,
                                                sizeof(renameName),
                                                ui::InputTextFlags_EnterReturnsTrue |
                                                    ui::InputTextFlags_AutoSelectAll);
            if (enter || ui::button("Rename")) {
                if (world.alive(renameTarget) &&
                    std::string(renameName).find_first_not_of(" \t\r\n") != std::string::npos) {
                    world.add(renameTarget, ObjectName{renameName});
                    changed = true;
                    ui::closeCurrentPopup();
                }
            }
            ui::sameLine();
            if (ui::button("Cancel") || ui::isKeyPressed(ui::Key_Escape)) {
                ui::closeCurrentPopup();
            }
            ui::endPopup();
        }
        if (ui::isWindowHovered() && ui::isMouseClicked(ui::MouseButton_Left) &&
            !ui::isAnyItemHovered()) {
            changed = selected != Entity{};
            selected = {};
        }
        ui::endChild();
        ui::endDisabled();
    }
    ui::endWindow();
    return changed;
}
