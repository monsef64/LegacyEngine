# In-game GUI (Dear ImGui)

The `GUI` Lua namespace draws interfaces inside per-frame `onGUI()` callbacks.
It is not a component and does not require document or stylesheet assets.

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
mouse capture suppress corresponding gameplay input. Draw callbacks run only during
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

## Removed document UI

The RML/RCSS runtime, Canvas visual editor and Canvas/UI component have been
removed, together with their external rendering/font dependencies. Older scene
component records and exported references are consumed and discarded during load;
new saves do not contain them. Existing RML/RCSS project files are not deleted,
but are no longer loaded or rendered. Scripts using Canvas/UI tokens or document
methods must be rewritten using the GUI namespace; source files are not rewritten.

## Manual verification

- Run the ImGui example in embedded Game, separate Game and fullscreen.
- Check widgets, input capture, cursor locking, Pause/Stop and owner destruction.
- Open older scenes containing the removed component, save and reopen. Other
  components, prefabs and levels must survive; the removed UI must not reappear.
- Build editor/player from a clean directory without the removed dependencies.
