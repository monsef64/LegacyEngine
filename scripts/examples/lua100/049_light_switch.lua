-- 049: light switch
-- Setup: Dynamic PointLight. Press E in Game to switch it.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(PointLight)
end
local enabled = true
function onUpdate(dt)
    if c and isKeyPressed("E") then enabled = not enabled; c:setEnabled(enabled) end
end
