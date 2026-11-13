#!/usr/bin/env python3
"""Generate the offline API reference from engine-owned scripting definitions."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'docs/scripting-reference.md'

# Executable fragments, not pseudo-code. Asset names are project-relative examples.
GLOBALS = {
'onStart': 'function onStart()\n    debug.log("Script started", self:getName())\nend',
'onUpdate': 'function onUpdate(deltaTime)\n    self:translate(deltaTime, 0, 0)\nend',
'onFixedUpdate': 'function onFixedUpdate(deltaTime)\n    debug.log("Fixed step", deltaTime)\nend',
'onDestroy': 'function onDestroy()\n    debug.log("Script stopping")\nend',
'createGameObject': 'local object = createGameObject("Player")\nobject:setPosition(0, 1, 0)',
'destroyGameObject': 'local object = createGameObject("Temporary")\ndestroyGameObject(object)',
'getComponent': 'local transform = getComponent(self, Transform)\nif transform then debug.log(transform.position.x) end',
'addComponent': 'local collider = addComponent(self, Collider)\ncollider.radius = 0.5',
'removeComponent': 'addComponent(self, Collider)\nremoveComponent(self, Collider)',
'findGameObjectByName': 'local player = findGameObjectByName("Player")\nif player then debug.log(player:getName()) end',
'findGameObjectsByComponent': 'for _, object in ipairs(findGameObjectsByComponent(Camera)) do\n    debug.log(object:getName())\nend',
'setParent': 'local child = GameObject.new("Child")\nsetParent(child, self)',
'getParent': 'local parent = getParent(self)\nif parent then debug.log(parent:getName()) end',
'createCamera': 'local camera = createCamera("Player Camera")\nsetActiveCamera(camera)',
'setActiveCamera': 'local camera = Camera.new("Player Camera")\nsetActiveCamera(camera)',
'getActiveCamera': 'local camera = getActiveCamera()\nif camera then debug.log(camera:getPosition().y) end',
'AudioClip': 'local clip = AudioClip("Audio/example.wav")\ndebug.log(clip)',
'createAudioSource': 'local audio = createAudioSource(self, AudioClip("Audio/example.wav"))\naudio:spatialBlend(1)',
'on': 'local token = on("scoreChanged", function(score)\n    debug.log("Score", score)\nend)',
'off': 'local token = on("scoreChanged", function(score) debug.log(score) end)\nlocal removed = off(token)\ndebug.log(removed)',
'emit': 'on("scoreChanged", function(score, name) debug.log(name, score) end)\nlocal accepted = emit("scoreChanged", 10, "Player")\ndebug.log(accepted)',
'onLevelLoaded': 'function onLevelLoaded(name)\n    debug.log("Loaded level", name)\nend',
'onLevelFailedToLoad': 'function onLevelFailedToLoad(name, reason)\n    debug.warning("Failed level", name, reason)\nend',
'fadeSkybox': 'local skybox1 = GameObject.new("Skybox1")\nlocal skybox2 = GameObject.new("Skybox2")\nskybox1:addComponent(Skybox):setColor(0.8, 0.9, 1)\nlocal evening = skybox2:addComponent(Skybox)\nevening:setColor(0.3, 0.1, 0.05)\nevening:setEnabled(false)\nfadeSkybox(skybox1, skybox2, 0.5)',
'getCurrentLevel': 'local name = getCurrentLevel()\nif name then debug.log("Current level", name) end',
}
for name in ('onMouseDown','onMouseUp','onMouseEnter','onMouseExit','onMouseOver','onAudioStarted','onAudioFinished','onAudioPaused','onAudioResumed','onAudioStopped'):
    GLOBALS[name] = f'function {name}()\n    debug.log("{name}", self:getName())\nend'
GLOBALS['waitForSeconds'] = 'function onStart()\n    debug.log("Before wait")\n    waitForSeconds(5)\n    debug.log("Five playing seconds later")\nend'
GLOBALS['wait'] = 'function onStart()\n    local audio = createAudioSource(self, AudioClip("Audio/example.wav"))\n    audio:play()\n    wait(audio.onAudioFinished)\n    debug.log("Audio finished")\nend'
GLOBALS['spawn'] = 'local copy = spawn(self, {x=0, y=2, z=0}, {x=0, y=90, z=0})\ndebug.log(copy:getName())'
GLOBALS['destroy'] = 'local copy = spawn(self, {x=0, y=2, z=0}, {x=0, y=0, z=0})\ndestroy(copy, 5)'
CAMERA_GLOBALS = {
'setCameraPosition': '{0, 2, 6}', 'setCameraRotation': '{0, 180, 0}',
'lookAt': '{0, 0, 0}', 'setFieldOfView': '60', 'setProjectionMode': '"Perspective"',
'setNearClipPlane': '0.1', 'setFarClipPlane': '100', 'setViewport': '0, 0, 1, 1',
'setCameraTarget': 'self', 'setFollowTarget': 'self, {0, 2, 5}', 'setCameraShake': '0.2, 0.5',
'worldToScreen': '{0, 0, 0}', 'screenToWorld': '{0.5, 0.5, 0.5}',
}
for name, args in CAMERA_GLOBALS.items():
    GLOBALS[name] = f'local camera = Camera.new("Reference Camera")\ncamera:setPosition(0, 2, 6)\nlocal result = {name}(camera, {args})'
for name in ('loadLevel', 'loadLevelAsync', 'setCurrentLevel'):
    GLOBALS[name] = f'local accepted, reason = {name}("Main")\nif not accepted then debug.warning(reason) end'
for name in ('unloadLevel', 'restartLevel', 'nextLevel', 'previousLevel'):
    GLOBALS[name] = f'local accepted, reason = {name}()\nif not accepted then debug.warning(reason) end'
for name in ('isKeyPressed', 'isKeyDown', 'isKeyReleased'):
    GLOBALS[name] = f'if {name}("Space") then debug.log("Space") end'
for name in ('isMouseButtonPressed', 'isMouseButtonDown', 'isMouseButtonReleased'):
    GLOBALS[name] = f'if {name}("Left") then debug.log("Left mouse") end'
for name in ('isGamepadButtonPressed', 'isGamepadButtonDown'):
    GLOBALS[name] = f'if {name}("South") then debug.log("South button") end'
for name in ('isButtonPressed', 'isButtonReleased', 'isButtonDown'):
    GLOBALS[name] = f'if {name}("Jump") then debug.log("Jump action") end'
for name in ('getMousePosition', 'getMouseDelta', 'getMouseWheelDelta'):
    GLOBALS[name] = f'local value = {name}()\ndebug.log(value.x, value.y)'
GLOBALS.update({
'getInputAxis': 'local horizontal = getInputAxis("Left", "Right")\ndebug.log(horizontal)',
'getInputVector': 'local movement = getInputVector("Left", "Right", "Down", "Up")\ndebug.log(movement.x, movement.y)',
'getGamepadAxis': 'debug.log(getGamepadAxis("leftx"))',
'getGamepadStick': 'local stick = getGamepadStick("Left")\ndebug.log(stick.x, stick.y)',
})
for name in ('lockCursor', 'unlockCursor', 'hideCursor', 'showCursor', 'isCursorVisible', 'isCursorLocked'):
    GLOBALS[name] = f'local result = {name}()\ndebug.log(result)'

METHOD_ARGS = {
'move':'0, -1', 'sprint':'true', 'setIsFreecam':'false',
'getComponent':'Transform', 'addComponent':'Collider', 'removeComponent':'Collider',
'setPosition':'1, 2, 3', 'setRotation':'0, 45, 0', 'setScale':'1, 1, 1', 'translate':'0, 0, 1',
'lookAt':'0, 0, -5', 'setParent':'nil', 'setName':'"Player"',
'setActive':'true', 'setFieldOfView':'60', 'setProjectionMode':'"Perspective"',
'setNearClipPlane':'0.1', 'setFarClipPlane':'100', 'setViewport':'0, 0, 1, 1',
'setCameraTarget':'nil', 'setCameraShake':'0.2, 0.5', 'setTarget':'nil', 'setFollowTarget':'self, {0, 2, 5}', 'setShake':'0.2, 0.5',
'worldToScreen':'{0, 0, 0}', 'screenToWorld':'{0.5, 0.5, 0.5}',
'setMaterial':'"Materials/example.mat"', 'setMaterialColor':'1, 0.6, 0.3',
'setMaterialTexture':'"Textures/albedo.png"', 'setMaterialNormalMap':'"Textures/normal.png"',
'setMaterialShader':'"Diffuse Bumped"', 'setMaterialNormalStrength':'1',
'setMaterialTiling':'2, 2', 'setMaterialSpecular':'0.5', 'setPitch':'1.2',
'fadeIn':'1', 'fadeOut':'1', 'setVolume':'0.75', 'setLooping':'true', 'setPan':'0',
'setDistance':'25', 'spatialBlend':'1', 'setSpatialBlend':'1',
'playOneShot':'AudioClip("Audio/example.wav")', 'setColor':'1, 0.8, 0.6',
'setIntensity':'2', 'setRange':'20', 'setEnabled':'true', 'setShadows':'true',
'setShadowQuality':'2', 'setSpotAngles':'15, 35', 'setType':'"Textures"',
'setCubemap':'"Sky/Evening.cubemap"', 'setHDR':'"Sky/studio.hdr"', 'setFace':'"front", "Sky/front.png"',
'setTextures':', '.join(f'"Sky/{face}.png"' for face in ('right','left','top','bottom','front','back')),
}
NO_ARGS = set('getForward getBackward getRight getLeft getUp getDown jump getIsFreecam isActive getPosition getRotation getScale getParent getName destroy resetMaterialOverrides play stop pause resume mute unmute isPlaying start'.split())


def inventory():
    text = (ROOT / 'scripts/definitions/legacyengine.lua').read_text()
    catalog_text = (ROOT / 'src/scripting/lua_definitions.hpp').read_text()
    catalog = dict(re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', catalog_text))
    entries, properties, annotations, owner = [], {}, [], ''
    for line in text.splitlines():
        cls = re.match(r'---@class (\w+)', line)
        if cls:
            owner = cls[1]
        method = re.match(r'---@field (\w+) fun\(([^)]*)\)(.*)', line)
        prop = re.match(r'---@field (\w+) (.+)', line)
        glob = re.match(r'function (\w+)\(([^)]*)\) end', line)
        if method:
            name, args, result = method.groups()
            entries.append({'key': f'{owner}.{name}', 'owner': owner, 'name': name,
                            'signature': f'{name}({args}){result}', 'notes': [catalog[name]] if name in catalog else []})
        elif prop:
            properties.setdefault(owner, []).append(prop.groups())
        if line.startswith('---@param') or line.startswith('---@return') or line.startswith('---@overload'):
            annotations.append(line[4:])
        elif glob:
            name, args = glob.groups()
            entries.append({'key': name, 'owner': '', 'name': name,
                            'signature': catalog.get(name, f'{name}({args})'), 'notes': annotations[:]})
            annotations.clear()
        elif not line.startswith('---'):
            annotations.clear()
    entries.append({'key':'print','owner':'','name':'print','signature':'print(...) - log tab-separated values','notes':[]})
    return entries, properties


def example(entry):
    owner, name = entry['owner'], entry['name']
    if not owner:
        return GLOBALS[name] if name != 'print' else 'print("Player position", self:getPosition().x)'
    if name.startswith('on') and owner == 'GameObject':
        args = 'deltaTime' if name in ('onUpdate', 'onFixedUpdate') else ''
        return f'self.{name} = function({args})\n    debug.log("Object callback: {name}")\nend'
    if owner == 'GameObjectConstructor':
        return 'local object = GameObject.new("Player")' if name == 'new' else 'local object = GameObject.find("Player")\nif object then debug.log(object:getName()) end'
    if owner == 'CameraConstructor':
        return 'local camera = Camera.new("Player Camera")' if name == 'new' else 'local camera = Camera.getActive()\nif camera then debug.log(camera:getName()) end'
    if owner == 'LegacyDebug':
        return f'debug.{name}("Example message", self:getName())'
    if owner == 'GameObject':
        prefix, variable = 'local object = self', 'object'
    elif owner == 'CameraObject':
        prefix, variable = 'local camera = Camera.new("Reference Camera")\ncamera:setPosition(0, 2, 6)', 'camera'
    else:
        variable = 'component'
        prefix = f'local component = self:getComponent({owner}) or self:addComponent({owner})'
        if owner == 'AudioSource':
            prefix += '\ncomponent.clip = AudioClip("Audio/example.wav")'
    if name == 'linkToCharacter':
        return 'local character = self:getComponent(CharacterController) or self:addComponent(CharacterController)\nlocal camera = Camera.new("Player FPS Camera")\nlocal fps = camera:addComponent(FPSCamera)\nfps:linkToCharacter(character, {x=0,y=0.7,z=0})'
    if name == 'fadeTo':
        return 'local skybox1 = self:getComponent(Skybox) or self:addComponent(Skybox)\nlocal skybox2 = GameObject.new("Skybox2"):addComponent(Skybox)\nskybox2:setColor(0.3, 0.1, 0.05)\nskybox2:setEnabled(false)\nskybox1:fadeTo(skybox2, 0.5)'
    if name in NO_ARGS:
        args = ''
    else:
        args = METHOD_ARGS[name]  # Unknown APIs fail generation until an example is supplied.
    return prefix + f'\n{variable}:{name}({args})'


CALLABLE_DETAILS = {
    'CharacterController.move': 'Sets persistent local X/Z input, clamped to unit length. Call move(0,0) to stop. Applied by fixed physics steps; do not multiply by deltaTime. Requires Transform, Collider and dynamic RigidBody (added automatically). Upright rotation is locked against physics torque. State flags update on fixed steps.',
    'CharacterController.jump': 'Queues one grounded jump. Requests while airborne or canJump=false are discarded; holding a button does not automatically retrigger unless your script requests another jump. isJumping means airborne and ascending, not falling.',
    'CharacterController.sprint': 'Stores sprint intent; true when omitted. isSprinting requires canMove, canSprint and nonzero movement input.',
    'FPSCamera.linkToCharacter': 'Accepts a CharacterController component or its GameObject. Parents the camera and sets its parent-local position offset, default (0,0.7,0). Mouse yaw turns the character, while pitch stays local and clamps to +/-89 degrees. Existing Camera APIs remain available on the camera GameObject; FPSCamera also forwards Camera settings and methods.',
    'FPSCamera.setIsFreecam': 'Unparented FPS cameras with isFreecam=true use native WASD and Q/E movement while Game input is focused. False disables translation, not mouse look. Parented FPS cameras never translate freely. Mouse look requires a locked cursor; mouseLook=false leaves rotation to scripts.',

    'destroyGameObject': 'Immediately destroys the GameObject and all descendants, including inactive children. Attached scripts receive onDestroy once. To keep a child, detach it with child:setParent(nil) before deleting its parent. Editor deletion also removes the entire subtree and supports undo/redo.',
    'spawn': 'Creates a prefab asset instance or copies a GameObject subtree and returns its new root. Prefab paths are project-relative .prefab strings, not component tokens. Position is world-space; rotation accepts Euler degrees or a quaternion. Internal object references are remapped; external references are cleared. See the Prefab workflow guide.',
    'destroy': 'Destroys the root and its descendants now (omitted/zero delay) or after nonnegative playing seconds. Pause freezes delayed destruction. A later schedule replaces the delay; pending timers are not serialized.',
    'getComponent': 'Use getComponent(Type) for attached self, getComponent(object, Type) for an explicit owner, or object:getComponent(Type). Missing components return nil. The implicit form requires an attached live self.',
    'Skybox.setCubemap': 'Selects Cubemap mode and assigns a project-relative .cubemap asset. Author its six faces in Cubemap Editor; new Skybox components no longer need six independent Inspector slots. Nil clears the cubemap.',
    'Skybox.setHDR': 'Selects HDR mode and assigns a project-relative Radiance panorama. Nil clears the slot. Use setCubemap for six-face skybox assets.',
    'onMouseDown': 'Runs on the nearest active Collider hit by a left-button press in Game. Requires an active Game camera; this is object picking, not a global input hook.',
    'onMouseUp': 'Runs on the object that received the matching Down, even if the pointer has moved off it. No callback is delivered after the object is destroyed or inactive.',
    'onMouseEnter': 'Runs once when the hovered Collider changes to this object. Picking uses the active camera viewport; a locked cursor uses its center.',
    'onMouseExit': 'Runs when hover leaves this active object, including moving out of Game. Deactivated objects do not receive callbacks.',
    'onMouseOver': 'Runs every playing frame while this object is the nearest active Collider under the Game pointer. It does not require a pressed button.',
    'onAudioStarted': 'Runs on scripts attached to the AudioSource owner after play/fadeIn starts or restarts the primary clip. One-shots do not produce this primary-clip event.',
    'onAudioFinished': 'Runs on the AudioSource owner when its non-looping primary clip reaches its natural end. Explicit stop, fade-out stop, decode failure and one-shot completion do not count.',
    'onAudioPaused': 'Runs on the AudioSource owner when pause changes Playing to Paused. Calling pause again while already paused does not emit another event.',
    'onAudioResumed': 'Runs on the AudioSource owner when resume changes Paused to Playing. Resuming continues from the existing playback cursor.',
    'onAudioStopped': 'Runs on the AudioSource owner when a playing/paused primary source is explicitly stopped, including fade-out stop or playback failure. It is distinct from natural Finished.',
    'waitForSeconds': 'Suspends the current lifecycle/object callback for finite playing-time seconds in [0, 86400]. Zero resumes no earlier than the next update. Other callbacks continue; this does not block the engine.',
    'wait': 'Takes an opaque AudioEvent token, not a function name string or a call to the event. Waits for its next occurrence after subscribing. Stop/reset cancels waits; removing the AudioSource cancels its continuation.',
    'GameObject.setActive': 'Sets local activity; omitted enabled defaults to true. Inactive parents make descendants effectively inactive without changing their local flags. Inactive objects are hidden, excluded from physics, silent, and their attached script updates/events/waits pause. Another active script or Inspector can reactivate them.',
    'GameObject.isActive': 'Returns effective activity, including all parents. It can be false even if this object is locally enabled. Destroyed handles raise a script error rather than returning false.',
}


def render_reference():
    entries, properties = inventory()
    lines = ['# Scripting Reference', '',
             'Complete engine API reference: global functions, constructors, component/object methods,',
             'lifecycle events, level events, custom events and exposed fields. Generated from the',
             'engine scripting definitions; every callable below has a usage example.', '',
             '[TOC]', '', '## How to use the examples', '',
             'Examples are independent fragments for an attached `.ls` or `.lua` script, where `self`',
             'is its GameObject. Place setup/calls inside `onStart()` unless an example defines a',
             'callback itself. They are not a single script to concatenate. Vector arguments can be',
             'tables; object methods also accept numeric coordinates as shown. `:` supplies the',
             'receiver; constructors and `debug` logging use `.`.', '',
             'Import the example asset paths before running audio/material/skybox examples. Configure',
             'named input buttons in Project > Input Settings and create/register the `Main` level',
             'before level examples. Find/get functions can return nil; check before dereferencing.', '',
             'See [LegacyScript guide](scripting.md), [input/audio details](audio-input.md),',
             '[prefab workflow](prefabs.md), [level storage](projects.md#levels), and [skybox transitions](skybox.md#smooth-scripted-crossfades).', '',
             '## Events and runtime rules', '',
             '- `onStart()` runs once when an attached runtime starts; `onUpdate(dt)` runs per playing frame.',
             '- `onFixedUpdate(dt)` runs before fixed physics steps; `onDestroy()` runs on teardown.',
             '- `onLevelLoaded(name)` runs in destination scripts after startup, including initial Play.',
             '- `onLevelFailedToLoad(name, reason)` runs in the still-active level on a rejected/failed load.',
             '- `on(name, callback)` subscribes, `off(token)` unsubscribes, `emit(name, ...)` queues custom events.',
             '- Custom event arguments are nil/boolean/number/string only, at most eight per event.',
             '- Delivery is deferred; events emitted by listeners wait for the next pump. Failing listeners',
             '  are removed and logged; runtime stop/reset removes subscriptions. ',
             '- Custom event limits: 256 queued events, 128 listeners per runtime, 128-byte names and',
             '  4096-byte strings. Check the boolean returned by `emit` and remove unneeded listeners.',
             '- Level transition functions return acceptance and an optional error, not completion.',
             '- Lifecycle failures stop the failing script. Errors retain source filename/line and traceback.',
             '- Mouse callbacks target the nearest active Collider under the Game camera in the Game view.',
             '  Enter/Exit track hover, Over runs every playing frame, Down targets a left press and',
             '  Up targets the object that received Down, even if released outside it.',
             '- Audio callbacks target scripts attached to the AudioSource owner. Finished means natural',
             '  primary-clip completion, not pause, explicit stop, fade-out stop or one-shot completion.',
             '- `waitForSeconds` and `wait(audio.onAudioFinished)` suspend lifecycle/object callbacks only.',
             '  Do not use them at file top-level, in custom event listeners or onDestroy. Each callback',
             '  has at most one pending invocation. Other callbacks keep running; Play pause or object',
             '  deactivation freezes timers, and Stop cancels waits. Audio waits observe the next event,',
             '  not one that happened before subscribing. A looping clip never naturally finishes.',
             '- `self:setActive(false)` hides an object, disables its physics/audio and pauses attached',
             '  script updates/events/waits. Parents propagate inactivity; another active script or the',
             '  Inspector can reactivate it. State persists in project/level saves and undo history.',
             '- `debug.error()` logs without throwing. Memory/instruction limits still apply.', '',
             'The sandbox also exposes Lua 5.4 base, table, string, math and utf8 computation libraries.',
             'It does not expose filesystem/process/module loading, coroutines, pcall/xpcall or the',
             'standard introspection debug library. Those standard computation-library APIs are not',
             'engine functions; this reference covers the engine-owned API.', '']
    lines += ['## Callback and wait quick guide', '',
              '| API | Where to define / call it | Requirement |',
              '| --- | --- | --- |',
              '| `onMouseDown/Up/Enter/Exit/Over()` | Global function in the target object’s attached script, or `object.onMouseDown = function() ... end` | Active Collider and Game camera |',
              '| `onAudioStarted/Finished/Paused/Resumed/Stopped()` | Global function in the AudioSource owner’s attached script, or object callback | AudioSource on that owner |',
              '| `waitForSeconds(seconds)` | Inside a running lifecycle/object callback | Finite 0–86400 seconds |',
              '| `wait(audio.onAudioFinished)` | Inside a running lifecycle/object callback | Live AudioSource; next event only |', '',
              'AudioSource event properties are opaque `AudioEvent` tokens. Define the callback as',
              '`function onAudioFinished() ... end`; pass `audio.onAudioFinished` to `wait` without',
              'parentheses. Do not assign a function to the AudioSource event property.', '',
              'Waits are not supported at file top-level, in custom/level event listeners, or in',
              '`onDestroy`. Play pause and object inactivity freeze timer progress. The engine allows',
              'at most 128 pending callbacks per runtime and coalesces repeated invocations of the',
              'same waiting callback. Shutdown/reset cancels continuations; variables survive a pause.', '',
              '```lua', 'function onStart()',
              '    local audio = self:getComponent(AudioSource)',
              '    if not audio then return end',
              '    waitForSeconds(5)',
              '    audio:play()',
              '    wait(audio.onAudioFinished)',
              '    debug.log("Continue after natural playback completion")',
              'end', '```', '',
              'For complete setup and troubleshooting, see [mouse and audio events](audio-input.md#gameobject-mouse-callbacks)',
              'and [object activity and waits](scripting.md#object-activity-mouseaudio-callbacks-and-waits).', '']
    groups = {'': 'Global functions and callbacks'}
    for entry in entries:
        groups.setdefault(entry['owner'], entry['owner'])
    for owner, title in groups.items():
        lines += [f'## {title}', '']
        for entry in (e for e in entries if e['owner'] == owner):
            lines += [f'### {entry["key"]}', '', f'`{entry["signature"]}`', '']
            detail = CALLABLE_DETAILS.get(entry['key'])
            if detail:
                lines += [detail, '']
            if entry['notes']:
                lines += [f'- `{note}`' for note in entry['notes']] + ['']
            lines += ['```lua', example(entry), '```', '']
    lines += ['## Component fields and type tokens', '',
              'Component type names such as `Transform`, `Skybox`, `PointLight` and `AudioSource`',
              'are opaque tokens for get/add/remove operations, not callable loaders.',
              '`BoxCollider` aliases `Collider`. `Texture` is an export-only project asset path.',
              '`self` is generation-safe; using destroyed objects/components raises a script error.', '']
    for owner, fields in properties.items():
        lines += [f'### {owner} fields', '', '| Field | Type / meaning |', '| --- | --- |']
        lines += [f'| `{name}` | `{kind.replace("|", " / ")}` |' for name, kind in fields] + ['']
    lines += ['## Skybox1 to Skybox2 transition', '',
              'Use `fadeSkybox(skybox1, skybox2, speed)` for explicit source/destination,',
              '`fadeSkybox(skybox2, speed)` for the active source, or `skybox1Component:fadeTo(skybox2, speed)`.',
              'A speed of 0.5 takes two seconds with smoothstep easing. The source stays enabled during',
              'the blend. At completion Skybox1 is disabled and Skybox2 is enabled: their names,',
              'component identities, textures and colors are not overwritten. Prepare valid textures',
              'first; failed destination loading cancels the transition and keeps the source.', '',
              '```lua', 'function onStart()',
              '    local skybox1 = GameObject.find("Skybox1")',
              '    local skybox2 = GameObject.find("Skybox2")',
              '    if skybox1 and skybox2 then fadeSkybox(skybox1, skybox2, 0.5) end', 'end', '```', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    content = render_reference()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != content:
            parser.exit(1, 'Scripting reference is stale; run python3 docs/generate_reference.py\n')
    else:
        OUTPUT.write_text(content)
