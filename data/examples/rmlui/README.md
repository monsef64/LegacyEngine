# Canvas component HUD example

1. Import `main.rml`, `hud.rcss` and `hud.lua` into any folder inside your
   project's Assets. There is **no required UI folder or automatic entry point**.
2. Create/select a GameObject. Choose **Add Component > Canvas** and assign
   `main.rml` to **RML asset** and `hud.rcss` to **RCSS asset**. Both are required.
3. Attach `hud.lua` to that object's **LegacyScript** component. Alternatively,
   attach it to another object and drag the Canvas header (or the Canvas's GameObject)
   onto the script's exported **HUD (Canvas)** slot. Use only one HUD driver.
4. Press Play, focus Game and unlock the mouse if your player script locks it.
   No Camera or mesh is required for the overlay.
5. Click **Collect supply**, press **H**, then **Use supply**. The example uses
   component-scoped `gui:onClick(id, callback)` handlers and `gui:setText` updates.
6. Try the callsign text field; gameplay key polling should stop while editing.
   Click empty Game space to restore it. Lua text-field value access is not yet
   available in this bridge.
7. Stop/Play resets the UI and script. Edit RML/RCSS while stopped and restart
   Play to reload file contents. Runtime text changes are never written to assets.

UI uses its assigned stylesheet, so this example does not need an RML `<link>`.
RML images and RCSS resources still resolve relative to their respective files.
Embedded `font-family: Roboto` needs no external font asset. RmlUi is not a web
browser: no JavaScript or full DOM Lua binding is provided.

API:

- `local gui = self:getComponent(Canvas)` or `global Canvas HUD =`.
- `gui:isLoaded()` returns a boolean (loading may finish after first onStart).
- `gui:setText(id, plainText)` returns success and escapes markup.
- `local token = gui:onClick(id, function() ... end)` registers once; `off(token)`
  cancels it. Never register on every update. Callbacks use the normal bounded
  event dispatcher and do not support `wait`.
- `gui:onClick(id)` is the optional boolean polling form. It replaces
  `wasClicked`; repeated clicks coalesce until polled.
- `gui.enabled`, `gui.rml`, `gui.rcss` configure that UI only.

Each UI has its own document/id scope. Put the same example on two objects to
check independent UI state (overlapping panels will cover one another; move a
panel in a separate stylesheet to see both). The old global `ui` table and
`Assets/UI/main.rml` automatic loading have been removed.

Images use PNG/JPEG/BMP/TGA, including alpha, limited to 8192 pixels per dimension
and 64 MiB per encoded file. See `docs/game-ui.md` for the full setup/API guide.
