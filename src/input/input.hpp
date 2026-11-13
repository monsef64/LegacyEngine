#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
struct InputState {
    std::array<bool, SDL_SCANCODE_COUNT> keys{}, pressed{}, released{};
    std::array<bool, 9> mouse{}, mousePressed{}, mouseReleased{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> pad{}, padPressed{}, padReleased{};
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> axes{};
    std::map<std::string, int> buttons; // primary slot; legacy keyboard codes remain unchanged
    std::map<std::string, std::array<int, 2>> alternatives;
    SDL_Window* window = nullptr;
    bool mouseLocked = false, mouseVisible = true;
    static constexpr int mouseCode = 1024, gamepadCode = 2048;
    static bool validBinding(int code) {
        return code == 0 || (code > 0 && code < SDL_SCANCODE_COUNT) ||
               (code > mouseCode && code < mouseCode + 9) ||
               (code >= gamepadCode && code < gamepadCode + SDL_GAMEPAD_BUTTON_COUNT);
    }
    int binding(const std::string& name, int slot) const {
        if (slot < 0 || slot > 2) {
            return 0;
        }
        if (slot == 0) {
            auto it = buttons.find(name);
            return it == buttons.end() ? 0 : it->second;
        }
        auto it = alternatives.find(name);
        return it == alternatives.end() ? 0 : it->second[slot - 1];
    }
    void bind(const std::string& name, int slot, int code) {
        if (slot < 0 || slot > 2 || !validBinding(code)) {
            throw std::runtime_error("Invalid binding slot or code");
        }
        if (slot == 0) {
            buttons[name] = code;
        } else {
            buttons.try_emplace(name, 0);
            alternatives[name][slot - 1] = code;
        }
    }
    bool lockCursor(bool locked) {
        if (!window) {
            return false;
        }
        if (!SDL_SetWindowRelativeMouseMode(window, locked)) {
            return false;
        }
        mouseLocked = locked;
        return true;
    }
    bool showCursor(bool visible) {
        if (!window) {
            return false;
        }
        if (!(visible ? SDL_ShowCursor() : SDL_HideCursor())) {
            return false;
        }
        mouseVisible = visible;
        return true;
    }
    void releaseMouse() {
        if (window) {
            SDL_SetWindowRelativeMouseMode(window, false);
            SDL_ShowCursor();
        }
        mouseLocked = false;
        mouseVisible = true;
    }

    float x = 0, y = 0, dx = 0, dy = 0, wheelX = 0, wheelY = 0;
    bool enabled = false, mouseEnabled = false;
    SDL_Gamepad* gamepad = nullptr;
    inline static InputState* current = nullptr;
    InputState() {
        current = this;
    }
    ~InputState() {
        if (gamepad) {
            SDL_CloseGamepad(gamepad);
        }
        if (current == this) {
            current = nullptr;
        }
    }
    void clear() {
        keys.fill(false);
        pressed.fill(false);
        released.fill(false);
        mouse.fill(false);
        mousePressed.fill(false);
        mouseReleased.fill(false);
        pad.fill(false);
        padPressed.fill(false);
        padReleased.fill(false);
        axes.fill(0);
        dx = dy = wheelX = wheelY = 0;
    }
    void beginFrame() {
        pressed.fill(false);
        released.fill(false);
        mousePressed.fill(false);
        mouseReleased.fill(false);
        padPressed.fill(false);
        padReleased.fill(false);
        dx = dy = wheelX = wheelY = 0;
        if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
            SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        }
        if (gamepad && !SDL_GamepadConnected(gamepad)) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
            pad.fill(false);
            axes.fill(0);
        }
        if (!gamepad) {
            int count = 0;
            auto* ids = SDL_GetGamepads(&count);
            if (count) {
                gamepad = SDL_OpenGamepad(ids[0]);
            }
            SDL_free(ids);
        }
        if (!enabled) {
            clear();
            return;
        }
        if (gamepad) {
            SDL_PumpEvents();
            SDL_UpdateGamepads();
            for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
                bool down = SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(i));
                padPressed[i] = down && !pad[i];
                padReleased[i] = !down && pad[i];
                pad[i] = down;
            }
            for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
                float v = SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(i)) / 32767.0f;
                axes[i] = std::abs(v) < .15f ? 0 : std::clamp(v, -1.0f, 1.0f);
            }
        }
    }
    void event(const SDL_Event& e) {
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            clear();
            releaseMouse();
            return;
        }
        if (e.type == SDL_EVENT_KEY_UP || e.type == SDL_EVENT_KEY_DOWN) {
            int key = e.key.scancode;
            if (key > 0 && key < SDL_SCANCODE_COUNT) {
                bool down = e.type == SDL_EVENT_KEY_DOWN;
                if (down && enabled && !e.key.repeat) {
                    pressed[key] |= !keys[key];
                    keys[key] = true;
                }
                if (!down) {
                    released[key] |= enabled && keys[key];
                    keys[key] = false;
                }
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            int button = e.button.button;
            if (button > 0 && button < 9) {
                bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                if (down && enabled && mouseEnabled) {
                    mousePressed[button] |= !mouse[button];
                    mouse[button] = true;
                }
                if (!down) {
                    mouseReleased[button] |= enabled && mouse[button];
                    mouse[button] = false;
                }
            }
        }
        if ((e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
            enabled && (!gamepad || e.gbutton.which == SDL_GetGamepadID(gamepad)) &&
            e.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
            const int i = e.gbutton.button;
            const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            if (down) {
                padPressed[i] |= !pad[i];
                pad[i] = true;
            } else {
                padReleased[i] |= pad[i];
                pad[i] = false;
            }
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            x = e.motion.x;
            y = e.motion.y;
            if (enabled && mouseEnabled) {
                dx += e.motion.xrel;
                dy += e.motion.yrel;
            }
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL && enabled && mouseEnabled) {
            float sign = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
            wheelX += e.wheel.x * sign;
            wheelY += e.wheel.y * sign;
        }
    }
    bool bindingState(int code, int phase) const {
        if (code > 0 && code < SDL_SCANCODE_COUNT) {
            return phase == 0 ? pressed[code] : phase == 1 ? keys[code] : released[code];
        }
        if (code > mouseCode && code < mouseCode + 9) {
            int i = code - mouseCode;
            return phase == 0 ? mousePressed[i] : phase == 1 ? mouse[i] : mouseReleased[i];
        }
        if (code >= gamepadCode && code < gamepadCode + SDL_GAMEPAD_BUTTON_COUNT) {
            int i = code - gamepadCode;
            return phase == 0 ? padPressed[i] : phase == 1 ? pad[i] : padReleased[i];
        }
        return false;
    }
    bool button(const std::string& name, int phase) const {
        if (!enabled || !buttons.count(name)) {
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            if (bindingState(binding(name, i), phase)) {
                return true;
            }
        }
        return false;
    }
    float axis(const std::string& negative, const std::string& positive) const {
        return float(button(positive, 1)) - float(button(negative, 1));
    }
};
