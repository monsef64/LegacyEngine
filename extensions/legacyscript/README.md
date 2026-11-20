# LegacyScript for VS Code

A bundled Node LSP server for LegacyEngine's Lua-based `.lua` scripts (legacy `.ls` files are also accepted). No separate
Lua installation, Lua extension or online service is needed. Never executes scripts.

## Install

With Node 20+:

```sh
cd extensions/legacyscript
npm ci
npm test
npm run package
code --install-extension legacyscript.vsix
```

Alternatively use **Extensions → … → Install from VSIX** in VS Code.
Open a `.lua` file and check that its language is **LegacyScript**. Remove user
`*.lua: lua` file associations if they override it. For engine scripts, select **LegacyScript** as the language mode if another Lua extension takes precedence. Both `.lua` and legacy `.ls` are associated with this extension.

## Features

- LSP completion (Ctrl+Space), including `self:`, `Camera.` and `GameObject.`
- Hover API help and call signature hints (Ctrl+Shift+Space)
- Document-local name suggestions and function outline
- Live syntax errors and export-declaration diagnostics in Problems and editor squiggles
- Lua syntax highlighting, comments and bracket matching
- Lifecycle snippets: `onStart`, `onUpdate`, `onFixedUpdate`, `onDestroy`
- `cameraFov` snippet sets FOV on the owning Camera

`self` is the GameObject owning the attachment; Camera methods require a Camera
component. Select an owner in LegacyEngine when executing a shared asset.

## Limits

This is a lightweight engine API LSP, not a full Lua type checker. Object receivers
suggest both GameObject and Camera methods without type inference. Symbol and
signature discovery is lexical and may be limited by comments, strings or complex
expressions. No go-to-definition, rename, workspace indexing or debugger yet. Standard library support is limited to basic global suggestions.
Not published on the Marketplace.

## Development

`npm run generate` refreshes checked-in catalogs from engine definitions in the
full repository. Run it after API changes, then test and package.

## Download from GitHub Actions

The **LegacyScript VSIX** workflow builds automatically on pushes and
pull requests affecting the extension or engine API definitions. You can also
run it manually via **Actions → LegacyScript VSIX → Run workflow** once the
workflow is available on the default branch.

Open a successful run and download **legacyscript-vsix** under **Artifacts**
(GitHub sign-in may be required). Extract the ZIP and install `legacyscript.vsix`
using **Extensions → … → Install from VSIX…** in VS Code.
Artifacts are retained for 30 days; rerun the workflow if a download has expired.
This workflow does not publish to the VS Code Marketplace.

## Export declarations

Use `@export target GameObject` or `@export speed number 5` at the beginning
of a line to expose an Inspector field. The engine preprocesses this LegacyScript
syntax before executing Lua; external plain-Lua runtimes cannot execute it as-is.
Legacy `---@export` comments remain supported. The `export` snippet inserts the
preferred bare form, and both forms supply completion and hover information.


## Error diagnostics

Version 0.1.1 validates open `.lua` / `.ls` documents on open/save and shortly after edits.
Errors appear inline and in **View → Problems** with source **LegacyScript**;
fixed errors disappear automatically, and closed files clear their diagnostics.
Syntax checking never executes scripts. It understands engine `@export` lines and
reports malformed/duplicate declarations and unknown export types. Lua 5.4 local
`<const>` / `<close>` annotations are accepted on top of the bundled Lua 5.3 syntax
grammar, but attribute semantics are not checked. Validation pauses for documents
over 2 MiB and shows a warning rather than freezing the extension host.

Runtime-only failures (missing scene objects/components, dynamic values and engine
callbacks) still appear in LegacyEngine's script console; this extension is not a
full Lua type checker or debugger. It reports the first Lua syntax error per pass,
plus export-declaration errors. Correcting that error may reveal the next one.

Internal language-service failures now produce a visible notification (rate-limited)
and details in **Output → LegacyScript** instead of only silently logging. If no
errors appear, verify the file language is **LegacyScript**, install the updated
VSIX and reload VS Code. Existing Lua extensions/file associations can override `.lua` / `.ls`.
