# Independent waiting blocks: manual verification

No automated tests are added or run for this change.

1. In onStart, create a task that logs A, waits two seconds, then logs C; log B
   immediately after task(...). Expect A, B immediately, then C after two playing
   seconds. Repeat with wait(2) and waitForSeconds(2).
2. Keep unrelated onUpdate movement/logging running while the task waits. Compare
   a direct wait inside an if: it still suspends its callback, including statements
   after the if. Only explicitly wrapped task blocks run independently.
3. Start two tasks with different delays and a nested task. Verify completion
   ordering, multiple sequential waits, captured local variables and wait(0)
   resuming no earlier than a later update. A task with no wait completes immediately.
4. Guard tasks launched from onUpdate with a busy flag. An unguarded task spawned
   every frame can exhaust the shared 128-live-task limit and should report an error.
5. Pause Play and deactivate/reactivate the owner: pending timers should freeze.
   Destroy the owner, Stop Play or reset the script: pending work must not resume.
6. Inside a task, play audio, wait(audio.onAudioFinished), then log. Check natural
   completion, looping audio, unrelated audio events and source removal. Event waits
   still listen to the next matching event rather than consuming an old event.
7. Verify task rejects non-functions, file-top-level/listener/shutdown calls and
   onDestroy. Verify wait rejects negative, nonfinite and over-86400-second timers.
8. Verify task errors show a script traceback and follow existing runtime failure
   handling. Infinite loops in new/resumed/nested tasks must remain instruction-
   limited, and tasks/closures remain subject to the runtime memory budget.
9. Inspect VS Code task/wait completion, hover signatures and the task snippet;
   task(function() ... end) is ordinary Lua syntax and should validate normally.

Cancellation does not run code following a wait (including busy-flag cleanup).
These are cooperative script tasks, not parallel OS threads or arbitrary async
if/loop syntax. The API does not return cancellation handles or task results.
