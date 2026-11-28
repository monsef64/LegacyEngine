#pragma once
#include "project/json_io.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <imgui.h>
#include <limits>
#include <type_traits>
namespace EditorValues {
inline const unsigned char* defaultObject = nullptr;
inline const unsigned char* editedObject = nullptr;
inline std::size_t defaultObjectSize = 0;
inline void setDefaultObject(const void* edited, const void* defaults, std::size_t size) {
    editedObject = static_cast<const unsigned char*>(edited);
    defaultObject = static_cast<const unsigned char*>(defaults);
    defaultObjectSize = size;
}
template <class T> inline void bindDefaults(T& edited) {
    static const T defaults{};
    setDefaultObject(&edited, &defaults, sizeof(T));
}
template <class T> inline T defaultValue(const T* value, T fallback = {}) {
    const auto address = reinterpret_cast<std::uintptr_t>(value),
               base = reinterpret_cast<std::uintptr_t>(editedObject);
    if (defaultObject && address >= base && address - base + sizeof(T) <= defaultObjectSize) {
        std::memcpy(&fallback, defaultObject + address - base, sizeof(T));
    }
    return fallback;
}
template <class T>
inline bool
context(T* values, int count, T low = T{}, T high = T{}, const char* popupID = nullptr) {
    bool changed = false;
    if (ImGui::BeginPopupContextItem(popupID)) {
        if (ImGui::MenuItem("Copy")) {
            Json j;
            if (count == 1) {
                j = values[0];
            } else {
                j = Json::array();
                for (int i = 0; i < count; ++i) {
                    j.push_back(values[i]);
                }
            }
            auto text = j.dump();
            ImGui::SetClipboardText(text.c_str());
        }
        if (ImGui::MenuItem("Reset value")) {
            for (int i = 0; i < count; ++i) {
                T reset = defaultValue(&values[i]);
                if constexpr (!std::is_same_v<T, bool>) {
                    if (low < high) {
                        reset = std::clamp(reset, low, high);
                    }
                }
                changed |= values[i] != reset;
                values[i] = reset;
            }
        }
        std::array<T, 4> candidate{};
        bool valid = false;
        try {
            const char* text = ImGui::GetClipboardText();
            if (text && std::char_traits<char>::length(text) <= 1024) {
                auto j = Json::parse(text);
                valid = count == 1 ? !j.is_array() : j.is_array() && int(j.size()) == count;
                if (valid) {
                    for (int i = 0; i < count; ++i) {
                        const auto& v = count == 1 ? j : j[i];
                        if constexpr (std::is_same_v<T, bool>) {
                            if (!v.is_boolean()) {
                                valid = false;
                                break;
                            }
                            candidate[i] = v.get<bool>();
                        } else {
                            if (!v.is_number()) {
                                valid = false;
                                break;
                            }
                            double n = v.get<double>();
                            if (!std::isfinite(n) || n < std::numeric_limits<T>::lowest() ||
                                n > std::numeric_limits<T>::max() ||
                                (std::is_integral_v<T> && std::floor(n) != n)) {
                                valid = false;
                                break;
                            }
                            candidate[i] = T(n);
                            if (low < high) {
                                candidate[i] = std::clamp(candidate[i], low, high);
                            }
                        }
                    }
                }
            }
        } catch (const std::exception&) {
        }
        if (ImGui::MenuItem("Paste", nullptr, false, valid)) {
            for (int i = 0; i < count; ++i) {
                changed |= values[i] != candidate[i];
                values[i] = candidate[i];
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}
inline bool textContext(char* value, std::size_t capacity) {
    bool changed = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Copy")) {
            auto text = Json(std::string(value)).dump();
            ImGui::SetClipboardText(text.c_str());
        }
        if (ImGui::MenuItem("Reset value")) {
            changed = value[0] != '\0';
            value[0] = '\0';
        }
        std::string candidate;
        bool valid = false;
        try {
            const char* text = ImGui::GetClipboardText();
            if (text && std::strlen(text) <= 65536) {
                auto j = Json::parse(text);
                if (j.is_string()) {
                    candidate = j.get<std::string>();
                    valid =
                        candidate.size() < capacity && candidate.find('\0') == std::string::npos;
                }
            }
        } catch (const std::exception&) {
        }
        if (ImGui::MenuItem("Paste", nullptr, false, valid)) {
            changed = candidate != value;
            std::memcpy(value, candidate.c_str(), candidate.size() + 1);
        }
        ImGui::EndPopup();
    }
    return changed;
}
template <class... Args>
inline bool InputText(const char* label, char* value, std::size_t capacity, Args... args) {
    bool changed = ImGui::InputText(label, value, capacity, args...);
    return textContext(value, capacity) || changed;
}
inline bool DragFloat(const char* l,
                      float* v,
                      float s = 1,
                      float lo = 0,
                      float hi = 0,
                      const char* f = "%.3f",
                      ImGuiSliderFlags flags = 0) {
    bool c = ImGui::DragFloat(l, v, s, lo, hi, f, flags);
    return context(v, 1, lo, hi) || c;
}
inline bool DragFloat2(const char* l,
                       float* v,
                       float s = 1,
                       float lo = 0,
                       float hi = 0,
                       const char* f = "%.3f",
                       ImGuiSliderFlags flags = 0) {
    bool c = ImGui::DragFloat2(l, v, s, lo, hi, f, flags);
    return context(v, 2, lo, hi) || c;
}
inline bool DragFloat4(const char* l,
                       float* v,
                       float s = 1,
                       float lo = 0,
                       float hi = 0,
                       const char* f = "%.3f",
                       ImGuiSliderFlags flags = 0) {
    bool c = ImGui::DragFloat4(l, v, s, lo, hi, f, flags);
    return context(v, 4, lo, hi) || c;
}
inline bool SliderFloat(const char* l,
                        float* v,
                        float lo,
                        float hi,
                        const char* f = "%.3f",
                        ImGuiSliderFlags flags = 0) {
    bool c = ImGui::SliderFloat(l, v, lo, hi, f, flags);
    return context(v, 1, lo, hi) || c;
}
inline bool
SliderInt(const char* l, int* v, int lo, int hi, const char* f = "%d", ImGuiSliderFlags flags = 0) {
    bool c = ImGui::SliderInt(l, v, lo, hi, f, flags);
    return context(v, 1, lo, hi) || c;
}
inline bool InputFloat(const char* l,
                       float* v,
                       float step = 0,
                       float fast = 0,
                       const char* f = "%.3f",
                       ImGuiInputTextFlags flags = 0) {
    bool c = ImGui::InputFloat(l, v, step, fast, f, flags);
    return context(v, 1) || c;
}
inline bool
InputInt(const char* l, int* v, int step = 1, int fast = 100, ImGuiInputTextFlags flags = 0) {
    bool c = ImGui::InputInt(l, v, step, fast, flags);
    return context(v, 1) || c;
}
inline bool Checkbox(const char* l, bool* v) {
    bool c = ImGui::Checkbox(l, v);
    return context(v, 1) || c;
}
inline bool ColorEdit3(const char* l, float* v, ImGuiColorEditFlags flags = 0) {
    ImGui::PushID(l);
    if (l[0] != '#' || l[1] != '#') {
        ImGui::TextUnformatted(l);
    }
    // Labels no longer compete with channel inputs for a cached/default width.
    // Very narrow panels expose channel editing through the color popup instead.
    if (ImGui::GetContentRegionAvail().x < ImGui::GetFrameHeight() * 9.f) {
        flags |= ImGuiColorEditFlags_NoInputs;
    }
    ImGui::SetNextItemWidth(-1.f);
    bool changed = ImGui::ColorEdit3(
        "##value", v, flags | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoOptions);
    changed |= context(v,
                       3,
                       0.f,
                       (flags & ImGuiColorEditFlags_HDR) ? std::numeric_limits<float>::max() : 1.f,
                       "WholeColor");
    ImGui::PopID();
    return changed;
}
inline bool ColorEdit4(const char* l, float* v, ImGuiColorEditFlags flags = 0) {
    ImGui::PushID(l);
    if (l[0] != '#' || l[1] != '#') {
        ImGui::TextUnformatted(l);
    }
    // Labels no longer compete with channel inputs for a cached/default width.
    // Very narrow panels expose channel editing through the color popup instead.
    if (ImGui::GetContentRegionAvail().x < ImGui::GetFrameHeight() * 10.f) {
        flags |= ImGuiColorEditFlags_NoInputs;
    }
    ImGui::SetNextItemWidth(-1.f);
    bool changed = ImGui::ColorEdit4(
        "##value", v, flags | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoOptions);
    changed |= context(v,
                       4,
                       0.f,
                       (flags & ImGuiColorEditFlags_HDR) ? std::numeric_limits<float>::max() : 1.f,
                       "WholeColor");
    ImGui::PopID();
    return changed;
}
inline bool Combo(const char* l, int* v, const char* items, int height = -1) {
    bool c = ImGui::Combo(l, v, items, height);
    int count = 0;
    for (const char* p = items; *p; p += std::strlen(p) + 1) {
        ++count;
    }
    return context(v, 1, 0, std::max(0, count - 1)) || c;
}
inline bool Combo(const char* l, int* v, const char* const* items, int count, int height = -1) {
    bool c = ImGui::Combo(l, v, items, count, height);
    return context(v, 1, 0, std::max(0, count - 1)) || c;
}
} // namespace EditorValues
