-- 070: collision particles
-- Setup: Dynamic Collider + ParticleSystem with normal emission disabled.
-- Attach this script to one object using its LegacyScript component.

function onCollisionEnter(other)
    local particles = self:getComponent(ParticleSystem)
    if particles then particles:emit(15) end
end
