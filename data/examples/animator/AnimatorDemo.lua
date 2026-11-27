-- Attach to the same GameObject as Animator; assign Locomotion.controller.
function onStart()
    local animator = self:getComponent(Animator)
    if not animator then return end
    animator:setBool("Grounded", true)
    animator:setInt("Mode", 0)
    animator:setFloat("Speed", 0)
    task(function()
        while true do
            waitForSeconds(2)
            animator:setFloat("Speed", 2)
            waitForSeconds(2)
            animator:setInt("Mode", 1)
            animator:setBool("Grounded", false)
            waitForSeconds(1)
            animator:setBool("Grounded", true)
            animator:setFloat("Speed", 0)
            waitForSeconds(2)
        end
    end)
end
-- Manual overrides, e.g. from your own callbacks:
-- animator:crossFade("Move", 0.3)
-- animator:play("Idle")
-- animator:pause(); animator:resume(); animator:stop()
