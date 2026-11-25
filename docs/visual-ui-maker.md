# Canvas — Visual UI Maker

Open **Windows > Visual UI Maker**. The preview uses RmlUi's actual OpenGL renderer,
embedded Roboto font and runtime image loader. It is an editor-only context: preview
controls never call gameplay click listeners. Canvas editing is disabled during Play.

## Create and assign a Canvas

1. Add **Canvas** to a GameObject (previously called UI).
2. Open the maker, choose **New Canvas**, and choose project-relative `.rml` and
   `.rcss` filenames. The initial draft is a screen-space 1280 × 720 Canvas.
3. Drag **button, Image, Text, List, Dropdown, input, or Panel** from the palette
   onto the preview, or click a palette button to add one near the top left.
4. Click an element in the preview or hierarchy. Drag it to position it. Edit
   `left`, `top`, `width`, `height`, colors, font size, ID, classes, image source
   and inner RML in the property panel. Size/position values accept RCSS units.
   Dragging converts position to absolute pixel coordinates relative to its parent.
5. Edit stylesheet rules under **RCSS source**, then **Apply RCSS preview**.
6. **Save RML + RCSS**, then **Assign to selected**. Assign requires a selected
   GameObject with Canvas. The standard Inspector asset fields also work.
7. Press Play and focus Game to interact with the UI using scripts.

Palette elements are children of the Canvas. Loaded documents can retain nested
panels/elements, which are selectable in the hierarchy. This initial editor does
not provide visual reparenting, multi-selection, element undo, or automatic
responsive constraints. Use RML/RCSS for advanced layouts and RmlUi features.
Lists/dropdowns start with example entries; edit their Content RML to change them.
An image needs its `src` set; paths are relative to the RML document.

## Load existing files

Set the RML and RCSS paths and click **Load**, or select an existing Canvas and
choose **Use selected Canvas**. Save/discard unsaved changes before loading a
different document. The document's original head (linked resources, title,
embedded styles) is retained; body edits are serialized by RmlUi. Formatting and
comments inside the body are not preserved. Unsupported RmlUi/custom-element
features should be edited as source; keep a backup before round-tripping them.
RCSS edits are saved as ordinary source, not generated from the visual layout.
Inline element edits override stylesheet rules just as they do at runtime.

The preview Width/Height controls set the authoring resolution, not the runtime
window size. Runtime layout uses the actual Game viewport. Preview changes are
explicitly applied; external file changes require Load. Save and project/Play
preflight preserve or report unsaved drafts, including when the window is closed.

## Compatibility

New scenes call this component **Canvas**, and scripts can use
`self:getComponent(Canvas)` and `global Canvas hud = nil`. Old `UI` script names,
serialized `UI`/`GUI` scene components and old UI reference fields remain readable.
The underlying component slot is unchanged, so duplication/prefabs/rollback keep
working. The immediate-mode `GUI` scripting namespace is unrelated and unchanged.
