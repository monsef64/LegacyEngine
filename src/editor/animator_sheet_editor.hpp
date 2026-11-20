#pragma once
#include "animation/animator_sheet.hpp"
#include "editor/asset_picker.hpp"
#include <imgui.h>

inline void drawAnimatorSheetInspector(const AssetManager& assets, const std::string& path) {
    static std::filesystem::path root;
    static std::string current, status;
    static Json doc;
    auto load = [&] {
        doc = readJsonFile(assets.resolve(path), 2 * 1024 * 1024);
        Animators::validate(doc);
        status.clear();
    };
    try {
        if (root != assets.root || current != path) {
            root = assets.root;
            current = path;
            doc = Json();
            load();
        }
        ImGui::TextWrapped("AnimatorSheet: %s", path.c_str());
        if (ImGui::Button("Reload sheet")) {
            load();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save sheet")) {
            Animators::validate(doc);
            writeJsonFile(assets.resolve(path), doc);
            status = "AnimatorSheet saved.";
        }
        if (doc.is_null()) {
            ImGui::TextWrapped("%s", status.c_str());
            return;
        }
        auto text = [](const char* label, Json& value) {
            char buffer[4097];
            std::snprintf(buffer, sizeof(buffer), "%s", value.get<std::string>().c_str());
            if (ImGui::InputText(label, buffer, sizeof(buffer))) {
                value = std::string(buffer);
            }
        };
        auto number = [](const char* label, Json& value, float min, float max) {
            float v = value.get<float>();
            if (ImGui::DragFloat(label, &v, .05f, min, max, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
                value = v;
            }
        };
        text("Entry state", doc["entry"]);
        ImGui::TextWrapped(
            "State names are unique. Clips must target the same model for blending. Transitions are evaluated top to bottom; * means Any State; exit time -1 disables its time gate. Save validates all references.");
        if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& list = doc["parameters"];
            if (list.size() < 64 && ImGui::Button("Add parameter")) {
                list.push_back({{"name", "Parameter_" + std::to_string(list.size())},
                                {"type", "float"},
                                {"default", 0}});
            }
            int remove = -1;
            for (std::size_t i = 0; i < list.size(); ++i) {
                auto& p = list[i];
                ImGui::PushID(int(i));
                text("Name", p["name"]);
                int type = p["type"] == "float" ? 0 : p["type"] == "bool" ? 1 : 2;
                if (ImGui::Combo("Type", &type, "Float\0Bool\0Trigger\0")) {
                    p["type"] = type == 0 ? "float" : type == 1 ? "bool" : "trigger";
                    p["default"] = type == 0 ? Json(0) : Json(false);
                }
                if (type == 0) {
                    number("Default", p["default"], -1e6f, 1e6f);
                } else {
                    bool v = p["default"].get<bool>();
                    if (ImGui::Checkbox("Default", &v)) {
                        p["default"] = v;
                    }
                }
                if (ImGui::SmallButton("Remove parameter")) {
                    remove = int(i);
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (remove >= 0) {
                list.erase(list.begin() + remove);
            }
        }
        if (ImGui::CollapsingHeader("States", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& list = doc["states"];
            if (list.size() < 128 && ImGui::Button("Add state")) {
                list.push_back({{"name", "State_" + std::to_string(list.size())},
                                {"clip", ""},
                                {"loop", true}});
            }
            int remove = -1;
            for (std::size_t i = 0; i < list.size(); ++i) {
                auto& s = list[i];
                ImGui::PushID(1000 + int(i));
                text("State", s["name"]);
                auto clip = s["clip"].get<std::string>();
                if (assetPicker("Animation clip", clip, &assets, [](const std::string& p) {
                        return animationAsset(p);
                    })) {
                    s["clip"] = clip;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        std::string path = static_cast<const char*>(p->Data);
                        if (animationAsset(path)) {
                            s["clip"] = path;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                bool loop = s["loop"].get<bool>();
                if (ImGui::Checkbox("Loop", &loop)) {
                    s["loop"] = loop;
                }
                if (list.size() > 1 && ImGui::SmallButton("Remove state")) {
                    remove = int(i);
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (remove >= 0) {
                list.erase(list.begin() + remove);
            }
        }
        if (ImGui::CollapsingHeader("Transitions", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& list = doc["transitions"];
            if (list.size() < 256 && ImGui::Button("Add transition")) {
                list.push_back({{"from", doc["entry"]},
                                {"to", doc["entry"]},
                                {"duration", .2f},
                                {"exitTime", -1},
                                {"conditions", Json::array()}});
            }
            int remove = -1;
            for (std::size_t i = 0; i < list.size(); ++i) {
                auto& t = list[i];
                ImGui::PushID(2000 + int(i));
                text("From", t["from"]);
                text("To", t["to"]);
                if (!t.contains("duration")) {
                    t["duration"] = .2f;
                }
                if (!t.contains("exitTime")) {
                    t["exitTime"] = -1;
                }
                number("Blend seconds", t["duration"], 0, 30);
                number("Exit normalized time", t["exitTime"], -1, 1);
                auto& conditions = t["conditions"];
                if (conditions.size() < 8 && ImGui::SmallButton("Add condition")) {
                    conditions.push_back({{"parameter", ""}, {"op", ">"}, {"value", 0}});
                }
                int removeCondition = -1;
                for (std::size_t j = 0; j < conditions.size(); ++j) {
                    auto& c = conditions[j];
                    ImGui::PushID(int(j));
                    text("Parameter", c["parameter"]);
                    const char* ops[]{"==", "!=", ">", "<", "trigger"};
                    int op = 0;
                    for (int k = 0; k < 5; ++k) {
                        if (c["op"] == ops[k]) {
                            op = k;
                        }
                    }
                    if (ImGui::Combo("Condition", &op, "==\0!=\0>\0<\0trigger\0")) {
                        c["op"] = ops[op];
                    }
                    if (op != 4) {
                        bool boolean = false;
                        for (const auto& p : doc["parameters"]) {
                            if (p["name"] == c["parameter"]) {
                                boolean = p["type"] != "float";
                            }
                        }
                        if (boolean) {
                            if (!c.contains("value") || !c["value"].is_boolean()) {
                                c["value"] = false;
                            }
                            bool v = c["value"].get<bool>();
                            if (ImGui::Checkbox("Value", &v)) {
                                c["value"] = v;
                            }
                        } else {
                            if (!c.contains("value") || !c["value"].is_number()) {
                                c["value"] = 0;
                            }
                            number("Value", c["value"], -1e6f, 1e6f);
                        }
                    }
                    if (ImGui::SmallButton("Remove condition")) {
                        removeCondition = int(j);
                    }
                    ImGui::PopID();
                }
                if (removeCondition >= 0) {
                    conditions.erase(conditions.begin() + removeCondition);
                }
                if (i > 0 && ImGui::SmallButton("Move transition up")) {
                    std::swap(list[i], list[i - 1]);
                }
                if (ImGui::SmallButton("Remove transition")) {
                    remove = int(i);
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (remove >= 0) {
                list.erase(list.begin() + remove);
            }
        }
    } catch (const std::exception& e) {
        status = e.what();
    }
    if (!status.empty()) {
        ImGui::TextWrapped("%s", status.c_str());
    }
}
