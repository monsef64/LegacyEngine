-- 038: trigger counter
-- Setup: Attach to a trigger Collider; let another Collider enter/exit.
-- Attach this script to one object using its LegacyScript component.

local count = 0
function onTriggerEnter(other)
    count = count + 1
    debug.log("Overlaps:", count)
end
function onTriggerExit(other)
    count = math.max(0, count - 1)
    debug.log("Overlaps:", count)
end
