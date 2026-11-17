#pragma once
#include "assets/assets.hpp"
#include "input/input.hpp"
#include "project/json_io.hpp"
#include <fstream>
#include <iomanip>
struct InputSettings {
    bool visible = false;
    std::string waiting, error;
    char name[129]{};
    int waitingSlot = 0;
    std::filesystem::path root;
    static std::filesystem::path settingsPath(const AssetManager& assets) {
        return (assets.root.filename() == "Assets" ? assets.root.parent_path() : assets.root) /
               "Settings" / "InputSettings.json";
    }
    void load(const AssetManager& assets, InputState& input) {
        if (root == assets.root) {
            return;
        }
        input.clear();
        root = assets.root;
        input.buttons.clear();
        input.alternatives.clear();
        waiting.clear();
        error.clear();
        try {
            const auto jsonPath = settingsPath(assets);
            if (std::filesystem::exists(jsonPath)) {
                const auto data = readJsonFile(jsonPath, 64 * 1024);
                const int version = data.at("version");
                if ((version != 1 && version != 2) || !data.at("buttons").is_object() ||
                    data.at("buttons").size() > 256) {
                    throw std::runtime_error("Invalid input settings");
                }
                std::map<std::string, int> primary;
                std::map<std::string, std::array<int, 2>> extra;
                for (auto it = data.at("buttons").begin(); it != data.at("buttons").end(); ++it) {
                    auto label = it.key();
                    if (label.empty() || label.size() > 128 ||
                        label.find('\0') != std::string::npos) {
                        throw std::runtime_error("Invalid input name");
                    }
                    auto values = version == 1 ? Json::array({it.value(), 0, 0}) : it.value();
                    if (!values.is_array() || values.size() != 3) {
                        throw std::runtime_error("Expected three input binding slots");
                    }
                    std::array<int, 3> codes{};
                    for (int slot = 0; slot < 3; ++slot) {
                        if (!values[slot].is_number_integer()) {
                            throw std::runtime_error("Invalid input binding");
                        }
                        auto n = values[slot].get<std::int64_t>();
                        if (n < 0 || n > 4096 || !InputState::validBinding(int(n))) {
                            throw std::runtime_error("Invalid input binding");
                        }
                        codes[slot] = int(n);
                    }
                    primary[label] = codes[0];
                    extra[label] = {codes[1], codes[2]};
                }
                input.buttons = std::move(primary);
                input.alternatives = std::move(extra);
                return;
            }
            auto path = assets.resolve(".legacy-input");
            if (!std::filesystem::exists(path)) {
                return;
            }
            if (std::filesystem::file_size(path) > 64 * 1024) {
                throw std::runtime_error("Input settings exceed 64 KiB");
            }
            std::ifstream in(path);
            if (!in) {
                throw std::runtime_error("Cannot open input settings");
            }
            std::string label;
            int key = 0;
            std::map<std::string, int> next;
            while (in >> std::ws && in.peek() != std::char_traits<char>::eof()) {
                if (!(in >> std::quoted(label) >> key) || label.empty() || label.size() > 128 ||
                    label.find('\0') != std::string::npos || key <= 0 ||
                    key >= SDL_SCANCODE_COUNT || next.size() >= 256) {
                    throw std::runtime_error("Invalid input settings");
                }
                next[label] = key;
            }
            if (in.bad()) {
                throw std::runtime_error("Cannot read input settings");
            }
            input.buttons = std::move(next);
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    void save(const AssetManager& assets, const InputState& input) {
        try {
            Json bindings = Json::object();
            for (const auto& pair : input.buttons) {
                bindings[pair.first] = Json::array({input.binding(pair.first, 0),
                                                    input.binding(pair.first, 1),
                                                    input.binding(pair.first, 2)});
            }
            writeJsonFile(settingsPath(assets), Json{{"version", 2}, {"buttons", bindings}});
            error.clear();
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    bool capture(const SDL_Event& e, const AssetManager& assets, InputState& input) {
        if (waiting.empty()) {
            return false;
        }
        int code = -1;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                waiting.clear();
                input.clear();
                return true;
            }
            if (e.key.scancode != SDL_SCANCODE_UNKNOWN) {
                code = e.key.scancode;
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button > 0 && e.button.button < 9) {
            code = InputState::mouseCode + e.button.button;
        }
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            e.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
            code = InputState::gamepadCode + e.gbutton.button;
        }
        if (code >= 0) {
            input.bind(waiting, waitingSlot, code);
            save(assets, input);
            waiting.clear();
            input.clear();
            return true;
        }
        return e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP ||
               e.type == SDL_EVENT_MOUSE_BUTTON_UP || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP;
    }
    static std::string label(int code) {
        if (code == 0) {
            return "Unassigned";
        }
        if (code > InputState::mouseCode && code < InputState::mouseCode + 9) {
            const char* names[]{"", "Left", "Middle", "Right", "X1", "X2"};
            int button = code - InputState::mouseCode;
            return std::string("Mouse ") + (button <= 5 ? names[button] : std::to_string(button));
        }
        if (code >= InputState::gamepadCode) {
            const char* name = SDL_GetGamepadStringForButton(
                static_cast<SDL_GamepadButton>(code - InputState::gamepadCode));
            return std::string("Pad ") + (name ? name : "?");
        }
        return SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
    }
};
