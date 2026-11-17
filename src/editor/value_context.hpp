#pragma once
#include "project/json_io.hpp"
#include "ui/mygui_ui.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

// Numeric and text controls share a small JSON copy/paste context menu. This
// keeps the editor's value workflow independent of the underlying UI toolkit.
namespace EditorValues {
template <class T>
inline bool
context(T* values, int count, T low = T{}, T high = T{}, const char* popupID = nullptr) {
    if (!values || count <= 0 || count > 4) return false;
    bool changed = false;
    if (ui::beginPopupContextItem(popupID)) {
        if (ui::menuItem("Copy")) {
            Json json;
            if (count == 1) {
                json = values[0];
            } else {
                json = Json::array();
                for (int i = 0; i < count; ++i) json.push_back(values[i]);
            }
            const auto text = json.dump();
            ui::setClipboardText(text.c_str());
        }
        std::array<T, 4> candidate{};
        bool valid = false;
        try {
            const auto text = ui::getClipboardText();
            if (text.size() <= 1024) {
                const auto json = Json::parse(text);
                valid = count == 1 ? !json.is_array()
                                   : json.is_array() && static_cast<int>(json.size()) == count;
                if (valid) {
                    for (int i = 0; i < count; ++i) {
                        const auto& value = count == 1 ? json : json[i];
                        if constexpr (std::is_same_v<T, bool>) {
                            if (!value.is_boolean()) {
                                valid = false;
                                break;
                            }
                            candidate[i] = value.get<bool>();
                        } else {
                            if (!value.is_number()) {
                                valid = false;
                                break;
                            }
                            const double number = value.get<double>();
                            if (!std::isfinite(number) ||
                                number < static_cast<double>(std::numeric_limits<T>::lowest()) ||
                                number > static_cast<double>(std::numeric_limits<T>::max()) ||
                                (std::is_integral_v<T> && std::floor(number) != number)) {
                                valid = false;
                                break;
                            }
                            candidate[i] = static_cast<T>(number);
                            if (low < high) candidate[i] = std::clamp(candidate[i], low, high);
                        }
                    }
                }
            }
        } catch (const std::exception&) {
        }
        if (ui::menuItem("Paste", nullptr, false, valid)) {
            for (int i = 0; i < count; ++i) {
                changed |= values[i] != candidate[i];
                values[i] = candidate[i];
            }
        }
        ui::endPopup();
    }
    return changed;
}

inline bool textContext(char* value, std::size_t capacity) {
    if (!value || capacity == 0) return false;
    bool changed = false;
    if (ui::beginPopupContextItem()) {
        if (ui::menuItem("Copy")) {
            const auto text = Json(std::string(value)).dump();
            ui::setClipboardText(text.c_str());
        }
        std::string candidate;
        bool valid = false;
        try {
            const auto text = ui::getClipboardText();
            if (text.size() <= 65536) {
                const auto json = Json::parse(text);
                if (json.is_string()) {
                    candidate = json.get<std::string>();
                    valid = candidate.size() < capacity &&
                            candidate.find('\0') == std::string::npos;
                }
            }
        } catch (const std::exception&) {
        }
        if (ui::menuItem("Paste", nullptr, false, valid)) {
            changed = candidate != value;
            std::memcpy(value, candidate.c_str(), candidate.size() + 1);
        }
        ui::endPopup();
    }
    return changed;
}

template <class... Args>
inline bool InputText(const char* label, char* value, std::size_t capacity, Args... args) {
    const bool changed = ui::inputText(label, value, capacity, args...);
    return textContext(value, capacity) || changed;
}

inline bool DragFloat(const char* label,
                      float* value,
                      float speed = 1,
                      float low = 0,
                      float high = 0,
                      const char* format = "%.3f",
                      ui::SliderFlags flags = ui::SliderFlags_None) {
    const bool changed = ui::dragFloat(label, value, speed, low, high, format, flags);
    return context(value, 1, low, high) || changed;
}
inline bool DragFloat2(const char* label,
                       float* value,
                       float speed = 1,
                       float low = 0,
                       float high = 0,
                       const char* format = "%.3f",
                       ui::SliderFlags flags = ui::SliderFlags_None) {
    const bool changed = ui::dragFloat2(label, value, speed, low, high, format, flags);
    return context(value, 2, low, high) || changed;
}
inline bool DragFloat3(const char* label,
                       float* value,
                       float speed = 1,
                       float low = 0,
                       float high = 0,
                       const char* format = "%.3f",
                       ui::SliderFlags flags = ui::SliderFlags_None) {
    const bool changed = ui::dragFloat3(label, value, speed, low, high, format, flags);
    return context(value, 3, low, high) || changed;
}
inline bool DragFloat4(const char* label,
                       float* value,
                       float speed = 1,
                       float low = 0,
                       float high = 0,
                       const char* format = "%.3f",
                       ui::SliderFlags flags = ui::SliderFlags_None) {
    const bool changed = ui::dragFloat4(label, value, speed, low, high, format, flags);
    return context(value, 4, low, high) || changed;
}
inline bool DragInt(const char* label,
                    int* value,
                    float speed = 1,
                    int low = 0,
                    int high = 0,
                    const char* format = "%d",
                    ui::SliderFlags flags = ui::SliderFlags_None) {
    const bool changed = ui::dragInt(label, value, speed, low, high, format, flags);
    return context(value, 1, low, high) || changed;
}
inline bool SliderFloat(const char* label,
                        float* value,
                        float low,
                        float high,
                        const char* format = "%.3f",
                        ui::SliderFlags flags = ui::SliderFlags_None) {
    const bool changed = ui::sliderFloat(label, value, low, high, format, flags);
    return context(value, 1, low, high) || changed;
}
inline bool SliderInt(const char* label,
                      int* value,
                      int low,
                      int high,
                      const char* format = "%d") {
    const bool changed = ui::sliderInt(label, value, low, high, format);
    return context(value, 1, low, high) || changed;
}
inline bool InputFloat(const char* label,
                       float* value,
                       float step = 0,
                       float stepFast = 0,
                       const char* format = "%.3f") {
    const bool changed = ui::inputFloat(label, value, step, stepFast, format);
    return context(value, 1) || changed;
}
inline bool InputInt(const char* label, int* value, int step = 1, int stepFast = 100) {
    const bool changed = ui::inputInt(label, value, step, stepFast);
    return context(value, 1) || changed;
}
inline bool Checkbox(const char* label, bool* value) {
    const bool changed = ui::checkbox(label, value);
    return context(value, 1) || changed;
}
inline bool ColorEdit3(const char* label, float* value) {
    const bool changed = ui::colorEdit3(label, value);
    return context(value, 3, 0.0f, 1.0f) || changed;
}
inline bool Combo(const char* label, int* value, const char* items, int height = -1) {
    (void)height;
    const bool changed = ui::combo(label, value, items);
    int count = 0;
    for (const char* p = items; p && *p; p += std::strlen(p) + 1) ++count;
    return context(value, 1, 0, std::max(0, count - 1)) || changed;
}
inline bool Combo(const char* label,
                  int* value,
                  const char* const* items,
                  int count,
                  int height = -1) {
    (void)height;
    const bool changed = ui::combo(label, value, items, count);
    return context(value, 1, 0, std::max(0, count - 1)) || changed;
}
} // namespace EditorValues
