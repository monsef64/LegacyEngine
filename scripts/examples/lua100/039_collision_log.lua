-- 039: collision log
-- Setup: Attach to a dynamic body with Collider; drop onto a floor.
-- Attach this script to one object using its LegacyScript component.

function onCollisionEnter(other)
    if other then debug.log("Hit:", other.gameObject:getName()) end
end
