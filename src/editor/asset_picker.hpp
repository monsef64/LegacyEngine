#pragma once
#include "editor/asset_icons.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/script_explorer.hpp"
#include "editor/value_context.hpp"
#include "models/model_asset.hpp"
#include <cfloat>
#include <functional>

// Cache virtual model-member inventory while pickers are open. Physical files
// remain the source of truth for file operations/export; virtual entries are refs.
inline const std::vector<std::string>& assetPickerFiles(const AssetManager& assets) {
    static std::filesystem::path root;
    static unsigned revision = ~0u;
    static auto checked = std::chrono::steady_clock::time_point{};
    static std::vector<std::string> choices;
    struct Inventory {
        std::filesystem::file_time_type stamp{};
        unsigned revision = ~0u;
        std::vector<std::string> members;
    };
    static std::map<std::string, Inventory> inventories;
    const auto now = std::chrono::steady_clock::now();
    if (root != assets.root || revision != modelAssetRevision ||
        now - checked > std::chrono::seconds(1)) {
        if (root != assets.root) {
            inventories.clear();
        }
        root = assets.root;
        revision = modelAssetRevision;
        checked = now;
        choices = assets.files();
        const auto files = choices;
        for (const auto& file : files) {
            if (modelAsset(file)) {
                try {
                    auto& inventory = inventories[file];
                    const auto stamp = std::filesystem::last_write_time(assets.resolve(file));
                    if (inventory.stamp != stamp || inventory.revision != modelAssetRevision) {
                        inventory.members.clear();
                        const auto document = modelDocument(assets, file);
                        if (document.contains("embeddedAssets")) {
                            for (auto it = document["embeddedAssets"].begin();
                                 it != document["embeddedAssets"].end();
                                 ++it) {
                                inventory.members.push_back(file + "#" + it.key());
                            }
                        }
                        inventory.stamp = stamp;
                        inventory.revision = modelAssetRevision;
                    }
                    choices.insert(
                        choices.end(), inventory.members.begin(), inventory.members.end());
                } catch (const std::exception&) { /* Corrupt assets stay visible for repair. */
                }
            }
        }
    }
    return choices;
}

