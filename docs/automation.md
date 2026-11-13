# Automation and limitations
- [Engine workflow](../.github/workflows/release.yml): build, Linux packaging and a
  separate VSIX job/artifact. Tags publish engine and VSIX release assets;
  branch/PR/manual runs provide downloadable artifacts.
- [VSIX workflow](../.github/workflows/legacyscript.yml): regenerate engine API catalogs,
  package and upload the installable extension. Relevant source paths track the
  subsystem layout.

The engine is experimental. Arbitrary imported meshes/material pipelines, OGG,
folder rename/trash, named analog bindings and a full Lua debugger are not implemented.
System Trash is Linux-tested; platform-dependent dialogs, clipboard, VS Code URI
launching, real audio hardware and physical gamepads need target-platform testing.

The engine and standalone VSIX workflows also build the [offline HTML/CSS documentation](index.md#build-this-documentation), publish **legacyengine-docs**, and include it in engine packages. Tagged engine releases attach `legacyengine-docs.zip`.

GitHub Actions does not run automated tests or formatting checks. Release builds
configure with `BUILD_TESTING=OFF`; the existing test sources remain available
for manual local testing. See [Building](building.md) for local commands.
