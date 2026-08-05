# ReaperCore

Clean Windows x64 C++20 DLL foundation.

## Requirements

- Windows 10 or newer
- Visual Studio 2022 with Desktop development with C++
- CMake 3.24 or newer

## Build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output:

```text
build\bin\Release\ReaperCore.dll
```

## Runtime

When the DLL loads, it opens a console and writes a log to:

```text
%APPDATA%\ReaperCore\ReaperCore.log
```

Press `END` to request a clean unload.
