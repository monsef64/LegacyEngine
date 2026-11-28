# Removed in-game UI

The scripted in-game ImGui implementation has been removed. Scripts no longer
receive per-frame `onGUI` callbacks, and no `GUI` namespace, widget functions or
GUI constants are installed. Existing scripts that call those APIs must be
updated; script source is not rewritten automatically.

The earlier document-based Canvas/UI implementation was also removed. Old scene
records for that component are consumed and discarded during loading.

Dear ImGui remains in use for the editor, built-in file/folder picker and loading
overlay. These interfaces are not exposed to gameplay scripts. Rendering,
gameplay input, screenshots, lifecycle callbacks, events and task/wait APIs
remain available. No replacement runtime UI system is introduced by this change.
