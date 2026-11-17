#pragma once
#include "editor/asset_icons.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/script_explorer.hpp"
#include "editor/value_context.hpp"
#include "ui/mygui_ui.hpp"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <vector>

// All component/window asset requests share the same filtered, preview-enabled UI.
// Renderer callbacks are optional so header-only/headless clients need no GL context.
inline bool assetPicker(const char* label,
                        std::string& value,
                        const AssetManager* assets,
                        const std::function<bool(const std::string&)>& supported) {
    ui::pushId(label);
    bool changed = false;
    if (label[0] != '#') ui::textUnformatted(label);

    static ui::ID active = 0;
    static ui::TextFilter search;
    static bool grid = true;
    static float iconSize = 72;
    static std::string inspected;
    static std::filesystem::path root;

    const auto id = ui::getId("Asset selection");
    if (ui::button(value.empty() ? "None - select asset" : value.c_str(), ui::Vec2(-1, 0))) {
        active = id;
        search.clear();
        inspected = value;
        root = assets ? assets->root : std::filesystem::path{};
    }
    {
        char path[4097];
        std::snprintf(path, sizeof(path), "%s", value.c_str());
        if (EditorValues::textContext(path, sizeof(path)) &&
            (path[0] == '\0' || (supported && supported(path)))) {
            value = path;
            changed = true;
        }
    }

    if (active == id) {
        if (assets && root != assets->root) {
            root = assets->root;
            inspected.clear();
            search.clear();
        }
        ui::setNextWindowSize(ui::Vec2(860, 540), ui::Cond_FirstUseEver);
        ui::setNextWindowSizeConstraints(ui::Vec2(380, 260), ui::Vec2(FLT_MAX, FLT_MAX));
        bool open = true;
        if (ui::beginWindow("Select asset###MatchingAssetSelector", &open)) {
            search.draw("Search", -1);
            int layout = grid ? 1 : 0;
            ui::setNextItemWidth(100);
            if (ui::combo("View", &layout, "List\0Grid\0")) grid = layout == 1;
            ui::sameLine();
            ui::setNextItemWidth(150);
            ui::sliderFloat("Preview size", &iconSize, 40, 112, "%.0f");
            if (ui::selectable("None - clear assignment", value.empty())) {
                value.clear();
                inspected.clear();
                changed = true;
                open = false;
            }

            std::vector<std::string> matching;
            if (assets) {
                for (const auto& path : assets->files()) {
                    if (supported && supported(path) && search.passFilter(path.c_str())) {
                        matching.push_back(path);
                    }
                }
            }

            const bool side = ui::contentRegionAvail().x >= 620;
            constexpr float detailWidth = 240;
            const float spacing = ui::style().ItemSpacing.x;
            ui::beginChild("Matching assets",
                           ui::Vec2(side ? -(detailWidth + spacing) : 0, 0),
                           ui::ChildFlags_Borders);
            if (matching.empty()) {
                ui::textDisabled(assets ? "No matching assets" : "No asset project available");
            }
            const int columns =
                grid ? std::max(1, static_cast<int>(ui::contentRegionAvail().x / (iconSize + 40)))
                     : 1;
            if (ui::beginTable("Asset choices", columns, ui::TableFlags_SizingStretchSame)) {
                for (int col = 0; col < columns; ++col) {
                    const auto idText = "##asset-column-" + std::to_string(col);
                    ui::tableSetupColumn(idText.c_str(), ui::TableColumnFlags_WidthStretch);
                }
                const float rowHeight = grid ? iconSize + ui::calcTextSize("A").y + 18
                                             : std::max(36.f, iconSize * .5f) + 8;
                for (std::size_t row = 0; row * static_cast<std::size_t>(columns) < matching.size();
                     ++row) {
                    ui::tableNextRow(rowHeight);
                    for (int col = 0; col < columns; ++col) {
                        const auto index = row * static_cast<std::size_t>(columns) +
                                           static_cast<std::size_t>(col);
                        if (index >= matching.size()) break;
                        ui::tableSetColumnIndex(col);
                        const auto& path = matching[index];
                        ui::pushId(path.c_str());
                        const auto pos = ui::cursorScreenPos();
                        const bool clicked = ui::selectable("##Asset",
                                                            value == path,
                                                            ui::SelectableFlags_None,
                                                            ui::Vec2(0, rowHeight));
                        const bool hovered = ui::isItemHovered();
                        const bool visible = ui::isItemVisible();
                        const auto end = ui::itemRectMax();
                        if (hovered) inspected = path;
                        if (visible) {
                            AssetPreviewData image;
                            if (assets && assetThumbnail) image = assetThumbnail(*assets, path);
                            const float size = grid ? iconSize : rowHeight - 8;
                            const ui::Vec2 origin(
                                grid ? pos.x + std::max(0.f, (end.x - pos.x - size) * .5f)
                                     : pos.x + 4,
                                pos.y + 3);
                            if (image.texture) {
                                ui::windowDrawList()->addImage(
                                    static_cast<ui::TextureID>(image.texture),
                                    origin,
                                    ui::Vec2(origin.x + size, origin.y + size),
                                    image.flip ? ui::Vec2(0, 1) : ui::Vec2(0, 0),
                                    image.flip ? ui::Vec2(1, 0) : ui::Vec2(1, 1));
                            } else {
                                fileBadge(path, origin, false, 0, size);
                            }
                            auto name = std::filesystem::path(path).filename().string();
                            const float available =
                                std::max(10.f, end.x - pos.x - (grid ? 8 : size + 14));
                            if (ui::calcTextSize(name.c_str()).x > available) {
                                while (!name.empty() &&
                                       ui::calcTextSize((name + "...").c_str()).x > available) {
                                    auto cut = name.size() - 1;
                                    while (cut > 0 &&
                                           (static_cast<unsigned char>(name[cut]) & 0xc0) == 0x80) {
                                        --cut;
                                    }
                                    name.resize(cut);
                                }
                                name += "...";
                            }
                            const float nameWidth = ui::calcTextSize(name.c_str()).x;
                            const ui::Vec2 text(
                                grid ? pos.x + std::max(4.f, (end.x - pos.x - nameWidth) * .5f)
                                     : origin.x + size + 8,
                                grid ? origin.y + size + 4
                                     : origin.y + (size - ui::calcTextSize("A").y) * .5f);
                            ui::windowDrawList()->addText(
                                text, ui::colorU32(ui::Col_Text), name.c_str());
                        }
                        if (hovered && ui::beginTooltip()) {
                            ui::textUnformatted(path.c_str());
                            if (!side && assets) drawAssetPreview(*assets, path, 230, 3);
                            ui::endTooltip();
                        }
                        if (clicked) {
                            value = path;
                            changed = true;
                            open = false;
                        }
                        ui::popId();
                    }
                }
                ui::endTable();
            }
            ui::endChild();

            if (side) {
                ui::sameLine();
                ui::beginChild("Asset preview", ui::Vec2(0, 0), ui::ChildFlags_Borders);
                if (assets && !inspected.empty() && supported && supported(inspected)) {
                    ui::textWrapped("%s", inspected.c_str());
                    ui::separator();
                    drawAssetPreview(
                        *assets, inspected, std::max(40.f, ui::contentRegionAvail().x), 3);
                    if (!assetPreview) ui::textDisabled("Preview renderer unavailable");
                } else {
                    ui::textWrapped("Hover or focus an asset to preview it. Click to assign.");
                }
                ui::endChild();
            }
        }
        ui::endWindow();
        if (!open) active = 0;
    }
    ui::popId();
    return changed;
}
