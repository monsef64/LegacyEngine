# Terminal logs and crash reporting

Launch LegacyEngine from a terminal to see startup, initialization, shutdown,
script messages, warnings and errors. Normal logs go to **standard error** with
a UTC timestamp, severity and SDL category, so they can be captured separately
from standard output:

```sh
./LegacyEngine 2> engine.log
# Linux/macOS: view and save both streams at once
./LegacyEngine 2>&1 | tee engine.log
```

In PowerShell:

```powershell
.\LegacyEngine.exe 2>&1 | Tee-Object engine.log
```

The engine does not create a log file automatically. When launched without a
terminal, the desktop launcher may capture or discard standard error. Logs are
flushed immediately; no debug build or command-line option is required.

## What gets reported

- Startup, SDL/renderer initialization, entry into the editor and clean shutdown.
- Failed initialization, invalid command-line arguments and nonzero exits.
- Project errors and changes to asset, audio, preview, input/settings, history,
  material, skybox and Game-rendering error states. A persistent error is not
  printed again on every frame; clearing and recurring errors are reported again.
- LegacyScript output (`print` / `debug.log`), warnings, errors, runtime exceptions
  and script-asset load failures, while keeping the in-editor Console intact.
- Unhandled C++ exceptions in the application entry point and `std::terminate`.

## Crash reporting limits

On POSIX systems, SIGSEGV, SIGABRT, SIGILL, SIGFPE and SIGBUS (where available)
write a short fatal message directly to standard error, without allocating,
locking or attempting unsafe engine cleanup. The process still terminates with
its original signal; the operating system's debugger/core-dump policy is retained.
On Windows, unhandled native exceptions report their hexadecimal exception code,
and CRT aborts report a fatal message.

These are **best-effort diagnostics**, not crash recovery. They do not generate
stack traces or minidumps. SIGKILL, power loss, a blocked/broken stderr stream,
severe stack corruption, or an OS/driver failure may prevent any final message.
Use a debugger or operating-system crash dump for deeper investigation. Script
messages and asset paths can contain project information; review logs before
sharing them.

## Unexpected exits

Startup logs identify interactive, smoke-test, or screenshot mode. Smoke tests
and screenshots intentionally exit after 120 frames. Shutdown-request logs identify
SDL quit events, **File → Quit**, or completion of automated frames. A clean
shutdown message indicates that the editor loop and normal teardown completed;
it is not itself evidence of a crash. If the window exits unexpectedly, include
these lines and the shell exit status (`echo $?` immediately after running on
Linux/macOS) when reporting the issue.

Missing skybox/material/audio assets continue to be reported to the terminal,
but do not automatically open ImGui error windows. Inline Inspector/Asset Browser
validation messages remain available.
