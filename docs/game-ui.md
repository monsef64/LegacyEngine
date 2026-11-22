# In-game UI and GUI

LegacyEngine uses **RmlUi 6.1** for in-game interfaces rendered with OpenGL 3.3.
ImGui provides the editor and the separate scripted GUI system. Each GameObject's **UI** component owns a separate
runtime document. Nothing is loaded automatically from an `UI` folder.

## Setup

1. Import your `.rml` and `.rcss` files anywhere inside the project's Assets.
2. Select a GameObject and choose **Add Component > UI**.
3. Assign **RML asset** and **RCSS asset** in its Inspector. **Both are required.**
   Keep **Enabled** checked and the owner/its parents active.
4. Press Play and focus Game. Unlock the player cursor to click controls.

UI is a screen-space Game overlay, independent of the owner's Transform. No
Camera is required. Multiple UI components can display simultaneously; ids are
scoped to each document, so separate UI components may use the same element ids. Documents
are shown in creation order, with newly shown documents in front. There is no
explicit layer-order control yet. Scene view and camera previews do not show UI.

Configuration is saved with projects, levels and prefabs and supports component
Reset/Remove, duplicate/copy, undo and exported script references. Document state
is runtime-only. Stop unloads documents; Play reloads their original content.
Disabling/removing UI, deactivating its owner/ancestor, or clearing either asset
unloads that document. Re-enabling creates a fresh document. Changing an assigned
asset path reloads that UI on the next runtime synchronization.

## Script access

Get the component on the attached object:

```lua
local gui
function onStart()
    gui = self:getComponent(UI)
    if not gui then return end
    gui:onClick("buy", function()
        gui:setText("message", "Purchased!")
end
```

Or expose an Inspector slot and drag a UI component (or an object with UI)
onto it. The script and UI can be on different objects:

```lua
@export HUD UI

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
  this UI document; no markup injection. False when the document/id is missing.
- `gui:onClick(id, callback)` → integer subscription token. **Register once**,
  usually in `onStart`, not on every update. The callback receives no arguments.
  It runs in the normal budgeted event dispatcher, not inside rendering/SDL.
  Cancel with `off(token)`. Script shutdown removes its subscriptions; callbacks
  are skipped while the UI/owner is disabled or missing. Callback errors use
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
forms refer to this UI's document, not any other UI with the same id.

**Migration:** the global `ui` table and `wasClicked` name have been removed.
Replace `ui.setText(...)` with `gui:setText(...)` and `ui.wasClicked(id)` with
`gui:onClick(id)` or register an `onClick` callback. Existing files at
`Assets/UI/main.rml` are not automatically loaded: add UI and assign both assets.

## Included interactive example

See [`data/examples/rmlui/README.md`](../data/examples/rmlui/README.md).
Import [`main.rml`](../data/examples/rmlui/main.rml),
[`hud.rcss`](../data/examples/rmlui/hud.rcss), and
[`hud.lua`](../data/examples/rmlui/hud.lua) into any Assets subfolder.
Add UI, assign those RML/RCSS assets, and attach the script to that same object,
or assign the exported **HUD** slot to a different object's UI.

Collect supplies, press H to take damage, then heal. The callsign field demonstrates
text-input focus, but reading its value through Lua is not implemented yet.

## Styles, assets and input

RML/RCSS are HTML/CSS-like formats, **not a browser**: no JavaScript, network
fetch, full browser CSS, or full DOM Lua binding. The component's assigned RCSS is
applied in addition to document styles, with assigned styles taking precedence.
An RML `<link>` is not required. References inside the document and stylesheet
remain relative to their own source files. Both assigned assets must exist, have
the correct extensions and be at most 4 MiB each. Invalid assets log a UI error
without stopping gameplay. Correct them and Stop/Play to retry loading.

`font-family: Roboto` uses the engine's embedded font. The example uses RmlUi `dp`
units for display-density-aware sizing. PNG/JPEG/BMP/TGA images use the existing
engine decoder and premultiplied alpha; per-image limits are 64 MiB encoded and
8192 pixels per dimension. SVG/Lottie plugins are disabled.

For a HUD that does not block the whole Game image, use `pointer-events: none` on
its body and `pointer-events: auto` on interactive panels. Pointer positions are
mapped into Game framebuffer pixels, including the separate/fullscreen Game
window. Locked gameplay cursors bypass UI pointer handling. Interactive UI
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

1. Leave an old `Assets/UI/main.rml` in a scene without UI: nothing should load.
2. Add UI; leave either slot empty, then assign both from unrelated Assets
   folders. Confirm it displays only when both are assigned and enabled.
3. Run the example; verify callback text updates, H damage, supply collection and
   healing. Assign its exported HUD to another object's UI and repeat.
4. Display two GUIs using the same ids and verify each script changes only its
   assigned document. Disable/remove/deactivate one and confirm the other remains.
5. Change paths, Stop/Play, save/reopen, duplicate, prefab-spawn and undo/redo.
   Ensure UI configuration survives but runtime text/state is never saved.
6. Drag the UI header and its GameObject into a UI export slot. Check wrong
   component types are rejected. Rename/move the assigned asset folder and check
   live and saved level/prefab component paths follow the rename.
7. Resize/dock Game, test HiDPI, separate/fullscreen Game, text-field focus,
   transparent PNG edges, clipping, locked cursors and release outside the panel.
8. Remove a UI with a subscribed callback, then click elsewhere. Confirm no stale
   callbacks/crashes. Use `off(token)` and verify the callback stops firing.

## GUI: scripted Dear ImGui

**UI** is the RML/RCSS component described above. **GUI** is a separate Lua
namespace, not a component or export-reference type. It needs no RML/RCSS assets.
Attach a LegacyScript to an active GameObject and define `onGUI()`:

```lua
local supplies = 0
function onGUI()
    if GUI.beginWindow("Inventory") then
        GUI.text("Supplies: " .. supplies)
        if GUI.button("Collect") then supplies = supplies + 1 end
    end
    GUI.endWindow()
