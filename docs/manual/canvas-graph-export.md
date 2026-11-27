# Manual checks: graph controls and export compression

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
- Save/reimport mesh, model and terrain assets: new editor files are ordinary JSON,
  not LEMZ/TERZ streams. Open existing zlib/Zstandard assets without migration.
- Export using an updated native player. Package manifest should be LegacyGame2
  with compression=zstd. Run, load levels and check GUI/images/models/terrain.
  Include empty files, incompressible PNG data and assets crossing chunk boundaries.
  Old LegacyGame1 packages should still mount using the updated runtime. Corrupt,
  truncated, authenticated-but-oversized frames must fail without oversized allocation.
- Compare project load duration on the same representative scene before/after;
  loading avoids JSON dump/reparse and redundant component-pool deep copies.
