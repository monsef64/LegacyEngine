-- 055: audio pan
-- Setup: Playing AudioSource; this demo uses 2D panning.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(AudioSource)
end
local time = 0
function onUpdate(dt)
    time = time + dt
    if c then c:setSpatialBlend(0); c:setPan(math.sin(time)) end
end
