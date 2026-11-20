-- 017: timed destroy
-- Setup: Attach to an expendable object. Destroys it after three seconds.
-- Attach this script to one object using its LegacyScript component.

local elapsed = 0
function onUpdate(dt)
    elapsed = elapsed + dt
    if elapsed >= 3 then self:destroy() end
end
