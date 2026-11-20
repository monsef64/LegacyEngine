-- 067: particle burst
-- Setup: ParticleSystem with emission disabled in Inspector. Space emits 25 particles.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(ParticleSystem)
end
function onUpdate(dt)
    if c and isKeyPressed("Space") then c:emit(25) end
end
