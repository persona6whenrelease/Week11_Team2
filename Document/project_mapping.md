# Project Mapping & Build Automation Analysis

## Repository Layout

```
C:\GitDirectory11\
├── KraftonEngine.sln                       # Single solution
├── GenerateProjectFiles.bat                # Wrapper for the python script
├── Scripts/
│   └── generateprojectfiles.py             # Auto-generates the two .vcxproj files
├── KraftonEngine/
│   ├── KraftonEngine.vcxproj               # AUTO-GENERATED — do not edit
│   ├── KraftonEngine.vcxproj.filters       # AUTO-GENERATED
│   ├── CrossyGame.vcxproj                  # AUTO-GENERATED
│   ├── CrossyGame.vcxproj.filters          # AUTO-GENERATED
│   ├── VcpkgLua.props                      # AUTO-GENERATED (by same script)
│   ├── ThirdParty/                         # SFML, FBXSDK, ImGui, Sol
│   ├── Shaders/
│   ├── main.cpp
│   └── Source/
│       ├── Editor/
│       ├── Engine/
│       ├── GameClient/
│       ├── Games/Crossy/                   # ← belongs to CrossyGame.vcxproj only
│       └── ObjViewer/
├── Document/                               # Analysis artifacts live here
└── vcpkg_installed/
```

## Two .vcxproj Targets — Critical Distinction

Both `.vcxproj` files live in the same folder (`C:\GitDirectory11\KraftonEngine\`):

| Project | Type | Scope | PCH? |
|---------|------|-------|------|
| `KraftonEngine.vcxproj` | Application (exe) | `Source\Engine`, `Source\Editor`, `Source\GameClient`, `Source\ObjViewer`, `ThirdParty`, root `main.cpp` | **YES** |
| `CrossyGame.vcxproj` | StaticLibrary (.lib) | `Source\Games\Crossy` only | **NO** |

Because both `.vcxproj` files share `KraftonEngine\` as their parent, any `Directory.Build.props` placed at `C:\GitDirectory11\` or `C:\GitDirectory11\KraftonEngine\` applies to **both** projects. Project-name conditional logic (`'$(MSBuildProjectName)' == 'KraftonEngine'`) is therefore mandatory.

## `generateprojectfiles.py` Mechanics

Path: `C:\GitDirectory11\Scripts\generateprojectfiles.py` (capital `S`; the task spec wrote `script/` lowercase).

### Generation method
- Python `xml.etree.ElementTree` builds the XML element tree directly. **Not template-based**, **not CMake**, **not Premake**.
- Output is written with explicit UTF-8 + CRLF (line 430 area).

### Function topology
- `generate_vcxproj(...)` (lines 517–765) — emits one `.vcxproj`. Called twice from `main()`:
  - Once for `KraftonEngine` (call site ~line 926)
  - Once for `CrossyGame` (call site ~line 960)
- `generate_filters(...)` and `generate_solution(...)` follow.

### Constants relevant to PCH

| Symbol | Line | Meaning |
|--------|------|---------|
| `PROJECT_NAME = "KraftonEngine"` | 30 | Drives `MSBuildProjectName` |
| `CROSSY_PROJECT_NAME = "CrossyGame"` | 31 | |
| `INCLUDE_PATHS` | 166–177 | Contains `Source` — `#include "pch.h"` resolves from any TU |
| `CROSSY_INCLUDE_PATHS` | 182–188 | Contains `Source` (but PCH disabled by condition) |
| `ENGINE_SCAN_DIRS = ["Source", "ThirdParty"]` | 106 | What gets pulled into KraftonEngine |
| `ENGINE_EXCLUDE_PREFIXES` | 107–129 | Crossy and a handful of stale files |
| `ROOT_FILES = ["main.cpp"]` | 162 | Picked up explicitly |

### PCH integration points (for Method 1 — script patch)

| Where | Line | Action |
|-------|------|--------|
| `generate_vcxproj` signature | 517 | Add optional `pch_header` and `pch_create_source` parameters |
| ClCompile `ItemDefinitionGroup` | 632–665 | If `pch_header`: append `PrecompiledHeader=Use`, `PrecompiledHeaderFile`, `ForcedIncludeFiles` |
| Items loop ClCompile entries | 705–717 | When file path matches `pch_create_source`, attach per-item override `PrecompiledHeader=Create` + empty `ForcedIncludeFiles` |
| KraftonEngine call site | ~926 | Pass `pch_header="pch.h", pch_create_source="Source\\pch.cpp"` |
| CrossyGame call site | ~960 | **Do not pass** — leaves CrossyGame untouched |

### DLL dependency injection (context, unchanged by PCH work)

- **VcpkgLua.props** (regenerated at lines 440–512 of the script) handles Lua + RmlUi imports for `x64` configurations only. Imported via `<Import>` element in lines 587–593.
- **SFML / FBXSDK** are injected via `<AdditionalDependencies>` (lines 675–696) + `<PostBuildEvent>` xcopy commands (lines 375–392) to copy `.dll` files into `$(OutDir)`.

PCH integration must not collide with these mechanisms. Since both `VcpkgLua.props` and `AdditionalDependencies` operate on **link** stage or **import groups**, and PCH operates on **ClCompile** metadata, there is no overlap.

## Configurations (7 total)

```
Debug|Win32, Release|Win32, Debug|x64, Release|x64,
ObjViewDebug|x64, Demo|x64, GameClient|x64
```

PCH applies uniformly across all 7 configurations because Directory.Build.props condition is on `MSBuildProjectName`, not on configuration. CrossyGame is linked into the executable only for `(Debug|x64, Release|x64, GameClient|x64)` per `CROSSY_LINK_CONFIGURATIONS` (line 65), but this does not affect PCH wiring.

## Why we choose Method 2 (Directory.Build.props)

1. **No script edits** — `generateprojectfiles.py` does not need to know about PCH. Future script updates do not need to merge with PCH logic.
2. **Auto-import** — MSBuild walks up from the `.vcxproj` directory and applies any `Directory.Build.props` it finds. No `<Import>` declaration needed in the generated XML.
3. **Surgical scope** — `MSBuildProjectName` conditional cleanly excludes CrossyGame.
4. **Easy to reverse** — delete the props file to roll back, or comment out the conditional content.

## Existing PCH artifacts: NONE

Grep confirms zero `pch.h`, `stdafx.h`, or `PrecompiledHeader` references in the live build tree. Only stale `stdafx.h` files exist inside `ThirdParty\FBXSDK\samples\`, which are excluded from the build via `ENGINE_EXCLUDE_PREFIXES`.
