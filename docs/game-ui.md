# GUI component (RmlUi)

LegacyEngine uses **RmlUi 6.1** for in-game interfaces rendered with OpenGL 3.3.
ImGui remains the editor UI. Each GameObject's **GUI** component owns a separate
runtime document. Nothing is loaded automatically from an `UI` folder.

## Setup

1. Import your `.rml` and `.rcss` files anywhere inside the project's Assets.
2. Select a GameObject and choose **Add Component > GUI**.
3. Assign **RML asset** and **RCSS asset** in its Inspector. **Both are required.**
   Keep **Enabled** checked and the owner/its parents active.
4. Press Play and focus Game. Unlock the player cursor to click controls.

GUI is a screen-space Game overlay, independent of the owner's Transform. No
Camera is required. Multiple GUI components can display simultaneously; ids are
scoped to each document, so separate GUIs may use the same element ids. Documents
are shown in creation order, with newly shown documents in front. There is no
explicit layer-order control yet. Scene view and camera previews do not show GUI.

Configuration is saved with projects, levels and prefabs and supports component
Reset/Remove, duplicate/copy, undo and exported script references. Document state
is runtime-only. Stop unloads documents; Play reloads their original content.
Disabling/removing GUI, deactivating its owner/ancestor, or clearing either asset
unloads that document. Re-enabling creates a fresh document. Changing an assigned
asset path reloads that GUI on the next runtime synchronization.

## Script access

Get the component on the attached object:

```lua
local gui
function onStart()
    gui = self:getComponent(GUI)
    if not gui then return end
    gui:onClick("buy", function()
        gui:setText("message", "Purchased!")
    end)
end
```

Or expose an Inspector slot and drag a GUI component (or an object with GUI)
onto it. The script and GUI can be on different objects:

```lua
@export HUD GUI

function onStart()
    if HUD then
        HUD:onClick("buy", function()
            HUD:setText("message", "Purchased!")
        end)
    end
end
```

Use **colon** calls on the component:

- `gui:isLoaded()` → boolean. Whether this component's document is loaded and
  enabled. It may not yet be loaded in the first `onStart`; callbacks can still
  be registered there. Guard text updates in `onUpdate` when necessary.
- `gui:setText(id, plainText)` → boolean success. Sets **escaped plain text** in
  this GUI document; no markup injection. False when the document/id is missing.
- `gui:onClick(id, callback)` → integer subscription token. **Register once**,
  usually in `onStart`, not on every update. The callback receives no arguments.
  It runs in the normal budgeted event dispatcher, not inside rendering/SDL.
  Cancel with `off(token)`. Script shutdown removes its subscriptions; callbacks
  are skipped while the GUI/owner is disabled or missing. Callback errors use
  the existing event error handling and instruction budget. `wait` and `task` are not
  supported inside these event callbacks; set a flag for `onUpdate` to handle
  deferred work.
- `gui:onClick(id)` → boolean polling form. Consumes a queued click. Repeated
  clicks before polling coalesce. Do not combine polling and callback forms for
  the same gameplay action unless you intentionally want both notifications.
- `gui.enabled` → boolean. Toggle display; `gui.rml` and `gui.rcss` are writable
  project-relative asset paths. Assign complete strings, e.g.
  `gui.rml = "Menus/Pause.rml"` and `gui.rcss = "Styles/Pause.rcss"`.

Element ids for scripting are 1–64 bytes. Nested label clicks bubble to the
nearest element with an id. Pending polling ids are bounded to 256 per document;
callback subscriptions use the existing 128-listener-per-script limit. Both
forms refer to this GUI's document, not any other GUI with the same id.

**Migration:** the global `ui` table and `wasClicked` name have been removed.
Replace `ui.setText(...)` with `gui:setText(...)` and `ui.wasClicked(id)` with
`gui:onClick(id)` or register an `onClick` callback. Existing files at
`Assets/UI/main.rml` are not automatically loaded: add GUI and assign both assets.

## Included interactive example

See [`data/examples/rmlui/README.md`](../data/examples/rmlui/README.md).
Import [`main.rml`](../data/examples/rmlui/main.rml),
[`hud.rcss`](../data/examples/rmlui/hud.rcss), and
[`hud.lua`](../data/examples/rmlui/hud.lua) into any Assets subfolder.
Add GUI, assign those RML/RCSS assets, and attach the script to that same object,
or assign the exported **HUD** slot to a different object's GUI.

Collect supplies, press H to take damage, then heal. The callsign field demonstrates
text-input focus, but reading its value through Lua is not implemented yet.

## Styles, assets and input

RML/RCSS are HTML/CSS-like formats, **not a browser**: no JavaScript, network
fetch, full browser CSS, or full DOM Lua binding. The component's assigned RCSS is
applied in addition to document styles, with assigned styles taking precedence.
An RML `<link>` is not required. References inside the document and stylesheet
remain relative to their own source files. Both assigned assets must exist, have
the correct extensions and be at most 4 MiB each. Invalid assets log a GUI error
without stopping gameplay. Correct them and Stop/Play to retry loading.

`font-family: Roboto` uses the engine's embedded font. The example uses RmlUi `dp`
units for display-density-aware sizing. PNG/JPEG/BMP/TGA images use the existing
engine decoder and premultiplied alpha; per-image limits are 64 MiB encoded and
8192 pixels per dimension. SVG/Lottie plugins are disabled.

For a HUD that does not block the whole Game image, use `pointer-events: none` on
its body and `pointer-events: auto` on interactive panels. Pointer positions are
mapped into Game framebuffer pixels, including the separate/fullscreen Game
window. Locked gameplay cursors bypass GUI pointer handling. Interactive GUI
areas block world picking; release events still reach gameplay to avoid stuck
mouse buttons. Text/select focus suspends gameplay key polling. Click empty Game
space or leave Game to clear focus.

## Build and manual verification

CMake fetches pinned RmlUi/FreeType and links statically, including MinGW-w64.
The existing SDL3 window, OpenGL loader and offscreen Game framebuffer are reused.
The backend's final composition destination is adapted at configure time. The
pinned hash-map header receives its missing `<cstdint>` include to fix recent
MinGW's misleading `Unsupported bitness` error. No pointer-width override is used.
Re-run CMake configuration when upgrading an existing build directory.

Manual verification (no automated tests):

1. Leave an old `Assets/UI/main.rml` in a scene without GUI: nothing should load.
2. Add GUI; leave either slot empty, then assign both from unrelated Assets
   folders. Confirm it displays only when both are assigned and enabled.
3. Run the example; verify callback text updates, H damage, supply collection and
   healing. Assign its exported HUD to another object's GUI and repeat.
4. Display two GUIs using the same ids and verify each script changes only its
   assigned document. Disable/remove/deactivate one and confirm the other remains.
5. Change paths, Stop/Play, save/reopen, duplicate, prefab-spawn and undo/redo.
   Ensure GUI configuration survives but runtime text/state is never saved.
6. Drag the GUI header and its GameObject into a GUI export slot. Check wrong
   component types are rejected. Rename/move the assigned asset folder and check
   live and saved level/prefab component paths follow the rename.
7. Resize/dock Game, test HiDPI, separate/fullscreen Game, text-field focus,
   transparent PNG edges, clipping, locked cursors and release outside the panel.
8. Remove a GUI with a subscribed callback, then click elsewhere. Confirm no stale
   callbacks/crashes. Use `off(token)` and verify the callback stops firing.
