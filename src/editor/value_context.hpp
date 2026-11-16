#pragma once
#include "project/json_io.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <limits>
#include <type_traits>
namespace EditorValues {
template <class T> inline bool context(T* values, int count, T low = T{}, T high = T{}) {
    bool changed = false;
    if (ImGui::BeginPopupContextItem()) {
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
    bool c = ImGui::ColorEdit3(l, v, flags | ImGuiColorEditFlags_NoOptions);
    return context(v,
                   3,
                   0.f,
                   (flags & ImGuiColorEditFlags_HDR) ? std::numeric_limits<float>::max() : 1.f) ||
           c;
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
