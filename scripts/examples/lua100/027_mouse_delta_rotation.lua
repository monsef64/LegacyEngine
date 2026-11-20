-- 027: mouse delta rotation
-- Setup: Attach to a decorative mesh. Hold right mouse in Game to turn it.
-- Attach this script to one object using its LegacyScript component.

local yaw = 0
function onUpdate(dt)
    if isMouseButtonDown("right") then
        local delta = getMouseDelta()
        yaw = yaw - delta.x * 0.2
        self:setRotation(0, yaw, 0)
    end
end
