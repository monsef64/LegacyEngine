#pragma once

namespace FilePickerSettings {
// Set by EditorSettings before the startup launcher opens any dialogs.
// Zenity on Linux; the OS-native picker on other platforms.
inline int backend = 0; // 0: Zenity/native, 1: ImGui
} // namespace FilePickerSettings
