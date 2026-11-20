# LegacyScript for Sublime Text 4

Syntax highlighting for `.ls`, API/member completions, API hover help, and line comments. Uses the engine's definitions and the same TextMate grammar as the VS Code extension. This is native Sublime support, not an LSP client; it does not provide the VS Code extension's diagnostics or inferred types.

## Install

1. In Sublime Text 4 choose **Preferences > Browse Packages**.
2. Copy this entire folder there as **LegacyScript** (including `legacy_script.py`, grammar and catalogs).
3. Open a `.ls` file; choose **View > Syntax > LegacyScript** if it was previously associated with another language.
4. In LegacyEngine's **Editor Settings > Code editor**, select **Sublime Text 4**. Double-click scripts to open them. The setting is saved; switching back to VS Code or Internal remains available.

Install Sublime's `subl` command in PATH for launching from the engine. Standard Windows and macOS application locations are detected as well. Arguments are passed directly without shell interpolation.

API completions are intentionally a catalog, not type inference: same-named members may show several component annotations. Hover help is available for known API names. Existing user key bindings and snippets remain unchanged.

## Updating

Run `node extensions/legacyscript/scripts/generate-api.js` from the repository root to update both editors' API catalogs after engine API changes. The syntax grammar is mirrored from `extensions/legacyscript/syntaxes/legacyscript.tmLanguage.json` in XML plist format.

License: GPL-2.0-only, same as LegacyEngine.
