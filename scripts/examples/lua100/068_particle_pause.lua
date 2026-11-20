-- 068: particle pause
-- Setup: Playing ParticleSystem. Space toggles pause.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(ParticleSystem)
end
function onUpdate(dt)
    if c and isKeyPressed("Space") then
        if c.paused then c:resume() else c:pause() end
    end
end
