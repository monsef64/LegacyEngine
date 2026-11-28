# Manual verification: embedded LuaJIT

No automated tests were added or run for the runtime migration.

## Builds and packaging

- Build the native Linux editor/player and both MSYS2 MinGW-w64 Windows variants
  from clean directories. Confirm GNU make is installed even when CMake uses
  Ninja. No system Lua package/interpreter or lua51.dll should be required.
- Configure two build directories with different native compilers: LuaJIT objects
  must remain private to each build directory. Reconfigure/rebuild incrementally.
- Package/export and launch a game on a clean host. Confirm LuaJIT.txt and the
  retained Lua.txt (UTF-8 library attribution) are shipped. License collection
  must not reference the removed _deps/lua-src checkout.

## Engine script behavior

- Run scripts using global int/float/bool/string and component references. Check
  defaults, Inspector edits, save/reload, prefab overrides and cross-script calls.
- Exercise lifecycle callbacks, custom events, transform/component userdata fields,
  object lookup, vectors, input, audio and Animator scripts in editor and player.
- Test waitForSeconds, task(function() ... end), audio/animation waits, Pause/Resume, destruction,
  Stop/Play and level transitions. A callback error must not kill other scripts.
- Log strings/numbers/booleans/nil/userdata; confirm useful string conversion and
  tracebacks. Read/write Game save data with integer and fractional values.
- Use utf8.len/codepoint/char/offset/codes with ASCII, Arabic and supplementary
  Unicode characters. Check invalid byte sequences and argument bounds.
- Exercise bit.bor flag combinations in a gameplay script.
  Verify old 5.4 bitwise/division operators and local attributes report syntax
  errors, rather than being silently misinterpreted. VS Code diagnostics and the
  runtime should agree; .luarc.json must select LuaJIT.

## Sandbox and limits

- Run a runaway loop in the console, onStart, onUpdate, a task resumed after wait,
  a custom event and a cross-script function. Each must stop at the instruction
  limit, including after repeated callback execution. The editor must remain usable.
- Check memory-limit failure and recovery after Stop/Play. Do not rely on JIT code
  acceleration: tracing is disabled during instruction-budgeted execution.
- Verify io/os/package/require, ffi, jit, load/loadfile/loadstring/dofile,
  getfenv/setfenv, newproxy, debug introspection, pcall/xpcall and user coroutines
  are inaccessible. Engine debug.log/warn/error and task/wait APIs must still work.
- Verify fractional numbers and overflow are rejected for integer-only engine
  parameters; confirm 32-bit bit-library mask behavior and documented numeric
  precision limitations. Do not assume a separate Lua 5.4 64-bit integer subtype.

Desktop/runtime checks above have not been performed in the sandbox.
