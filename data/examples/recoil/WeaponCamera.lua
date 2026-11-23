-- Attach to the camera. Add Camera > Recoil, then assign optional clip/prefab fields.
-- Configure Fire in Project > Input Settings. No engine-host filesystem APIs used.
expose Recoil recoil
expose Animation weaponAnimation
expose AnimationClip reloadClip = ""
expose Prefab casing = ""
expose float roundsPerSecond = 10

function onStart()
    recoil = recoil or self:getComponent(Recoil) or self:addComponent(Recoil)
    recoil.pitch = 2.0
    recoil.yaw = 0.15
    recoil.roll = -0.2
    recoil.back = 0.025
    recoil.frequency = 8
    recoil.damping = 0.85
    recoil.positionFrequency = 12
    recoil.positionDamping = 1
    recoil.maxAngle = 12
    recoil.maxDistance = 0.12
    recoil.interval = 1 / math.max(0.1, math.min(roundsPerSecond, 100))
    recoil.burstCount = 0
end

local firing = false
local reloading = false

function onUpdate(dt)
    -- Timed firing is driven by game time inside Recoil, not one impulse per frame.
    local pressed = isButtonDown("Fire") and not reloading
    if pressed and not firing then recoil:start() end
    if not pressed and firing then recoil:stop() end
    firing = pressed
end

-- Call through a named script component, e.g. weapon:reload(). Runs in a task.
function reload()
    if reloading or not weaponAnimation or reloadClip == "" then return end
    reloading = true
    recoil:stop()
    firing = false
    weaponAnimation.loop = false
    weaponAnimation:playClip(reloadClip)
    -- onStarted is emitted after asynchronous loading. Check animation.error if
    -- the asset is invalid; a failed load does not emit onStarted/onFinished.
    wait(weaponAnimation.onFinished)
    reloading = false
end

-- Example scheduled pattern, independently callable from another component.
function previewBurst()
    recoil:setSequence({
        {time = 0.00, strength = 1.0},
        {time = 0.09, strength = 1.1},
        {time = 0.18, strength = 1.3}
    })
    recoil.sequenceLoop = false
    recoil:playSequence()
end

-- Destruction and Stop Play cancel owned waits/tasks automatically.
-- Stop firing before removing this script from a still-live camera.
function stopFiring()
    firing = false
    recoil:stop()
end
