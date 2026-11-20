# Build and test
Requires a C++17 compiler, CMake 3.20+, Git and platform development libraries.
CMake fetches SDL3 (if not installed), Jolt, Dear ImGui, ImGuizmo and Lua.
GLAD, Roboto/Font Awesome fonts, miniaudio, simdjson, nlohmann/json and stb_image
are included under `third_party/`.

On Debian/Ubuntu, install the window/audio/OpenGL development dependencies used
in [the engine workflow](../.github/workflows/release.yml). Then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/LegacyEngine
```

Use `-DLEGACY_VENDORED_SDL=ON` to force a static vendored SDL build.
A graphical session and an OpenGL 3.3-capable driver are required at runtime.
Native file dialogs require a working desktop portal or another SDL dialog backend.
Linux CI builds on Ubuntu 24.04; check its run log for the resulting glibc requirement.
GitHub Actions builds/packages only, with `BUILD_TESTING=OFF`; tests are manual.
Use `-DBUILD_TESTING=ON` locally to build the optional test executables.

For headless graphical checks on Linux:

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a build/LegacyEngine --smoke-test
```

Smoke tests finish after 120 rendered frames. Screenshot capture is no longer
part of the editor or Actions artifacts.

Engine GLSL sources live in `data/shaders/` (lit, unlit/shadow, and skybox vertex
and fragment files). CMake copies `data/` beside the executable, and release
archives include it. Keep that directory beside the engine when moving binaries;
shader files are loaded from the executable's directory, not the working directory.
Missing shader data is reported as an initialization/render error. Edit these
files and rebuild to refresh the runtime copy; no C++ shader strings are maintained.

## Offline scripting reference

The release engine package includes `docs/index.html` and
`docs/scripting-reference.html`. Open **Help > Documentation** or
**Help > Scripting Reference** in the engine; both work offline and locate the
manual beside the executable, even after opening a project elsewhere.

For a local build, install `docs/requirements.txt` then run
`python docs/build.py --output build/docs` beside `build/LegacyEngine`.
If changing scripting APIs, update `scripts/definitions/legacyengine.lua` and
`docs/generate_reference.py` examples, then run
`python docs/generate_reference.py`. Documentation tests reject a stale reference
or an API without an example. The release workflow builds the complete HTML site,
includes it in the engine package, and publishes a standalone docs ZIP.

## Compact release binaries

`LEGACY_SMALL_RELEASE=ON` (the default) enables function/data sections and linker
section garbage collection for optimized GNU/Clang builds on Linux and Windows
MinGW-w64. This discards unreachable compiled code, including unused routines
inside static-library objects; it does not disable engine subsystems, remove
supported asset formats, change optimization level, or remove runtime data.
Debug builds are unaffected. Set `-DLEGACY_SMALL_RELEASE=OFF` to compare builds.

The Windows and Linux packaging workflows strip unneeded symbols **only from
the packaged executable copy**. The build-directory executable retains its
symbols. Keep all bundled runtime DLLs, shaders, particle data, documentation and
licenses; they are not expendable size savings. No executable packer is used.

A Linux Release/headless-SDL build measured during implementation:

| Executable | Bytes |
| --- | ---: |
| Previous linking settings, unstripped | 15,796,656 |
| Section garbage collection, unstripped | 13,740,696 |
| Section garbage collection, packaged/stripped copy | 12,194,640 |

The combined reduction is about 22.8%. These are executable sizes, **not archive
or full installation sizes**; Windows builds and other toolchains will differ.
The full desktop runtime and Windows MinGW package need manual verification.


## Engine versions and GitHub releases

`VERSION` contains the local development baseline (`MAJOR.MINOR.PATCH`), initially
`0.1.0`. A normal CMake build displays `<VERSION>-dev` in **Help > About**.
CI development builds append the short commit ID. To set a version explicitly:

```sh
cmake -S . -B build -DLEGACY_ENGINE_VERSION=1.2.3
```

This is a CMake cache setting: reconfigure with a new value when reusing a build
directory. The generated `engine_version.hpp` supplies the About window; do not
edit a second hard-coded UI version. Update `VERSION` in a normal change when
advancing the development baseline. Release tags override this baseline; a
release does not automatically modify or commit `VERSION`.

### Publish from Actions

After the workflow changes land on the default branch:

1. Open **Actions > Release > Run workflow**.
2. Choose the branch/commit to build and enter **version**, such as `1.2.3` or
   `1.3.0-rc.1` (a leading `v` is optional).
3. Keep **Publish GitHub Release** checked and run the workflow. It validates the version, builds the selected commit, then
   creates `v1.2.3` and a GitHub Release with generated notes. The engine archive
   is named `LegacyEngine-1.2.3-linux-x86_64.tar.gz` and its About window shows
   `1.2.3`. Documentation ZIP and LegacyScript VSIX are attached too.

