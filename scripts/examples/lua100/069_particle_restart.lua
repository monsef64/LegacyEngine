-- 069: particle restart
-- Setup: ParticleSystem. R restarts simulation; C clears existing particles.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(ParticleSystem)
end
function onUpdate(dt)
    if not c then return end
    if isKeyPressed("R") then c:restart() end
    if isKeyPressed("C") then c:clear() end
end
