-- 018: count meshes
-- Setup: Attach to any object; prints count once.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local renderers = findGameObjectsByComponent(MeshRenderer)
    debug.log("Mesh objects:", #renderers)
end
