# VS Code extension
See [LegacyScript extension documentation](../extensions/legacyscript/README.md).
Its bundled LSP provides API/export completion, hover/signature help, highlighting,
function outlines and snippets. It is not a full Lua type checker or debugger.

```sh
cd extensions/legacyscript
npm ci
npm test
npm run package
code --install-extension legacyscript.vsix
```

The **engine Release workflow** also tests and builds the extension, publishing
**`legacyscript.vsix`** as a separate downloadable artifact in the same run. Tagged engine
releases attach `legacyscript.vsix` alongside the engine package. Use VS Code's
**Install from VSIX** command with that file. The standalone **LegacyScript VSIX**
workflow remains available (its artifact is a ZIP to extract first). `.ls` files
should show **LegacyScript** as their language. It is not on the Marketplace.
