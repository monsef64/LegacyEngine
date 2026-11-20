-- 079: obstacle size
-- Setup: Obstacle on baked plane. Dynamic box changes cause bounded agent repaths.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshObstacle)
end
local time = 0
function onUpdate(dt)
    time = time + dt
    if c then c.size = Vector3(2 + math.sin(time), 2, 1) end
end
