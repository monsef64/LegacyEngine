-- 100: timed spawn wave
-- Setup: Attach to an empty object. Spawns five static cubes, one each second.
-- Attach this script to one object using its LegacyScript component.

local elapsed, count = 0, 0
function onUpdate(dt)
    elapsed = elapsed + dt
    if count < 5 and elapsed >= 1 then
        elapsed = elapsed - 1; count = count + 1
        local object = createGameObject("Wave cube " .. count)
        object:addComponent(Transform)
        local mesh = object:addComponent(MeshRenderer)
        mesh.mesh = "Cube"
        object:setPosition(count*2, 1, 0)
    end
end
