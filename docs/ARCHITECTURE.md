# ReaperCore architecture

Every subsystem belongs to a dedicated folder in both `include` and `src`.

```text
include/reapercore/
  core/
    application/
    core/
    file_system/
    folder_system/
    logging/
    lua/
    settings_system/
  backend/
  frontend/

src/
  bootstrap/
  core/
    application/
    core/
    file_system/
    folder_system/
    logging/
    lua/
    settings_system/
  backend/
  frontend/
```

## Ownership

`Core` owns all managers and controls startup and shutdown order.

```text
Application
  -> Core
       -> Folder_System_Manager
       -> File_System_Manager
       -> Settings_System_Manager
       -> Logging_Manager
       -> ReaperCore_Lua_System
            -> LuaCore_Manager
       -> Backend
```

`Backend` may use core services but never owns them. `Frontend` is reserved exclusively for menus, rendering, themes, layouts, and UI input. Core and backend code must not depend on frontend code.

## Lua

`ReaperCore_Lua_System` is the subsystem boundary. `LuaCore_Manager` discovers and manages scripts. A concrete implementation of `Lua_Runtime_Interface` will later connect the project to the selected Lua library without leaking that dependency into the rest of ReaperCore.
