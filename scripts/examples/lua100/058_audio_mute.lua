-- 058: audio mute
-- Setup: Playing AudioSource. M toggles mute.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(AudioSource)
end
local muted = false
function onUpdate(dt)
    if c and isKeyPressed("M") then
        muted = not muted
        if muted then c:mute() else c:unmute() end
    end
end
