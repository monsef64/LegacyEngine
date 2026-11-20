-- 026: cursor toggle
-- Setup: Focus Game. L locks/unlocks; Escape unlocks.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    if isKeyPressed("L") then
        if isCursorLocked() then unlockCursor() else lockCursor() end
    end
    if isKeyPressed("Escape") then unlockCursor() end
end
function onDestroy() unlockCursor() end