The result appears at [GitHub Releases](https://github.com/monsef64/LegacyEngine/releases),
not just in Actions artifacts. The run summary contains a direct release link.

For artifacts only, uncheck **Publish GitHub Release**; the version input is ignored
and no tag/release is created. Publishing on a branch without a version fails early
with instructions instead of silently producing artifacts.
Alternatively, pushing a tag such as `v1.2.3` triggers the same release build.
Use semantic versions: patch for fixes, minor for compatible features, major for
breaking changes. Prerelease versions such as `1.3.0-rc.1` are marked as GitHub
prereleases; build metadata (`+...`) is not supported.

Existing tags may only be reused for the same commit. Rerunning that release
replaces matching assets, so prefer a new version for changed code. Publication
requires the repository's Actions policy to allow `contents: write`; no PAT is
needed. The release is created after the Linux build, with the VSIX attached by
a dependent job; if that job fails, rerun failed jobs to finish the release.

The existing Windows MinGW-w64 workflow also builds pushed version tags and embeds
the tag version in About. Its versioned Windows package remains an **Actions
artifact**, not a GitHub Release attachment. Tags created by the manual workflow's
`GITHUB_TOKEN` do not trigger other workflows: run Windows manually on that tag
if a Windows artifact is needed for a manually published release.

### Manual verification

- Configure/build locally and open About; check `<VERSION>-dev`.
- Reconfigure with `-DLEGACY_ENGINE_VERSION=1.2.3-rc.1`; rebuild and check About.
- Dispatch with **Publish GitHub Release** unchecked; confirm development artifacts and no release.
- Leave publishing checked without a version on a branch; confirm an actionable validation error.
- On a suitable release commit, dispatch a new prerelease version and inspect
  the tag target, release notes, prerelease flag, archive name and packaged About.
- Rerun the same version at the same commit; check asset replacement. Reusing a
  version on a different commit must fail before building.

Publishing is a real remote operation; it is not performed during local validation.

## Experimental Windows 7 SP1 x64 package

The Windows workflow builds **two separate artifacts**: the normal UCRT64 package
and `Windows7-x64-experimental`, built in MSYS2 **MINGW64 (MSVCRT)**. Do not use the
UCRT64 artifact as a Windows 7 package. Build on a modern Windows host; installing
current MSYS2 on Windows 7 is not required or supported by these instructions.

In a modern MSYS2 MINGW64 shell with Git, GCC, CMake, Ninja and Python installed:

```sh
cmake -S . -B build-win7 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DLEGACY_WINDOWS7=ON -DBUILD_TESTING=OFF
cmake --build build-win7 --target LegacyEngine --parallel 2
```

Package `LegacyEngine.exe`, `data/`, the license and its transitive MinGW runtime
DLLs together (see the Windows workflow). Run
`python tools/audit_windows7_imports.py package/LegacyEngine` from the toolchain
shell after packaging. This gate checks PE subsystem versions, missing/unreviewed
DLLs, UCRT dependencies and a conservative list of known newer static imports.
It is **not** a complete API-availability check: dynamically loaded APIs, driver
requirements and future toolchain changes still need review and runtime testing.
Do not copy system DLLs from newer Windows installations to bypass errors.

The compatibility build targets Windows 7 SP1 **64-bit**, retains OpenGL, and
omits unused SDL Renderer/GPU/Vulkan backends. A vendor-provided **OpenGL 3.3**
driver is mandatory; the stock Windows software driver is insufficient. This
package remains experimental until verified on real Windows 7 SP1 hardware.
Windows 7 itself is out of security support.

### Lower-cost defaults

`LEGACY_WINDOWS7=ON` defaults `LEGACY_LOW_END_DEFAULTS` to ON. New/missing shadow
settings use one 512px map instead of 1024px cascaded maps, reducing shadow passes
and map storage at a quality tradeoff. Existing project settings are preserved;
change their Rendering shadow mode/resolution explicitly if needed. Normal builds
retain their existing defaults. The low-end option can also be enabled separately
on other platforms, or explicitly disabled for the compatibility build. No frame
rate improvement has been benchmarked on Windows 7 hardware.

### Manual Windows navigation and compatibility checklist

- Start both packages on their intended OS with a vendor OpenGL driver; check
  startup, project open/save, assets, sound and Play/Stop. On Windows 7, verify
  there is no missing-DLL/entry-point error before calling the package usable.
- Hover Scene, hold RMB, move the mouse, and use WASD/QE and Shift. Repeat after
  editing an Inspector text field and while hovering (not dragging) a gizmo.
- Release RMB, press Escape, or Alt-Tab: cursor/grab must be restored. Return and
  repeat. Check MMB pan, Alt+MMB orbit, wheel zoom and Game input separately.
- Try remote desktop/relative-input failure if available: navigation should use
  the confined-warp fallback and log a warning rather than silently refuse RMB.
- Compare a fresh project against an existing project: low-end defaults should
  affect only missing settings, and saved rendering settings must survive reload.

## Windows console and Assets panel performance

Windows builds use the GUI subsystem and do not open a command window on normal
launch. To enable a debug console, run `LegacyEngine.exe -debug` (or add `-debug`
to a shortcut's target). It attaches to the launching console when possible,
otherwise opens one; engine logs go there. This also applies to the experimental
Windows 7 package. Linux terminal behavior is unchanged.

The Assets panel caches directory listings and file/directory metadata instead
of canonicalizing/stat-ing paths on every frame and sort comparison. Folder,
project, search-scope and asset-revision changes refresh the snapshot; external
filesystem edits are picked up within roughly one second while the panel is
visible. New folders/scripts/materials/cubemaps invalidate the listing directly.
Path containment validation remains in place when collecting entries and when
performing file operations.

Manual verification (no automated tests required):
- With the same scene, compare Profiler > Editor / panels / assets with Assets
  visible and with Console visible; compare warmed-up averages, not startup or
  thumbnail generation. Repeat in grid/list mode and with a large folder/search.
- Create/import/rename/copy/move/delete/undo assets; navigate folders, expand
  multi-mesh models, change projects, and verify the listing and thumbnails.
- Add/remove/change files externally and wait a second with Assets visible.
- On Windows, double-click the executable: no console should flash or remain.
  Repeat with a shortcut containing `-debug` and from a command prompt; logs
  should appear in the allocated/attached console. Repeat on Windows 7 if used.

Windows runtime behavior and FPS improvements require manual desktop validation;
no specific FPS gain is promised by the filesystem cache.
