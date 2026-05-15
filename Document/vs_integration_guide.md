# Visual Studio Integration Guide — KraftonEngine PCH

This guide describes how to apply the precompiled header (`pch.h`/`pch.cpp`) to `KraftonEngine.vcxproj` **without** ever editing the auto-generated `.vcxproj` directly. Two methods are provided. **Method 2 is the recommended path.**

---

## TL;DR for the maintainer

1. Pull the changes — `Directory.Build.props` **and** `Directory.Build.targets` are at the repo root, `pch.h`/`pch.cpp` are at `KraftonEngine/Source/`.
2. Run `GenerateProjectFiles.bat` (regenerates `.vcxproj`; does NOT change anything related to PCH because the script is unmodified).
3. Open the solution and build. MSBuild auto-imports both files and the PCH wiring activates.

Nothing else is required for end-users.

> **Why two files?** `Directory.Build.props` is auto-imported *before* the `.vcxproj` body, while `Directory.Build.targets` is imported *after*. The default PCH metadata (apply `/Yu` to every TU) lives in props. The per-file override that flips `pch.cpp` to `/Yc` MUST live in targets, because `<ClCompile Update="...">` can only modify items that already exist — and `pch.cpp` only exists as an item once the project body has been evaluated. See "Troubleshooting" below.

---

## Background

- `KraftonEngine.vcxproj` and `CrossyGame.vcxproj` are produced by `Scripts/generateprojectfiles.py`.
- Both `.vcxproj` files live in `KraftonEngine\` (same directory).
- Direct edits to either `.vcxproj` are **lost on regeneration**.
- PCH must therefore be applied via either (a) script modification, or (b) a `.props` file that MSBuild imports automatically.

---

## Method 2 — Directory.Build.props + Directory.Build.targets (RECOMMENDED)

**Why this is the recommended method:**

- The build script does not need to know about PCH.
- Future updates to `generateprojectfiles.py` cannot accidentally break the PCH wiring.
- MSBuild auto-imports both files from the closest ancestor directory of each `.vcxproj` — no `<Import>` declaration needed in the generated XML.
- The scope can be limited to a single project via the `$(MSBuildProjectName)` condition.

**File locations:**

| File | Location | Imported |
|------|----------|----------|
| `Directory.Build.props` | `C:\GitDirectory11\Directory.Build.props` | **Before** the .vcxproj body |
| `Directory.Build.targets` | `C:\GitDirectory11\Directory.Build.targets` | **After** the .vcxproj body |

Backup copies are kept in `Document/patches/Directory.Build.props.example` and `Document/patches/Directory.Build.targets.example`.

**How it works:**

### Step 1 — `Directory.Build.props` (defaults, runs first)

```xml
<ItemDefinitionGroup Condition="'$(MSBuildProjectName)' == 'KraftonEngine'">
  <ClCompile>
    <PrecompiledHeader>Use</PrecompiledHeader>
    <PrecompiledHeaderFile>pch.h</PrecompiledHeaderFile>
    <ForcedIncludeFiles>pch.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>
  </ClCompile>
</ItemDefinitionGroup>
```

`ItemDefinitionGroup` sets **defaults** for items that will be defined later. Every ClCompile item in the project — including `pch.cpp` itself, initially — receives these defaults. So all TUs get `/Yu` + `/FI pch.h`.

### Step 2 — `Directory.Build.targets` (per-file override, runs last)

```xml
<ItemGroup Condition="'$(MSBuildProjectName)' == 'KraftonEngine'">
  <ClCompile Update="$(MSBuildThisFileDirectory)KraftonEngine\Source\pch.cpp">
    <PrecompiledHeader>Create</PrecompiledHeader>
    <ForcedIncludeFiles></ForcedIncludeFiles>
  </ClCompile>
