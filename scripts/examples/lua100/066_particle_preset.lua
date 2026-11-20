-- 066: particle preset
-- Setup: Attach to any object. Adds a ParticleSystem if missing.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local particles = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
    particles:setPreset("Fire")
    particles:play()
end
