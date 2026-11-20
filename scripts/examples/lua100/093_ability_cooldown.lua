-- 093: ability cooldown
-- Setup: Focus Game. E activates at most once every two seconds.
-- Attach this script to one object using its LegacyScript component.

local cooldown = 0
function onUpdate(dt)
    cooldown = math.max(0, cooldown-dt)
    if cooldown == 0 and isKeyPressed("E") then
        cooldown = 2
        debug.log("Ability activated")
    end
end