</ItemGroup>
```

By the time `Directory.Build.targets` is imported, the project body has been evaluated and `pch.cpp` exists as a ClCompile item. The `Update=` value is the absolute path to `pch.cpp`; MSBuild normalizes both sides for matching, so it resolves against the project's relative-path item `Source\pch.cpp` correctly.

> Earlier drafts used `Update="@(ClCompile)" Condition="'%(Filename)%(Extension)' == 'pch.cpp'"` to match by filename. MSBuild rejects this with "built-in metadata `Filename` cannot be referenced at position 1" because the `%()` metadata syntax is not supported in an `Update`-element Condition. Direct path Update= sidesteps that limitation.

For the matched `pch.cpp` item:
- `PrecompiledHeader = Create` → flips this single TU to `/Yc` (it builds the PCH binary)
- `ForcedIncludeFiles = ""` → empty value clears the inherited ForcedInclude, preventing pch.cpp from `/FI pch.h`-ing itself (which would be a self-include cycle)

### Step 3 — CrossyGame is excluded

`CrossyGame.vcxproj` sits in the same `KraftonEngine\` folder, so MSBuild walks up and finds the same two files. The `Condition="'$(MSBuildProjectName)' == 'KraftonEngine'"` guard ensures both `ItemDefinitionGroup` and the `ItemGroup` are skipped when building CrossyGame — it gets no PCH metadata at all.

**Why two files instead of one?**

`Directory.Build.props` is imported **before** the project body. At that point no ClCompile items exist yet, so `<ClCompile Update="...">` placed in props is a silent no-op. The override must run **after** the project body — that's `Directory.Build.targets`. Putting both in one file was the first attempt and produced the "cannot open precompiled header file" build error, because pch.cpp inherited `/Yu` and never created the `.pch` binary.

**Trade-offs:**

| Pro | Con |
|-----|-----|
| Zero edits to the build script | Two-file mental model (props + targets + script) |
| Survives script regeneration | Slightly less discoverable than a script change |
| Easy to disable (rename either file) | Requires understanding of MSBuild auto-import order |

---

## Method 1 — Patch `generateprojectfiles.py`

**Use this only if you prefer the build configuration to live in one place (the script).**

**Patch file:** `Document/patches/generateprojectfiles_pch_patch.py.diff`

**Summary of changes (3 sites):**

1. `generate_vcxproj()` signature — add two optional kwargs:
   ```python
   pch_header: str | None = None,
   pch_create_source: str | None = None,
   ```
2. ClCompile ItemDefinitionGroup loop — emit `PrecompiledHeader=Use`, `PrecompiledHeaderFile`, and `ForcedIncludeFiles` when `pch_header` is set.
3. Items loop — when emitting the ClCompile entry for `pch.cpp`, override metadata to `PrecompiledHeader=Create` and clear `ForcedIncludeFiles`.

**Call site changes:**

| Project | Change |
|---------|--------|
| KraftonEngine generation (call ~line 938) | Add `pch_header="pch.h", pch_create_source="Source\\pch.cpp"` |
| CrossyGame generation (call ~line 981) | No change (do not pass PCH kwargs) |

**Apply the patch:**

```cmd
cd C:\GitDirectory11
git apply Document\patches\generateprojectfiles_pch_patch.py.diff
```

(or apply by hand — the diff annotates the exact insertion points)

**Trade-offs:**

| Pro | Con |
|-----|-----|
| Single source of truth for build config | Script becomes harder to merge with upstream changes |
| All PCH metadata appears in the generated .vcxproj XML | A bad PCH refactor breaks every regen |
| `Directory.Build.props` not needed | Mixing PCH and project generation responsibilities |

---

## Conflict between the two methods

**Pick one, not both.** Activating both causes:
- Duplicate ClCompile metadata in the project (harmless redundancy, but confusing).
- Potential `Update=` vs. inline-metadata conflicts on `pch.cpp`.

If you have committed to Method 1, delete the repo-root `Directory.Build.props` (or rename it to a `.disabled` suffix) before regenerating.

---

## Existing source cleanup

### Manual `#include "pch.h"` lines — NOT REQUIRED

Because both methods use `ForcedIncludeFiles` (`/FI pch.h`), **none of the ~233 in-scope `.cpp` files need editing.** The compiler injects `pch.h` automatically. New `.cpp` files added in the future will also receive PCH automatically.

### Duplicate include cleanup — OPTIONAL, NOT RECOMMENDED

`.cpp` files already containing `#include <vector>`, `#include "Engine/Core/CoreTypes.h"`, etc., will see those headers twice (once via `/FI pch.h`, once via the original include). MSVC handles this with the include guard / `#pragma once` — there is **no semantic or measurable performance issue**.

Removing duplicates in bulk would:
- Produce massive git churn (~hundreds of files)
- Make future merges painful
- Yield negligible compile-time benefit (PCH already de-duplicates the parse)

**Recommendation:** leave existing includes in place. Only remove them opportunistically when you happen to be editing a file for other reasons.

### `/FI` vs. manual `#include` trade-off

| | Manual `#include "pch.h"` | `/FI pch.h` (chosen) |
|--|---------------------------|----------------------|
| Edits required | All 233 `.cpp` files | None |
| New-file safety | Easy to forget; build will fail | Automatic |
| Visibility | Explicit dependency | Hidden in build config |
| Compatibility | Universal (works without props/script) | MSVC-specific feature |

We chose `/FI` because the edit cost of the manual approach is high and the visibility cost is negligible — `pch.h` is documented at `KraftonEngine/Source/pch.h` with a clear header comment explaining its scope.

---

