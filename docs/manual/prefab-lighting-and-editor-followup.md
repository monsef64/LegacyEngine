# Prefab lighting, suspension and animation view modes

## Implemented in this change

- Prefab Scene view gets a render-only warm key, cool fill and ambient floor.
  No helper entities or components are added to the edited world.
- Anti-roll strength and its suspension load-transfer behavior are removed.
  Existing saved settings are tolerated as ignored JSON keys. Center-of-mass
  offsets and spring/damper suspension are retained.
- Dope Sheet / Curves controls align to the right of the Animation status row.

## Manual verification (no automated tests added or run)

1. Open a dark prefab with no lights. Its geometry should be readable in Scene.
   Inspect the hierarchy: there must be no injected lights. Save and reopen the
   prefab; compare its object/component inventory with the original.
2. Test a prefab with existing lights, many lights, a skybox, camera effects,
   materials, terrain and linked instances. Studio lights should not cast shadows
   or enter baked lighting. Verify the light array stays within shader limits.
3. Exit prefab mode and compare normal Scene/Game lighting before/after. Repeat
   opening, Save, Exit, Undo/Redo and save/reopen the parent level. No lighting
   override should persist into Game views, other previews or saved assets.
4. Load a previously saved vehicle containing antiRollStrength. It should open
   without error, show no anti-roll field and omit that setting on the next save.
   Verify Lua definitions/completions no longer advertise it. Drive on slopes,
   uneven ground, dynamic platforms, jumps and flips. Independent suspension,
   COM tuning, wheel visuals and Stop rollback must remain intact.
5. Resize/dock the Animation window at wide and narrow sizes. Dope Sheet/Curves
   buttons should stay right-aligned, wrap without overlapping track/frame status,
   and preserve selection, curves, timeline controls and copy/paste operations.
6. Repeat interactive checks on Linux OpenGL and Windows MinGW-w64.

## Requested work not implemented in this change

The broader request remains incomplete. The following need further implementation,
not just UI labels or placeholders:

- Reversible import-time mesh combining, including compatible animated/skinned
  parts, preserving skeletons, material sections and original removable parts.
- Automatic import-time prewarming of both icon and hover previews for every
  supported asset type, using the default project cache and bounded scheduling.
- Named script component lookup, safe cross-script field/function access and
  animation events targeting a specific attached script component.
- File > Export with required game/company/version/start-scene/icon settings.
- Standalone Windows and Linux game players, Windows executable icon/version
  resource stamping, and authenticated encrypted chunk packaging/readback.
- Exported layout: GameName.exe (Windows) or GameName (Linux), with a sibling
  GameName_Data directory containing encrypted data chunks. Linux has no Windows
  EXE version-resource equivalent; platform metadata must be handled separately.

The export pipeline must use reviewed cryptography with authenticated chunks,
validated paths/manifest limits, bounded streaming, explicit key handling and
failure-safe output staging. Shipping players must be able to decrypt their data,
so encryption cannot guarantee protection against extraction. No exporter, player
or encryption functionality is claimed by this change.
