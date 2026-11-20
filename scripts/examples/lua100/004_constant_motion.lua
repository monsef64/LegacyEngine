-- 004: constant motion
-- Setup: Attach to a non-physics object. Speed is metres/second.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    self:translate(0, 0, -2 * dt)
end
