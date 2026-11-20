-- 088: vehicle telemetry
-- Setup: Vehicle root. Can run alongside a separate driving script.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
local timer = 0
function onUpdate(dt)
    timer = timer + dt
    if c and timer >= 1 then
        timer = timer - 1
        debug.log("km/h", c.speed*3.6, "grounded wheels", c.groundedWheels)
    end
end
