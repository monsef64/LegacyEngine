#pragma once
#include "editor/script_explorer.hpp"
#include <functional>
// Filter first so empty/unsupported folders never appear in the picker.
inline bool assetPicker(const char* label,
                        std::string& value,
                        const AssetManager* assets,
                        const std::function<bool(const std::string&)>& supported) {
    ui::pushId(label);
    bool changed = false;
    if (label[0] != '#') {
        ui::textUnformatted(label);
    }
    static ui::ID active = 0;
    static ui::TextFilter search;
    const auto id = ui::getId("Asset selection");
    if (ui::button(value.empty() ? "None - select asset" : value.c_str(), ui::Vec2(-1, 0))) {
        active = id;
        search.clear();
    }
    if (active == id) {
        ui::setNextWindowSize(ui::Vec2(560, 420), ui::Cond_FirstUseEver);
        bool open = true;
        if (ui::beginWindow("Select asset###MatchingAssetSelector", &open)) {
            search.draw("Search", -1);
            ui::beginChild("Matching assets");
            if (ui::selectable("None", value.empty())) {
                value.clear();
                changed = true;
                open = false;
            }
            if (assets) {
                for (const auto& path : assets->files()) {
                    if (supported(path) && search.passFilter(path.c_str()) &&
                        ui::selectable(path.c_str(), value == path)) {
                        value = path;
                        changed = true;
                        open = false;
                    }
                }
            }
            ui::endChild();
        }
        ui::endWindow();
        if (!open) {
            active = 0;
        }
    }
    ui::popId();
    return changed;
}
