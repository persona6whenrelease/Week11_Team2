# PCH Header Categorization

**Data source:** `Document/include_frequency.csv` (233 TUs scanned across in-scope folders; 288 distinct headers).

## Calibration note

The plan's original A-criterion (50%+ TU usage) was unreachable: **no header in the scan reaches even 30%**. The top direct-include header (`<algorithm>`) sits at 26.6%, and the most-used project header (`Object/ObjectFactory.h`) at 21.5%. This is normal for an engine codebase where the SSOT pattern routes most STL/math content through `CoreTypes.h` and the new `Math.h` aggregator — direct counts are low because **transitive coverage** does the heavy lifting.

Recalibrated thresholds (for this codebase):

| Category | Recalibrated criterion |
|----------|------------------------|
| A | System/platform header with ≥10% direct + heavy parse cost, OR transitive workhorse (Windows.h) |
| B | Already covered by `CoreTypes.h` or `Math.h` aggregator |
| C | 5–15% TU usage, OR engine-foundation with rec ≤ 1 and broad transitive impact |
| D | Game logic, render scene-specific, frequently-changing modules, third-party SDK headers (FBX/Lua), editor widget implementation headers |

Single-project sensitivity (constraint #5): when in doubt → C or D.

---

## A — Direct in PCH (commit)

| Header | TU% direct | Rationale |
|--------|-----------|-----------|
| `<Windows.h>` | 1.7% direct; ~26 files in-scope include it directly or via .h | Transitively dragged in by `Engine.h`, `Paths.h`, `WindowsWindow.h`, `Stats.h`, `InputSystem.h`, etc. Heavy SDK header. WIN32_LEAN_AND_MEAN + NOMINMAX make it manageable. Stable. |
| `<algorithm>` | 26.6% | Highest direct STL usage; not in CoreTypes.h; stable. |

## B — Via aggregator (zero-touch)

These are **already covered** by `CoreTypes.h` or the new `Engine/Math/Math.h`. The PCH includes them with a single aggregator line each.

| Header | Aggregator | Note |
|--------|-----------|------|
| `<stdint.h>` | CoreTypes | |
| `<cassert>` | CoreTypes | |
| `<vector>` | CoreTypes | backs TArray |
| `<list>` | CoreTypes | backs TLinkedList |
| `<unordered_set>` | CoreTypes | backs TSet |
| `<unordered_map>` | CoreTypes | backs TMap |
| `<queue>` | CoreTypes | backs TQueue |
| `<array>` | CoreTypes | backs TStaticArray |
| `<string>` | CoreTypes | backs FString |
| `<utility>` | CoreTypes | backs TPair |
| `<cmath>` | Math (Vector, Matrix, MathUtils, Quat, Rotator all #include it) | |
| `<cstring>` | Math (Matrix.h) | |
| `<DirectXMath.h>` | Math (Vector.h) | Heavy SDK header — significant savings |
| `Engine/Math/Vector.h` | Math aggregator | |
| `Engine/Math/Matrix.h` | Math aggregator | |
| `Engine/Math/MathUtils.h` | Math aggregator | |
| `Engine/Math/Quat.h` | Math aggregator | |
| `Engine/Math/Rotator.h` | Math aggregator | |
| `Engine/Math/Transform.h` | Math aggregator | |

## C — Review required (commented in pch.h, awaiting user opt-in)

These are strong candidates but cross conservative thresholds for a single-project PCH. They are placed in `pch.h` as **commented-out lines** with rationale; user toggles them on by decision.

| Header | TU% | rec_6mo | Rationale / risk |
|--------|-----|---------|------|
| `<filesystem>` | 9.4% | n/a | Very heavy parse. Many file-IO sites. Risk: future C++23 changes. **Recommend opt-in.** |
| `<fstream>` | 7.3% | n/a | Heavy. Often paired with filesystem. |
| `Core/Log.h` | 18.0% | 1 | Used everywhere. But Log infrastructure tends to evolve (format/sinks). Adding to PCH means logging API changes trigger full rebuild. **Recommend opt-in only after logging API is stable.** |
| `Engine/Platform/Paths.h` (+ `Platform/Paths.h` alias) | 12.4% combined | 1 | Foundation utility. Includes Windows.h. Already partially covered if Windows.h is in PCH; opt-in adds path utilities. |
| `Serialization/Archive.h` | 13.7% | 1 | Archive interface; rarely changes in mature engines but here `rec=1` indicates active work. **Hold for review.** |
| `<d3d11.h>` | 2.1% direct | n/a | Used by render core; heavy. Already transitively present. Direct PCH add saves render-area builds but ties PCH to DirectX 11 (migration risk). **Recommend opt-in if no D3D12 migration planned.** |

C-category lines in `pch.h` are wrapped in clearly labeled section blocks. The user can uncomment after weighing the rebuild risk.

## D — Excluded (do not put in PCH)

### Game-logic-adjacent (high churn / domain-specific)
- `Object/ObjectFactory.h` (50 TU) — Object reflection/factory; touches every gameplay class
- `Object/Object.h` (12 TU)
- `GameFramework/AActor.h` (46), `World.h` (44), `PlayerController.h` (17), `Pawn.h` (10)
- `Component/CameraComponent.h` (25), `ActorComponent.h` (17), `SceneComponent.h` (14), `StaticMeshComponent.h` (14), `PrimitiveComponent.h` (13), `BillboardComponent.h` (10), `MovementComponent.h` (9), `SpringArmComponent.h` (5), `TextRenderComponent.h` (4)
- All `Component/Collision/*.h`
- All `Component/Script/*.h`

### Rendering pipeline (frequently iterated)
- `Materials/Material.h` (21), `MaterialManager.h` (19)
- `Render/Scene/FScene.h` (15), `Render/Shader/ShaderManager.h` (15), `Render/Types/FrameContext.h` (15), `Render/Types/RenderConstants.h` (8), `Render/Types/GlobalLightParams.h` (5), `Render/Types/LightFrustumUtils.h` (5)
- `Render/Pipeline/Renderer.h` (5), `Render/Device/D3DDevice.h` (11)
- `Render/Proxy/PrimitiveSceneProxy.h` (9), `Render/Command/DrawCommandList.h` (7), `Render/Resource/MeshBufferManager.h` (8)
- `RenderPassRegistry.h` (14)
- `Texture/Texture2D.h` (11), `Mesh/MeshManager.h` (8)
- `Profiling/Stats.h` (10), `Profiling/StartupProfiler.h`, `Profiling/PlatformTime.h`

### Third-party SDKs (heavy and used only in narrow paths)
- `fbxsdk.h` (7) — only Editor packaging / asset import
- `SolInclude.h` (17), `LuaBindings.h` (15), `LuaHandles.h` (14), `LuaBindingHelper.h` (11), `LuaPropertyBridge.h` (5), `LuaScriptSubsystem.h`, `LuaWorldLibrary.h` and other Lua bindings
- `SimpleJSON/json.hpp` (13)

### Heavy STL / niche
- `<iostream>` (5 TU) — heavy, low usage, can stay local
- `<chrono>` (4 TU) — low usage
- `<cctype>`, `<cfloat>`, `<cstdlib>` — small enough not to matter

### Editor widgets (out of scope by constraint #4)
All `Editor/UI/*.h` except those inside `ContentBrowser/` — already excluded from analysis per `skipped_files.md`.

---

## Final PCH content summary

After applying A and B (with placeholders for C), the PCH will contain:

```
Platform:
  <Windows.h>             (A)

Aggregators:
  Engine/Core/CoreTypes.h  → 10 STL headers (B)
  Engine/Math/Math.h       → 6 math headers + <cmath>, <cstring>, <DirectXMath.h> (B)

Extra STL:
  <algorithm>              (A)

[Commented C-category slots]
  <filesystem>
  <fstream>
  Core/Log.h
  Engine/Platform/Paths.h

GameClient slot:
  (empty — placeholder comment)
```

This conservative selection yields **two aggregators + two direct system headers (Windows.h, algorithm)** as the active payload, with four optional opt-ins. Total active includes from PCH: ~19 system/SDK headers (resolved via CoreTypes + Math) + Windows.h + algorithm = 21 lines of parse-once.

## Sensitivity note

Engine/Editor/GameClient share one `.vcxproj`. **Any change to PCH triggers a full ~233-TU rebuild.** This is why C category is intentionally large: when in doubt, leave it commented so the user can opt in after observing build-time stability over a few weeks.
