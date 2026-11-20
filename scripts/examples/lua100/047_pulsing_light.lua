-- 047: pulsing light
-- Setup: Attach to a dynamic PointLight.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(PointLight)
end
local time = 0
function onUpdate(dt)
    time = time + dt
    if c then c:setIntensity(1.5 + math.sin(time * 3)) end
end
