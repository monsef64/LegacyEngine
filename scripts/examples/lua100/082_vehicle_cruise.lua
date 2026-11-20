-- 082: vehicle cruise
-- Setup: Vehicle root on a wide floor. Simple 8 m/s cruise controller.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
function onFixedUpdate(dt)
    if not c then return end
    c.throttle = math.max(0, math.min(1, (8-c.speed)*0.3))
    c.brake = c.speed > 9 and 0.3 or 0
end
