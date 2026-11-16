# Asset browser: open location

Right-click an asset and choose **Open asset location** to open its containing
folder in the system file manager. Right-click a folder and choose **Open folder
location** to open that folder itself. The right-clicked item becomes selected.
The action is also present in model/prefab-specific context menus and works in
both list and grid views. It does not open an asset for editing or change files.

Paths are launched as percent-encoded file URIs without shell interpolation.
Missing paths or desktop-launch errors are shown through the asset browser error
reporting. A desktop file manager/URI handler is required; the headless Linux
build environment cannot verify desktop launch behavior.

## Manual checks

No automated tests are added/run.

- In grid and list views, right-click a folder, texture, terrain, script, model
  and prefab. Confirm the action opens the expected folder on Windows/MinGW.
- Check paths containing spaces, Unicode, `#`, `%`, apostrophes and ampersands.
- Verify right-clicking a previously unselected item opens that item's location,
  not the prior selection's location, without modifying any asset.
- Check a missing or inaccessible location gives a readable error. On a desktop
  Linux installation, check the file URI opens the default file manager as well.
