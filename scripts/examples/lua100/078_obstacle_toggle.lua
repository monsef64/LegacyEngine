-- 078: obstacle toggle
-- Setup: Obstacle on baked navmesh, separate from agents. E toggles avoidance.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshObstacle)
end
function onUpdate(dt)
    if c and isKeyPressed("E") then c.enabled = not c.enabled end
end
