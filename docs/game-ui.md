# In-game UI (RmlUi)

LegacyEngine uses **RmlUi 6.1** for optional in-game interfaces, rendered with
OpenGL 3.3 over the Game image. ImGui remains the editor UI. There is no second
windowing backend, browser, JavaScript runtime, or Vulkan renderer.

## Quick start

The runtime entry point is **`Assets/UI/main.rml`** in the current project. If it
does not exist, no RmlUi context/renderer is created and gameplay is unchanged.
Documents/styles are loaded on entering Play, not persisted into the scene, and
unloaded on Stop. Stop/Play reloads edited RML and RCSS (including style caches).
The Game overlay renders even with no Camera, above the empty Game background.
It is not drawn in Scene view or camera previews.

Copy the complete [interactive HUD example](../data/examples/rmlui/README.md):

- [`main.rml`](../data/examples/rmlui/main.rml) → `Assets/UI/main.rml`
- [`hud.rcss`](../data/examples/rmlui/hud.rcss) → `Assets/UI/hud.rcss`
- [`hud.lua`](../data/examples/rmlui/hud.lua) → any location in Assets; attach it to
  **one** object's LegacyScript component.

Press Play, focus Game and unlock your player cursor. The example includes health,
supplies, two clickable buttons and a text-input focus demonstration. No extra
font assets are needed: `font-family: Roboto` uses the engine's embedded font.

## Markup, styles and assets

RML and RCSS are RmlUi's HTML/CSS-like formats, **not** a web browser. External
RCSS references and images resolve relative to their document/style sheet.
The example uses RmlUi `dp` units so its sizes follow display density. The GL3
backend supports RmlUi clipping, transforms, layers and filters. Images
use the existing engine image decoder, including PNG/JPEG/BMP/TGA; color data is
converted to premultiplied alpha before upload. Per-image limits are 64 MiB encoded
and 8192 pixels per dimension. SVG/Lottie plugins and arbitrary downloadable web
fonts are not enabled. Use the upstream RmlUi documentation for supported markup
and RCSS properties: <https://github.com/mikke89/RmlUi>.

Use `pointer-events: none` on a full-screen HUD body and `pointer-events: auto` on
interactive panels to allow clicks on empty screen space to reach the game. Cursor
coordinates are mapped from the embedded image (or separate Game window) into
framebuffer pixels. Pointer input is not sent to the UI when gameplay locks the
cursor. UI elements consume mouse input over their interactive area, including
blocking world object hover/picking there. Release events still reach gameplay
to prevent held mouse buttons getting stuck. Text/select input focus suspends
gameplay keyboard polling; clicking empty game space or leaving Game clears focus.

## Lua bridge

These functions are available to ordinary attached LegacyScript scripts; no
RmlUi Lua plugin or inline RML event scripts are required:

```lua
function onUpdate(dt)
    if not ui.isLoaded() then return end
    if ui.wasClicked("buy") then
        ui.setText("message", "Purchased!")
    end
end
```

- `ui.isLoaded()` → boolean. The entry document has loaded in the runtime context.
  It may not be ready during the first `onStart`; check it in `onUpdate`.
- `ui.setText(id, plainText)` → boolean success. Finds an element by id in the entry
  document and replaces its content with **escaped plain text**, not executable
  markup. Returns false when the document or id is missing.
- `ui.wasClicked(id)` → boolean. Consumes a pending click for that id. Nested
  label clicks bubble to the nearest ancestor with an id. Repeated clicks before
  polling coalesce. One script should own each control. Pending ids are bounded
  to 256 and reset when the document is unloaded.

This initial bridge intentionally does not expose a full DOM, binding arbitrary
Lua callbacks inside RML, or editing RML in the Script Editor. Edit `.rml`/`.rcss`
with an external editor. Text input is interactive but its value is not exposed
through this minimal bridge yet. Missing/invalid documents log an error and do
not prevent the world from playing; correct the file and restart Play.

## Build and manual verification

CMake fetches pinned RmlUi and FreeType sources and links them statically, including
on MinGW-w64. Examples/tests/Lua bindings are disabled. The engine uses its existing
GL loader and SDL3 window; RmlUi does not create a graphics context. A small
build-time adaptation directs upstream GL3's final composition to the offscreen
Game framebuffer rather than framebuffer zero. Original upstream license headers
are preserved in that generated backend source. The pinned RmlUi hash-map header
also receives a missing `<cstdint>` include at configure time, so MinGW builds do
not depend on transitive integer definitions or RmlUi's precompiled header. This
fixes the misleading `robin_hood.h: Unsupported bitness` error and subsequent
missing `uint64_t`/`UINT64_C` errors; no pointer-width override is needed. Re-run
CMake configuration before rebuilding an existing build directory.

Manual checks (no automated tests):

1. Play a project with no `UI/main.rml`: existing rendering/input must be unchanged.
2. Run the example; collect, take damage and heal. Verify numeric/status updates.
3. Type H/W/Space into the callsign field: no keyboard gameplay actions. Click
   outside the panel and verify gameplay keys work again.
4. Resize/dock Game, try display scaling, then separate Game and fullscreen. Check
   click alignment, alpha, clipping, input focus, and no UI in Scene view.
5. Lock cursor with an FPS/TPS script; UI should stop taking pointer input.
6. Release a gameplay-held mouse button over the HUD; it must not stay held.
7. Stop/Play, switch projects and close the separate Game window. Confirm clean
   UI lifetime, no stale clicks, no runtime world edits saved, and reloaded styles.
8. Break/restore the RML path and restart Play; inspect logs and verify recovery.
9. Check a PNG with transparent edges over the 3D image and a scrollable/clipped
   panel. Verify editor UI remains unaffected after drawing the game overlay.