## `/Yc` vs `/Yu` enforcement

- `pch.cpp` (the *only* file with `/Yc`) builds the PCH binary (`*.pch`)
- Every other `.cpp` in the project uses `/Yu` and includes `pch.h` as the first compiled content (via `/FI`)

This matches the standard MSVC PCH contract. Both Method 1 and Method 2 emit the same final ClCompile metadata; they only differ in **where** the metadata is authored.

---

## Estimated build-time impact

**Important caveat: these are estimates, not measurements.** Always benchmark before-and-after on the actual machine.

### Per-TU savings (parse time avoided)

| PCH content | Estimated single-parse cost (MSVC, /utf-8, x64) |
|-------------|------------------------------------------------|
| `<Windows.h>` (with WIN32_LEAN_AND_MEAN + NOMINMAX) | ~220 ms |
| `<DirectXMath.h>` (via Math aggregator → Vector.h) | ~90 ms |
| Full STL set via CoreTypes (`<vector>` `<unordered_map>` `<list>` `<queue>` `<array>` `<string>` `<utility>` ...) | ~120 ms |
| `<algorithm>` | ~60 ms |
| Math suite (Vector/Matrix/Quat/Rotator/Transform/MathUtils) | ~40 ms |
| **Total per-TU savings** | **~530 ms** (estimate) |

### Total wall-clock estimate

- 233 TUs × ~530 ms ≈ 123 seconds of compiler work avoided
- With MSBuild `MultiProcessorCompilation` (already enabled in script, line 663) on an 8-core machine: ~15 seconds wall-clock saved per clean build
- Plus reduced incremental rebuild times (PCH is reused across the incremental loop)

### Calculation formula (for re-estimation)

```
savings ≈ (TU_count - 1) × mean_pch_parse_time_per_TU / parallel_jobs
```

Numbers above are MSVC v143 reference estimates for similar-sized engines. **Real-world results may differ by 30-50%** depending on disk, RAM, antivirus exclusions, and SSD/HDD.

### Cost of PCH invalidation

Any change to `pch.h` (or any header it transitively includes through `CoreTypes.h` / `Math.h`) invalidates the PCH and triggers a full ~233-TU rebuild. This is precisely why the C category is conservatively sized — every header added to PCH increases the **probability** of triggering a full rebuild during day-to-day work.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Build error `cannot open precompiled header file: ...\KraftonEngine.pch` across many TUs | `pch.cpp` did not get `/Yc`, so the `.pch` binary was never created. Most common cause: `<ClCompile Update="...pch.cpp">` was placed in `Directory.Build.props` instead of `Directory.Build.targets`, so it ran before the project's items existed and silently did nothing | Confirm there are **two** files at the repo root — `Directory.Build.props` (ItemDefinitionGroup only) and `Directory.Build.targets` (ItemGroup with the Update). Verify in VS: right-click `pch.cpp` → Properties → C/C++ → Precompiled Headers should show `Create (/Yc)`. If it shows `Use`, the Update did not apply. |
| `error C1853: '...pch' is not a precompiled header file created with this compiler` | Stale `.pch` from old run | Clean rebuild |
| `fatal error C1083: Cannot open include file: 'pch.h'` | Include path missing `Source` | Verify `Source` is in `INCLUDE_PATHS` (line 168 of script) — it is by default |
| CrossyGame complains about missing pch.h | Condition slipped on Method 1 patch | Verify Method 2 condition `'$(MSBuildProjectName)' == 'KraftonEngine'` is intact in **both** props and targets, or that Method 1 patch only passes `pch_header` to the KraftonEngine call site |
| Long compile of a single file | That file's PCH metadata was overridden to `NotUsing` | Inspect `.vcxproj` per-file metadata after generation; if a file (e.g., a `.c` C source) needs to skip PCH, add an explicit `Update=` rule in `Directory.Build.targets` |
| pch.cpp itself fails with `cannot open precompiled header file` | pch.cpp is receiving `/Yu` instead of `/Yc` (same root cause as the multi-TU symptom above). Or the inherited `ForcedIncludeFiles` for pch.cpp was not cleared, causing self-recursion | Same fix — confirm `Directory.Build.targets` exists and that pch.cpp's metadata override clears `ForcedIncludeFiles` |
| `built-in metadata "Filename" cannot be referenced at position 1` from `Directory.Build.targets` | `%(Filename)` / `%(Extension)` used in a Condition on `<ClCompile Update="@(...)" ...>` — MSBuild does not allow built-in metadata in that position | Replace the metadata Condition with an explicit path Update, e.g. `Update="$(MSBuildThisFileDirectory)KraftonEngine\Source\pch.cpp"`. MSBuild matches via normalized full path. |
