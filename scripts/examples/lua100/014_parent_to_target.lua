-- 014: parent to target
-- Setup: Create Target. Parents this object to it once.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local target = findGameObjectByName("Target")
    if target then self:setParent(target) end
end
