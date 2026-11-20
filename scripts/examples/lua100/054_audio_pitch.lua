-- 054: audio pitch
-- Setup: Playing AudioSource. Hold W to raise pitch smoothly.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(AudioSource)
end
local pitch = 1
function onUpdate(dt)
    local target = isKeyDown("W") and 1.5 or 1
    pitch = pitch + (target - pitch) * (1 - math.exp(-4*dt))
    if c then c:setPitch(pitch) end
end