// All component/window asset requests share the same filtered, preview-enabled UI.
// Renderer callbacks are optional so header-only/headless clients need no GL context.
inline bool assetPicker(const char* label,
                        std::string& value,
                        const AssetManager* assets,
                        const std::function<bool(const std::string&)>& supported) {
    ImGui::PushID(label);
    bool changed = false;
    if (label[0] != '#') {
        ImGui::TextUnformatted(label);
    }
    static ImGuiID active = 0;
    static ImGuiTextFilter search;
    static bool grid = true;
    static float iconSize = 72;
    static std::string inspected;
    static std::filesystem::path root;
    const auto id = ImGui::GetID("Asset selection");
    if (ImGui::Button(value.empty() ? "None - select asset" : value.c_str(), ImVec2(-1, 0))) {
        active = id;
        search.Clear();
        inspected = value;
        root = assets ? assets->root : std::filesystem::path{};
    }
    {
        char path[4097];
        std::snprintf(path, sizeof(path), "%s", value.c_str());
        if (EditorValues::textContext(path, sizeof(path)) && (path[0] == '\0' || supported(path))) {
            value = path;
            changed = true;
        }
    }
    if (active == id) {
        if (assets && root != assets->root) {
            root = assets->root;
            inspected.clear();
            search.Clear();
        }
        ImGui::SetNextWindowSize(ImVec2(860, 540), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(380, 260), ImVec2(FLT_MAX, FLT_MAX));
        bool open = true;
        if (ImGui::Begin("Select asset###MatchingAssetSelector", &open)) {
            search.Draw("Search", -1);
            int layout = grid ? 1 : 0;
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("View", &layout, "List\0Grid\0")) {
                grid = layout == 1;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Preview size", &iconSize, 40, 112, "%.0f");
            if (ImGui::Selectable("None - clear assignment", value.empty())) {
                value.clear();
                inspected.clear();
                changed = true;
                open = false;
            }
            std::vector<std::string> matching;
            if (assets) {
                for (const auto& path : assetPickerFiles(*assets)) {
                    if (supported(path) && search.PassFilter(path.c_str())) {
                        matching.push_back(path);
                    }
                }
            }
            const bool side = ImGui::GetContentRegionAvail().x >= 620;
            const float detailWidth = 240;
            if (ImGui::BeginChild(
                    "Matching assets",
                    ImVec2(side ? -(detailWidth + ImGui::GetStyle().ItemSpacing.x) : 0, 0),
                    true)) {
                if (matching.empty()) {
                    ImGui::TextDisabled(assets ? "No matching assets"
                                               : "No asset project available");
                }
                const int columns =
                    grid ? std::max(1, int(ImGui::GetContentRegionAvail().x / (iconSize + 40))) : 1;
                if (ImGui::BeginTable(
                        "Asset choices", columns, ImGuiTableFlags_SizingStretchSame)) {
                    const float rowHeight = grid ? iconSize + ImGui::GetTextLineHeight() + 18
                                                 : std::max(36.f, iconSize * .5f) + 8;
                    ImGuiListClipper clipper;
                    clipper.Begin(int((matching.size() + columns - 1) / columns),
                                  rowHeight + ImGui::GetStyle().CellPadding.y * 2);
                    while (clipper.Step()) {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                            ImGui::TableNextRow(0, rowHeight);
                            for (int col = 0; col < columns; ++col) {
                                int index = row * columns + col;
                                if (index >= int(matching.size())) {
                                    break;
                                }
                                ImGui::TableSetColumnIndex(col);
                                const auto& path = matching[index];
                                ImGui::PushID(path.c_str());
                                const auto pos = ImGui::GetCursorScreenPos();
                                const bool clicked = ImGui::Selectable("##Asset",
                                                                       value == path,
                                                                       ImGuiSelectableFlags_None,
                                                                       ImVec2(0, rowHeight));
                                const bool hovered = ImGui::IsItemHovered(),
                                           focused = ImGui::IsItemFocused();
                                const bool visible = ImGui::IsItemVisible();
                                const auto end = ImGui::GetItemRectMax();
                                if (hovered || focused) {
                                    inspected = path;
                                }
                                if (visible) {
                                    AssetPreviewData image;
                                    if (assets && assetThumbnail) {
                                        image = assetThumbnail(*assets, path);
                                    }
                                    const float size = grid ? iconSize : rowHeight - 8;
                                    const ImVec2 origin(
                                        grid ? pos.x + std::max(0.f, (end.x - pos.x - size) * .5f)
                                             : pos.x + 4,
                                        pos.y + 3);
                                    if (image.texture) {
                                        ImGui::GetWindowDrawList()->AddImage(
                                            static_cast<ImTextureID>(image.texture),
                                            origin,
                                            {origin.x + size, origin.y + size},
                                            image.flip ? ImVec2(0, 1) : ImVec2(0, 0),
                                            image.flip ? ImVec2(1, 0) : ImVec2(1, 1));
                                    } else {
                                        fileBadge(path, origin, false, 0, size);
                                    }
                                    auto name = std::filesystem::path(path).filename().string();
                                    // Ellipsize on UTF-8 codepoint boundaries; full relative path
                                    // is in the tooltip.
                                    const float available =
                                        std::max(10.f, end.x - pos.x - (grid ? 8 : size + 14));
                                    if (ImGui::CalcTextSize(name.c_str()).x > available) {
                                        while (!name.empty() &&
                                               ImGui::CalcTextSize((name + "...").c_str()).x >
                                                   available) {
                                            auto end = name.size() - 1;
                                            while (end > 0 &&
                                                   (static_cast<unsigned char>(name[end]) & 0xc0) ==
                                                       0x80) {
                                                --end;
                                            }
                                            name.resize(end);
                                        }
                                        name += "...";
                                    }
                                    const ImVec2 text(
                                        grid ? pos.x +
                                                   std::max(4.f,
                                                            (end.x - pos.x -
                                                             ImGui::CalcTextSize(name.c_str()).x) *
                                                                .5f)
                                             : origin.x + size + 8,
                                        grid
                                            ? origin.y + size + 4
                                            : origin.y + (size - ImGui::GetTextLineHeight()) * .5f);
                                    ImGui::GetWindowDrawList()->AddText(
                                        text, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                                }
                                if (hovered && ImGui::BeginTooltip()) {
                                    ImGui::TextUnformatted(path.c_str());
                                    if (!side && assets) {
                                        drawAssetPreview(*assets, path, 230, 3);
                                    }
                                    ImGui::EndTooltip();
                                }
                                if (clicked) {
                                    value = path;
                                    changed = true;
                                    open = false;
                                }
                                ImGui::PopID();
                            }
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
            if (side) {
                ImGui::SameLine();
                if (ImGui::BeginChild("Asset preview", ImVec2(0, 0), true)) {
                    if (assets && !inspected.empty() && supported(inspected)) {
                        ImGui::TextWrapped("%s", inspected.c_str());
                        ImGui::Separator();
                        drawAssetPreview(*assets,
                                         inspected,
                                         std::max(40.f, ImGui::GetContentRegionAvail().x),
                                         3);
                        if (!assetPreview) {
                            ImGui::TextDisabled("Preview renderer unavailable");
                        }
                    } else {
                        ImGui::TextWrapped(
                            "Hover or focus an asset to preview it. Click to assign.");
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
        if (!open) {
            active = 0;
        }
    }
    ImGui::PopID();
    return changed;
}
