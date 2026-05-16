# Skipped Files / Folders (Out of PCH Scope)

Files in these locations are **excluded** from the include-frequency analysis and are **not** referenced by `pch.h`.

## Excluded folders under `Source\Editor\UI\`

Per task constraint, only `Editor\UI\ContentBrowser\` is in scope. All other `Editor\UI\*` items are excluded:

| Path | Type |
|------|------|
| `Source\Editor\UI\EditorConsoleWidget.*` | Widget |
| `Source\Editor\UI\EditorControlWidget.*` | Widget |
| `Source\Editor\UI\EditorCurveWidget.*` | Widget |
| `Source\Editor\UI\EditorDragSource.*` | Widget |
| `Source\Editor\UI\EditorFileUtils.*` | Helper |
| `Source\Editor\UI\EditorMainPanel.*` | Widget |
| `Source\Editor\UI\EditorMaterialInspector.*` | Widget |
| `Source\Editor\UI\EditorPlayToolbarWidget.*` | Widget |
| `Source\Editor\UI\EditorProjectSettingsWidget.*` | Widget |
| `Source\Editor\UI\EditorPropertyWidget.*` | Widget |
| `Source\Editor\UI\EditorSceneWidget.*` | Widget |
| `Source\Editor\UI\EditorShadowMapDebugWidget.*` | Widget |
| `Source\Editor\UI\EditorSkeletalMeshViewerWidget.*` | Widget |
| `Source\Editor\UI\EditorStatWidget.*` | Widget |
| `Source\Editor\UI\EditorViewportWidget.*` | Widget |
| `Source\Editor\UI\EditorWidget.*` | Base widget |
| `Source\Editor\UI\ImGuiSetting.*` | Config |
| `Source\Editor\UI\NotificationToast.*` | Widget |

These widgets are still compiled into `KraftonEngine.vcxproj` — they just are not counted in PCH inclusion-frequency stats. Their `.cpp` files will still use PCH via the `/FI` ForcedInclude (no harm done — PCH symbols are a superset of what they need).

## Excluded source trees (entirely outside in-scope analysis)

| Path | Reason |
|------|--------|
| `Source\Games\Crossy\` | Belongs to `CrossyGame.vcxproj` (separate static library). PCH not applied. |
| `Source\ObjViewer\` | Compiled into `KraftonEngine.vcxproj` but not in the task's analysis target list. Will still receive `/FI pch.h` since it is part of the application — but its headers do not influence PCH content. |
| `ThirdParty\` | Third-party code; not in scope. Some third-party headers (e.g., DirectX) may still appear in PCH if 60%+ TU usage justifies it. |
| `ThirdParty\FBXSDK\samples\` | Already excluded by `ENGINE_EXCLUDE_PREFIXES` in the build script. |

## Stale / excluded files (already filtered by script)

The build script's `ENGINE_EXCLUDE_PREFIXES` (lines 107–129) and `OBSOLETE_FILES` (lines 133–148) already remove these from the `.vcxproj`. We do not include them in PCH analysis either:

```
Source\Engine\Runtime\ObjectPoolSystem.cpp/h
Source\Engine\Runtime\RowManager.cpp/h
Source\Engine\Component\Movement\HopMovementComponent.cpp/h
Source\Engine\Component\ParryComponent.cpp/h
Source\Engine\Scripting\LuaParryComponentBindings.cpp
Source\Engine\Scripting\LuaRowManagerBindings.cpp
Source\Engine\Scripting\LuaUiBindings.cpp
Source\Engine\UI\Game\GameUiSystem.cpp/h
```

## Conditional-include lines (counted but not PCH-eligible)

`#include` lines inside `#if/#ifdef/#ifndef` blocks are flagged in `include_frequency.csv` (`in_conditional=true`) and are excluded from PCH candidate consideration. They remain in their original source location.
