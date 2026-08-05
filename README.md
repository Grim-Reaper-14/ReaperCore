# ReaperCore

Clean Windows x64 C++20 foundation organized into strict subsystem folders.

## Source domains

- `core` owns lifecycle, files, folders, settings, logging, and Lua orchestration.
- `backend` owns the runtime service loop.
- `frontend` is reserved for the menu and UI files supplied later.
- `bootstrap` contains only the Windows DLL entry point.

See `docs/ARCHITECTURE.md` for the complete ownership rules.

## Build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output:

```text
build\bin\Release\ReaperCore.dll
```

Press `END` to request a clean unload.

Runtime data is created under:

```text
%APPDATA%\ReaperCore
```

The Lua interfaces are present, but no Lua engine dependency is linked yet.
