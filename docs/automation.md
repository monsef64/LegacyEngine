# Automation and limitations
- [Engine workflow](../.github/workflows/release.yml): build, regression tests, Xvfb
  graphical light/shadow/cursor tests, screenshot capture, Linux packaging and a
  separate VSIX job/artifact. Tags publish engine and VSIX release assets;
  branch/PR/manual runs provide downloadable artifacts.
- [VSIX workflow](../.github/workflows/legacyscript.yml): regenerate engine API catalogs,
  test the LSP and upload the installable extension. Relevant source paths track the
  subsystem layout.

The engine is experimental. Arbitrary imported meshes/material pipelines, OGG,
folder rename/trash, named analog bindings and a full Lua debugger are not implemented.
System Trash is Linux-tested; platform-dependent dialogs, clipboard, VS Code URI
launching, real audio hardware and physical gamepads need target-platform testing.

The engine and standalone VSIX workflows also validate and build the [offline HTML/CSS documentation](index.md#build-this-documentation), publish **legacyengine-docs**, and include it in engine packages. Tagged engine releases attach `legacyengine-docs.zip`.
