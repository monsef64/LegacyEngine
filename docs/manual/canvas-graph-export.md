# Manual checks: Canvas, graph controls and export compression

No automated tests were added or run for this change.

- Open an Animator with reciprocal, parallel and self transitions. Confirm routes
  avoid other nodes, separate lanes where space allows, and click selection follows
  the drawn path. Test offscreen/widely spaced nodes and node dragging. Dense or
  non-planar graphs can require crossings; overlapping nodes need rearrangement.
- Wheel zoom at the pointer (25–250%). Pan with middle-drag and Alt+right-drag,
  including over a node. Right-click without Alt should still open context menus.
- Move nodes, edit state fields, parameters and conditions, add/remove states and
  links. Undo/redo with buttons and Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z; dragging should
  be one undo action. These shortcuts must not also undo the level. Save/reload,
  Discard and new/open controller must not carry history from another controller.
- The hierarchy search clear button is inside the field, padded from its right
  edge. Type a long query, clear it, and verify all rows return.
- Create Canvas, add every palette type, move/resize via properties, edit IDs,
  image paths, list/dropdown options, nested text and RCSS. Save/load both files;
  assign to Canvas and compare editor preview with Play and exported player.
- Load nested RML with an existing head, classes, inline properties and linked
  RCSS/images. Check CSS image URLs relative to the RCSS file. Save to a new file
  first; compare the saved structure and visual result. Test malformed/missing
  source, invalid paths, project switching, closing the maker with unsaved work,
  window resizing and Stop restoring the original runtime DOM.
- Open old scenes/prefabs with UI/GUI components and exported UI script fields.
  New scene saves use Canvas. Old UI Lua token and new Canvas token should work.
- Save/reimport mesh, model and terrain assets: new editor files are ordinary JSON,
  not LEMZ/TERZ streams. Open existing zlib/Zstandard assets without migration.
- Export using an updated native player. Package manifest should be LegacyGame2
  with compression=zstd. Run, load levels and check Canvas/images/models/terrain.
  Include empty files, incompressible PNG data and assets crossing chunk boundaries.
  Old LegacyGame1 packages should still mount using the updated runtime. Corrupt,
  truncated, authenticated-but-oversized frames must fail without oversized allocation.
- Compare project load duration on the same representative scene before/after;
  loading avoids JSON dump/reparse and redundant component-pool deep copies.

## Canvas controls, handles, hierarchy and fonts

- Check the window/menu name is Canvas - Visual UI Editor and palette labels all
  start uppercase. X/Y numeric fields and size sliders must not require unit suffixes.
- Change Width/Height and Font Size by slider and by double-click typing. Enter,
  focus changes and selection changes should exit typed mode without affecting
  another element. Check finite/positive size limits and Canvas resolution sliders.
- Select Text/Button/List Item, edit plain text containing <, > and &, save/reload
  and verify it stays literal. Edit input/dropdown Value and verify persistence.
  Nested markup must remain untouched unless Content RML is explicitly edited.
- Test text/background color pickers, transparency, save/reload and Play.
- Drag every corner and edge handle, including past the opposite edge, outside
  the preview, on nested panels with margins/padding, and on a scaled preview.
  Geometry must stay positive and moving a node must not also resize it.
- Create Child, wrap in Create Parent, drag rows to reparent, reject self/cycles,
  move to Canvas and reorder siblings (including loaded formatted RML). Confirm
  the preview, hierarchy, saved markup and Play agree.
- Choose two asset fonts (including a path with spaces), clear one to inherit,
  change font size and reload. Test missing/corrupt/oversized fonts. Verify fonts
  in Play, Stop/Play and an exported player; no reliance on installed system fonts.
- Confirm the Windows near/far macro build fix remains present; UI header changes
  must not regress UCRT64/MSVCRT builds. No automated tests added/run.
