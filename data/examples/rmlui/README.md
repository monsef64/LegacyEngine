# RmlUi in-game HUD example

1. Create/open any LegacyEngine project (even an empty scene).
2. Create `UI` inside that project's Assets directory. Copy `main.rml` and
   `hud.rcss` into it. The runtime entry point is **Assets/UI/main.rml**.
3. Copy `hud.lua` into Assets and attach it to one object's **LegacyScript**.
4. Press Play and focus Game. Unlock the cursor if your player script locks it.
5. Click **Collect supply**, press **H**, then click **Use supply**. The text
   changes through the engine's `ui` Lua API. Try typing into the callsign field;
   keyboard gameplay input is suspended while that field owns focus.
6. Stop and Play again to reset both the document and Lua state. Edit RML/RCSS
   while stopped and restart Play to reload them.

The overlay also works in the separate Game window/fullscreen. It is never part
of the Scene/editor view or saved runtime world state. No external fonts are
needed: the engine registers its embedded Roboto face as `Roboto`.

This is RmlUi markup/CSS, **not a web browser**: there is no JavaScript, DOM Lua
binding, network asset fetch, or full browser CSS compatibility. The small Lua
bridge exposes `ui.isLoaded()`, `ui.setText(id, plainText)` and
`ui.wasClicked(id)`. `setText` escapes markup. `wasClicked` consumes a pending
click, so one script should own each button. Clicks on labels bubble to the
nearest element with an id. Repeated clicks before polling coalesce per id.

Images use the engine decoder (PNG/JPEG/BMP/TGA, including alpha). Paths in
`<img src="..." />` and RCSS are relative to the document/style sheet. Images
are limited to 8192 pixels per dimension and 64 MiB per encoded file.
RML/RCSS and relative asset references are project files; no automatic copying
into a project is performed.
