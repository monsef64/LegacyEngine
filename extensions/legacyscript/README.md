# LegacyScript for VS Code

A bundled Node LSP server for LegacyEngine's Lua-based `.ls` scripts. No separate
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
Open a `.ls` file and check that its language is **LegacyScript**. Remove user
`*.ls: lua` file associations if they override it. `.lua` files remain unchanged.

## Features

- LSP completion (Ctrl+Space), including `self:`, `Camera.` and `GameObject.`
- Hover API help and call signature hints (Ctrl+Shift+Space)
- Document-local name suggestions and function outline
- Lua syntax highlighting, comments and bracket matching
- Lifecycle snippets: `onStart`, `onUpdate`, `onFixedUpdate`, `onDestroy`
- `cameraFov` snippet sets FOV on the owning Camera

`self` is the GameObject owning the attachment; Camera methods require a Camera
component. Select an owner in LegacyEngine when executing a shared asset.

## Limits

This is a lightweight engine API LSP, not a full Lua type checker. Object receivers
suggest both GameObject and Camera methods without type inference. Symbol and
signature discovery is lexical and may be limited by comments, strings or complex
expressions. No syntax diagnostics, go-to-definition, rename, workspace indexing
or debugger yet. Standard library support is limited to basic global suggestions.
Not published on the Marketplace.

## Development

`npm run generate` refreshes checked-in catalogs from engine definitions in the
full repository. Run it after API changes, then test and package.