end
```

`data/examples/imgui/hud.lua` includes text input, a slider and checkbox.
`onGUI()` is called once per rendered Game frame during unpaused Play. GUI
functions are valid only inside this callback, not in `onStart`/`onUpdate` or
event listeners. `GUI.onDraw` registration has been removed: move the drawing
body directly into `function onGUI() ... end`. `wait` and `task` are not supported
here; use state flags and handle longer work in `onUpdate`. Local variables
persist between frames. The normal script instruction budget protects drawing.

Widgets are generated every frame. Store values in Lua locals. Inputs return
`newValue, changed`; colors return their channels followed by `changed`. Buttons
and selectable items return activation as a boolean. Use dot syntax:
`volume = GUI.sliderFloat("Volume", volume, 0, 1)`.

Rendering uses an **independent ImGui context** composited into the Game surface,
including the separate Game window, never editor windows. Coordinates are Game
surface pixels. Keep Game focused and the cursor unlocked. Keyboard/text and
mouse capture suppress corresponding gameplay input. Avoid overlapping RML UI
and ImGui GUI controls; GUI is drawn on top. Draw callbacks run only during
unpaused Play while Game is rendered. Pause hides GUI; Stop clears its context.
Inactive script owners do not draw. Each callback gets its own automatically recovered GUI scope.

### Supported GUI functions

- Lifecycle: `onGUI()` (global or GameObject callback), no registration needed.
- Windows/layout: `beginWindow`, `endWindow`, `beginChild`, `endChild`,
  `beginGroup`, `endGroup`, `beginDisabled`, `endDisabled`, `pushID`, `popID`,
  `setNextWindowPos`, `setNextWindowSize`, `setCursorPos`, `setNextItemWidth`,
  `getWindowSize`, `getContentRegionAvail`, `getDisplaySize`, `sameLine`,
  `newLine`, `spacing`, `separator`.
- Text/actions: `text`, `textWrapped`, `bulletText`, `button`, `smallButton`,
  `selectable`, `collapsingHeader`, `progressBar`, `setTooltip`.
- Inputs: `checkbox`, `sliderFloat`, `sliderInt`, `dragFloat`, `inputFloat`,
  `inputInt`, `inputText`, `colorEdit3`, `colorEdit4`.
- Last-item queries: `isItemHovered`, `isItemActive`.

See the [scripting reference](scripting-reference.md#gui) for signatures/examples.
Advanced bindings also expose:
- Images: `image`, `imageButton` with project-relative image paths.
- Tables: `beginTable`, `endTable`, row/column navigation, column setup and headers.
- Menus/popups: menu bars, menus/items, regular/modal popups and combos.
- Selection: `combo` (one-based index) and `radioButton`.
- Scoped styling: style colors/variables and font size, with matching pops.
- Typed script-only drag/drop payloads, separate from editor asset/object drags.
- Custom draw lines, rectangles, circles, triangles and text in Game-surface pixels.
- Mouse position/button and named-key queries.

`GUI.WindowFlags`, `GUI.TableFlags`, `GUI.Col` and `GUI.StyleVar` contain supported
constants. Flags combine with `|`; menu bars require `GUI.WindowFlags.MenuBar` on
`beginWindow`. Table columns are zero-based. Only end table/menu/popup/combo/drag
scopes when their begin returns true. Pop styles/fonts in reverse push order.
See `data/examples/imgui/inventory.lua` for a complete advanced example.
This remains a bounded binding, **not the entire Dear ImGui C++ API**:
raw texture handles and multi-viewport platform windows are not exposed.

Always balance window/child/group/disabled/ID scopes. `beginWindow` and `beginChild`
require matching ends even when returning false. Errors automatically unwind
open scopes before another callback runs. Window identities are isolated per
script/object. Limits: 4096 API calls per callback, 32 nested scopes, 256-byte
labels, 4096-byte text (4095 for input content), and finite numbers within
+/-10000000. Embedded NULs and raw ImGui/GL handles are not allowed.

### Migrating the previous GUI component

Saved RML/RCSS **GUI** components and exported reference fields load as **UI**;
new JSON saves use `UI` and format version 26. Binary component identity is
unchanged. Update scripts explicitly: `getComponent(GUI)` / `@export HUD GUI`
become `getComponent(UI)` / `@export HUD UI`. Source files are not rewritten
automatically because `GUI` now names the ImGui namespace.

### Manual GUI verification

1. Load a pre-rename scene with an RML GUI and assigned script reference. Confirm
   it appears as UI, preserves assets/references, and saves/reopens correctly.
2. Change its script token to UI; verify scoped text updates and clicks.
3. Run the ImGui example, interact with every control and resize Game. Confirm
   editor styling, windows and input remain independent.
4. Repeat in separate Game/fullscreen, with focus changes and cursor locking.
5. Attach two instances with identical labels; confirm independent window state.
6. Deactivate/remove owners, cancel `off(token)`, Pause/Stop/Play and switch levels.
   Verify old subscriptions disappear and gameplay input never gets stuck.
7. Raise an error inside an open window/child/disabled scope. Other scripts and
   the editor must remain usable without ImGui assertions.
