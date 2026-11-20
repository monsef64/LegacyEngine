-- 090: vehicle upright toggle
-- Setup: Vehicle root. U toggles hard upright lock. Turning it off permits flips.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
function onUpdate(dt)
    if c and isKeyPressed("U") then
        c.keepUpright = not c.keepUpright
        debug.log("Prevent flipping:", c.keepUpright)
    end
end
