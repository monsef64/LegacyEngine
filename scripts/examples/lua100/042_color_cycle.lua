-- 042: color cycle
-- Setup: Attach to a visible mesh.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(MeshRenderer)
end
local time = 0
function onUpdate(dt)
    time = time + dt
    if c then c:setMaterialColor(0.5+0.5*math.sin(time), 0.4, 0.5+0.5*math.cos(time)) end
end
