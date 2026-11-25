# Animator state-machine demo

Copy these files into your project's `Assets/AnimatorDemo/` folder. Create a box
GameObject with Animator, assign `AnimatorDemo/Locomotion.controller`, and attach
`AnimatorDemo.lua` as LegacyScript to the same object. No imported model is needed:
the three clips animate Transform scale so transitions/fades are easy to see.

Open the controller using Inspector **Edit controller** or double-click its asset.
The script cycles Bool/Int/Float parameters through Idle, Move and Jump. Observe
the active state and live parameters during Play. Stop Play restores the pre-play
world. For a character, replace the three clip assignments with that character's
imported `.leanim` clips. Fading model clips requires the same model/skeleton.

Right-click a state > Create transition, then click its destination. Click an
arrow (or select it under All transitions) to edit fade seconds, normalized exit
time and AND conditions. Use each state's Loop checkbox. Any State transitions
have priority only by their position in the transition list, not automatically.
