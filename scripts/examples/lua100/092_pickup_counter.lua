-- 092: pickup counter
-- Setup: Attach to a player trigger. Name collectible objects Coin.
-- Attach this script to one object using its LegacyScript component.

local score = 0
function onTriggerEnter(other)
    if other and other.gameObject:getName() == "Coin" then
        score = score + 1
        debug.log("Coins:", score)
        other.gameObject:destroy()
    end
end
