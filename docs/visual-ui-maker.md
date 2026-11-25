# Canvas — Visual UI Editor

Open **Windows > Canvas - Visual UI Editor**. The preview uses RmlUi's actual OpenGL renderer,
embedded Roboto font and runtime image loader. It is an editor-only context: preview
controls never call gameplay click listeners. Canvas editing is disabled during Play.

## Create and assign a Canvas

1. Add **Canvas** to a GameObject (previously called UI).
2. Open the editor, choose **New Canvas**, and choose project-relative `.rml` and
   `.rcss` filenames. The initial draft is a screen-space 1280 × 720 Canvas.
3. Drag **Button, Image, Text, List, Dropdown, Input, or Panel** from the palette
   onto the preview, or click a palette button to add one near the top left.
4. Select an element in the preview or hierarchy. Drag its body to position it,
   or drag any of its **eight rectangle handles** to resize it. X/Y are numeric
   pixel inputs. Width, Height and Font Size are sliders; **double-click** a slider
   to type a number (no `px` suffix). Text and Background Color use the standard
   color picker with alpha. ID, classes and image source remain editable.
5. Edit stylesheet rules under **RCSS source**, then **Apply RCSS preview**.
6. **Save RML + RCSS**, then **Assign to selected**. Assign requires a selected
   GameObject with Canvas. The standard Inspector asset fields also work.
7. Press Play and focus Game to interact with the UI using scripts.

## Parents, children and text

Palette clicks and preview drops create Canvas children. Drag a palette element
onto a hierarchy row to create it as that container's child. Right-click a row for
**Create Child**, **Create Parent (Panel)**, **Move to Canvas**, and sibling
**Move Up/Down** actions. Drag existing hierarchy rows onto containers to reparent
them. Self-parenting and cycles are rejected. Reparenting preserves the element's
screen position and resolves its content size to pixels in its new parent.
Reparenting/wrapping is an explicit layout edit; percentage sizing becomes numeric.
Images, inputs, dropdowns and options are not general-purpose containers.

Leaf elements have an escaped plain **Text** editor; input/dropdown controls have
an editable **Value**. For nested text, select the text-bearing child. Under
**Advanced Properties / Content RML**, edit markup and additional RCSS properties.
Replacing Content RML replaces the child hierarchy, so save first if experimenting.
Lists/dropdowns start with example entries; edit their child text/options or markup.
An image needs its `src` set; image paths are relative to the RML document.

## Fonts from Assets

Use the **Font** asset picker to choose `.ttf` or `.otf` files inside Assets.
The override is stored in the element's `data-canvas-font` attribute as a
project-relative path. Editor preview and runtime load the same font and apply
its generated family name, and export follows the font reference into the package.
Clearing the picker removes the override and inherits the parent's font (Roboto
by default). Font size remains independent of font selection. Loaded font data
is retained until shutdown, as required by RmlUi: at most 64 fonts / 64 MiB total,
with a 16 MiB per-file limit. External font changes take effect on preview reload
or the next Play load. Invalid/missing fonts are reported rather than silently
substituted. Only use fonts licensed for redistribution when exporting.

Multi-selection, element undo and automatic responsive constraints remain outside
this editor's current scope. RCSS/source editing is available for advanced layouts.

## Load existing files

Set the RML and RCSS paths and click **Load**, or select an existing Canvas and
choose **Use selected Canvas**. Save/discard unsaved changes before loading a
different document. The document's original head (linked resources, title,
embedded styles) is retained; body edits are serialized by RmlUi. Formatting and
comments inside the body are not preserved. Unsupported RmlUi/custom-element
features should be edited as source; keep a backup before round-tripping them.
RCSS edits are saved as ordinary source, not generated from the visual layout.
Inline element edits override stylesheet rules just as they do at runtime.

The Canvas Width/Height sliders set the authoring resolution, not the runtime
window size. Runtime layout uses the actual Game viewport. Preview changes are
explicitly applied; external file changes require Load. Save and project/Play
preflight preserve or report unsaved drafts, including when the window is closed.

## Compatibility

New scenes call this component **Canvas**, and scripts can use
`self:getComponent(Canvas)` and `global Canvas hud = nil`. Old `UI` script names,
serialized `UI`/`GUI` scene components and old UI reference fields remain readable.
The underlying component slot is unchanged, so duplication/prefabs/rollback keep
working. The immediate-mode `GUI` scripting namespace is unrelated and unchanged.
