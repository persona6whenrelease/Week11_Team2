# 02 — 결합 지점 카테고리별 정리

분리 작업 시 반드시 손대야 할 지점만 카테고리별로 정리. 모든 인용은 `절대경로:라인번호` 기준.

---

## 2-A. 메인 빌드 시스템이 Crossy를 아는 곳

대상: `C:\GitDirectory11\Scripts\GenerateProjectFiles.py`

### A-1. 모듈 docstring (첫 14 라인)
| 라인 | 내용 | 일반화 가능성 |
|---|---|---|
| 7 | `CrossyGame.vcxproj -> static library target: Source/Games/Crossy only` | **일반화 가능** — 문서. 일반 게임 설명으로 교체 가능 |
| 9-10 | "The main project references CrossyGame only for GameClient|x64..." | **일반화 가능** — 문서 |

### A-2. Crossy 전용 모듈 상수 (라인 31~158)
| 라인 | 변수 | 절대 필요? |
|---|---|---|
| 31 | `CROSSY_PROJECT_NAME = "CrossyGame"` | **일반화 가능** — manifest에서 읽거나 `Source/Games/<Name>/` glob 결과로 대체 |
| 48 | `CROSSY_PROJECT_GUID = "{a6df3d49-...}"` | **일반화 가능** — manifest 파일(예: `Source/Games/<Name>/Game.toml`)로 이동 권장. GUID는 vcxproj가 안정 ID로 필요하므로 어딘가에는 보관 필요 |
| 50 | `CROSSY_ROOT_NAMESPACE = "CrossyGame"` | **일반화 가능** — 보통 `<Name>Game` 규칙으로 자동 생성 가능 |
| 65~69 | `CROSSY_LINK_CONFIGURATIONS = {("Debug","x64"),("Release","x64"),("GameClient","x64")}` | **일반화 가능** — 게임당 manifest 키로 이동. 실제로는 모든 게임이 동일한 set을 가질 가능성이 높음 |
| 92~103 | `CROSSY_CONFIG_PROPS = { ... is_game_client: True, extra_defines: ["STATS=0"] ... }` | **일반화 가능** — 게임 모듈은 모두 같은 빌드 옵션을 갖는 게 자연스러움. 공통 함수로 추출 가능 |
| 108 | `"Source\\Games\\Crossy"` (in `ENGINE_EXCLUDE_PREFIXES`) | **일반화 가능** — `glob("Source/Games/*")` 결과를 모두 exclude하면 됨 |
| 122~123 | `CROSSY_SCAN_DIRS = ["Source\\Games\\Crossy"]` | **일반화 가능** — 게임당 자기 경로 |
| 149~158 | `CROSSY_INCLUDE_PATHS = [...]` | **일반화 가능** — 게임 공통 INCLUDE 셋. 현재 엔진 공통 경로와 동일한 세트 |

### A-3. 빌드 매크로 주입 (라인 587)
```python
base_defs.append(f"WITH_CROSSY_GAME_MODULE={1 if (cfg, plat) in CROSSY_LINK_CONFIGURATIONS else 0}")
```
- **절대 필요(현 구조에서)** — 메인 vcxproj에 `WITH_CROSSY_GAME_MODULE=0/1` 매크로를 주입. 이 매크로는 `LinkedRuntimeModules.cpp:7,13` `#if`로 직접 사용됨.
- **일반화 가능 (구조 변경 시)** — 게임마다 `WITH_<NAME>_GAME_MODULE` 매크로 자동 생성하거나, 통합 매크로(예: `LINKED_GAMES="Crossy"`)로 대체. 단 LinkedRuntimeModules.cpp 구조 변경 동반.

### A-4. SLN 생성 로직 (라인 717~775, 특히 726, 735~736, 759~761)
| 라인 | 내용 | 일반화 가능성 |
|---|---|---|
| 726 | `crossy_guid = CROSSY_PROJECT_GUID.upper()` | **일반화 가능** — 게임 list 순회로 대체 |
| 735~736 | `Project(...) = "{CROSSY_PROJECT_NAME}", ...` 라인 추가 | **일반화 가능** — 게임 list 순회 |
| 759~761 | 각 cfg별 ProjectConfigurationPlatforms 항목 | **일반화 가능** — 게임 list 순회 |

### A-5. main() 안의 Crossy 전용 호출 (라인 823~892)
| 라인 | 내용 | 일반화 가능성 |
|---|---|---|
| 823~833 | `scan_files(scan_dirs=CROSSY_SCAN_DIRS, ...)` | **일반화 가능** — 게임 list 순회 |
| 851~855 | `ProjectReference(include="CrossyGame.vcxproj", guid=..., condition=...)` 메인 vcxproj에 추가 | **일반화 가능** — 게임 list 순회로 ProjectReference 여러 개 생성 |
| 870~883 | `generate_vcxproj(name=CROSSY_PROJECT_NAME, guid=..., files=crossy_files, ...)` 호출 | **일반화 가능** — 동일 함수를 게임 수만큼 반복 호출 |
| 886~892 | `generate_filters(crossy_files, project_name=CROSSY_PROJECT_NAME, ...)` | **일반화 가능** — 동일 |

**A 종합**: py 안의 Crossy 등장은 **거의 100% 일반화 가능**. `generate_vcxproj`/`generate_filters` 함수 자체는 이미 매개변수화되어 있음. 일반화 시 추가로 필요한 것: 게임 list (manifest 또는 디렉터리 glob), 게임당 GUID 보관처(현재는 py 상수).

---

## 2-B. 호스트 측 등록 코드

### B-1. `C:\GitDirectory11\KraftonEngine\Source\GameClient\LinkedRuntimeModules.cpp` ⭐ 핵심
파일 전체 17줄 — 그대로 인용:
```cpp
#include "GameClient/LinkedRuntimeModules.h"

#ifndef WITH_CROSSY_GAME_MODULE
#define WITH_CROSSY_GAME_MODULE IS_GAME_CLIENT
#endif

#if WITH_CROSSY_GAME_MODULE
#include "Games/Crossy/CrossyGameModule.h"
#endif

void RegisterLinkedRuntimeModules()
{
#if WITH_CROSSY_GAME_MODULE
	RegisterCrossyGameModule();
#endif
}
```

**호출처 2곳 (Editor와 GameClient 양쪽에서 호출됨)**:
- `C:\GitDirectory11\KraftonEngine\Source\GameClient\GameClientEngine.cpp:71` — `RegisterLinkedRuntimeModules();`
- `C:\GitDirectory11\KraftonEngine\Source\Editor\EditorEngine.cpp:59` — `RegisterLinkedRuntimeModules();`

함의: GameClient 빌드뿐 아니라 Debug|x64/Release|x64(에디터)에서도 Crossy가 등록됨. 매크로 상태(`WITH_CROSSY_GAME_MODULE=1`)가 결정.

**일반화 형태**: 게임 list을 받아 매크로별로 `Register<Name>GameModule()` 자동 호출하는 코드 생성. 또는 모듈 레지스트리 자체를 동적 로딩 기반으로 전환하면 이 파일 자체가 불필요해짐.

### B-2. 패키지 설정의 모듈 키 — `EditorPackageSettings.h`
| 라인 | 내용 |
|---|---|
| 21 | `FString SelectedGame = "Crossy";` (편집기 UI 기본 선택 게임) |
| 22 | `FString GameProjectPath = "CrossyGame.vcxproj";` |
| 43 | `TArray<FString> RuntimeModules = { "CrossyGame" };` (런타임 로드 모듈 키 — `FRuntimeModuleRegistry::Create()`에서 lookup 됨) |

→ 패키지 빌드 시 `ProjectSettings.json` 같은 산출물에 기록되어 `GameClientEngine.cpp:83 GetRuntimeModules().LoadModules(Settings.RuntimeModules)` 가 실제로 활성화함.

### B-3. `ProjectSettings.ini` — 현재 비활성
`C:\GitDirectory11\KraftonEngine\Settings\ProjectSettings.ini` 전체:
```json
{
  "Runtime": { "Modules": [] },
  "Shadow": { ... }
}
```
- **현 시점 Modules 배열은 비어있음**. 즉 ProjectSettings 경로로는 Crossy가 로드되지 않음. 활성 경로는 (a) 빌드 시간의 `WITH_CROSSY_GAME_MODULE` 분기 + `RegisterCrossyGameModule()` 정적 등록, (b) 패키징 시 EditorPackageSettings로부터 생성된 `Settings.RuntimeModules`.
- 따라서 `Settings/ProjectSettings.ini`의 Modules 키 자체는 분리 작업과 **결합 없음**. 그러나 분리 후 Crossy가 빠지면 `RuntimeModules`가 빈 채로도 동작하는지(=Crossy 없이 GameClient가 의미 있는지) 검증 필요.  `[확인 필요]`

### B-4. 패키징 게임 정의 — `EditorMainPanel.cpp:801`
```cpp
PackageGameDefinitions.push_back({ "Crossy", "CrossyGame", "CrossyGame.vcxproj" });
```
**중요**: 라인 780~796에서 `Source/Games/*` 디렉터리를 자동 스캔하여 `FEditorPackageGameDefinition`을 생성함. 라인 801의 하드코딩은 **스캔 결과가 비어있을 때만 사용되는 fallback**. 즉 `Source/Games/Crossy/` 디렉터리를 삭제하면 자동 스캔 결과가 비고, 이 fallback이 발동하므로 라인 801도 함께 수정 필요.

→ Editor UI는 이미 부분적으로 일반화되어 있고, 명명 규약(`<Name>Game`, `<Name>Game.vcxproj`)이 코드에 박혀 있음.

### B-5. 패키지 빌드 주석 — `GamePackageBuilder.cpp:430`
```cpp
// such as CrossyGame.vcxproj is only a dependency/module and must never be used as
```
주석에 Crossy 이름 등장. 의미 없는 결합. 분리 시 주석만 수정.

---

## 2-C. Source 트리 안의 Crossy 의존성

### C-1. `Source/Games/Crossy/` 외부에서 Crossy 헤더를 `#include`하는 파일
1단계 표 INCLUDE 분류 28건 중 **Source/Games/Crossy/ 외부에 있는 INCLUDE는 단 1건**:

| 파일 | 라인 | 라인 내용 |
|---|---|---|
| `KraftonEngine\Source\GameClient\LinkedRuntimeModules.cpp` | 8 | `#include "Games/Crossy/CrossyGameModule.h"` |

→ 외부에서 Crossy 헤더를 include 하는 곳은 **이 한 곳뿐**. 분리 작업의 결합도 측면에서는 매우 깔끔함.

### C-2. 다른 타겟이 Crossy 라이브러리를 참조하는 곳
| 파일 | 라인 | 내용 |
|---|---|---|
| `KraftonEngine\KraftonEngine.vcxproj` | 1282 | `<ProjectReference Include="CrossyGame.vcxproj" Condition="...Debug|x64' Or ...GameClient|x64' Or ...Release|x64'">` |

→ msbuild 레벨에서 메인 application 프로젝트가 CrossyGame.vcxproj에 `<ProjectReference>`로 의존. Condition은 Debug|x64, Release|x64, GameClient|x64 세 구성에서만 활성.

→ 이 외에 `target_link_libraries`나 다른 ProjectReference 경유는 없음 (CrossyGame.vcxproj.filters는 자기 자신 내부 분류이므로 무관).

**C 종합**: 외부 의존이 거의 0에 수렴. 헤더 1건, vcxproj 1건. 매우 깔끔.

---

## 2-D. 매크로 정의처

`WITH_CROSSY_GAME_MODULE`은 두 군데에서 정의됨:

### D-1. `KraftonEngine\KraftonEngine.vcxproj` (1차 정의처 — application project 컴파일에 박힘)
| 라인 | 구성 | 값 |
|---|---|---|
| 171 | Debug\|Win32 | `WITH_CROSSY_GAME_MODULE=0` |
| 188 | Release\|Win32 | `WITH_CROSSY_GAME_MODULE=0` |
| 203 | Debug\|x64 | `WITH_CROSSY_GAME_MODULE=1` |
| 228 | Release\|x64 | `WITH_CROSSY_GAME_MODULE=1` |
| 253 | ObjViewDebug\|x64 | `WITH_CROSSY_GAME_MODULE=0` |
| 278 | Demo\|x64 | `WITH_CROSSY_GAME_MODULE=0` |
| 303 | GameClient\|x64 | `WITH_CROSSY_GAME_MODULE=1` |

→ `CROSSY_LINK_CONFIGURATIONS` 집합과 정확히 일치(Debug|x64, Release|x64, GameClient|x64에서만 1).

### D-2. `Scripts\GenerateProjectFiles.py:587` (생성기 — vcxproj 라인을 이렇게 채움)
```python
base_defs.append(f"WITH_CROSSY_GAME_MODULE={1 if (cfg, plat) in CROSSY_LINK_CONFIGURATIONS else 0}")
```
- 조건문: `if application_project:` 블록 안에서만(라인 586) 매크로를 추가 → CrossyGame 자체 vcxproj에는 이 매크로가 들어가지 않음. 메인 vcxproj에만 들어감. ✓ 합리적.

### D-3. `LinkedRuntimeModules.cpp:3-5` (방어적 fallback)
```cpp
#ifndef WITH_CROSSY_GAME_MODULE
#define WITH_CROSSY_GAME_MODULE IS_GAME_CLIENT
#endif
```
→ vcxproj에서 정의가 안 와도 `IS_GAME_CLIENT` 매크로 값을 그대로 사용. 즉 GameClient 빌드면 1, 아니면 0.

**D 종합**: 매크로 정의처는 **명확**. 분리 시 D-1(7건), D-2(1건), D-3(3건)을 모두 손대거나, 매크로 자체를 제거하는 방식 중 선택.

---

## 2-E. LuaScript와의 결합

### E-1. Crossy → LuaScript (정상 방향)
| 결합 | 위치 |
|---|---|
| Crossy가 Lua 바인딩 등록 | `CrossyGameModule.cpp:38` `FLuaScriptSubsystem::Get().AddBindingRegistrar(&RegisterCrossyLuaBindings);` |
| Crossy가 Lua 스크립트 디렉터리 워처 등록 | `CrossyGameModule.cpp:46` `FLuaScriptSubsystem::Get().RegisterScriptDirectoryWatcher("Game/");` |
| Crossy의 모든 Lua* 바인딩 파일 | `Source/Games/Crossy/Scripting/CrossyLua*.cpp` (7개 파일, 모두 sol2 + 엔진의 `FLuaScriptSubsystem` 사용) |

### E-2. LuaScript → Crossy (역방향, 발견 시 심각)
- `FLuaScriptSubsystem`(엔진 측)이 Crossy를 직접 아는 곳: **0건** (Grep으로 확인). `AddBindingRegistrar`는 generic 함수 포인터를 받음.
- `FLuaScriptSubsystem.h/cpp` 내 Crossy 문자열: **0건**.
- 따라서 LuaScript 인터페이스는 **방향이 깔끔** — Crossy → LuaScript 단방향만 존재. 분리 시 LuaScript 코드 변경 불필요.

### E-3. Lua 스크립트(.lua) 측 결합
- `KraftonEngine\LuaScripts\Game\RowGenerator.lua:97` 한국어 주석 1줄에만 "Crossy" 언급. 코드 자체는 Crossy 키워드 없음.
- `KraftonEngine/LuaScripts/Game/` 안의 모든 .lua 파일(GameManager, Player, MapManager 등)은 Crossy 명칭 없이 generic 게임 로직. 그러나 게임플레이 의미상 Crossy 게임 전용 (예: ScoreTrigger, RestartButton 등 — Crossy식 게임 흐름).

→ **Lua 스크립트 자산은 Crossy를 직접 참조하지 않으나, 의미적으로 Crossy 게임 전용**. 분리 시 함께 옮길지/남길지는 별도 판단 필요. `[확인 필요]` (Lua 스크립트가 다른 게임에서 재활용 가능한지)

### E-4. Asset 자산
- `KraftonEngine\Asset\Sound\` 안의 `BackgroundMusic.wav`, `Crash.wav`, `Dash.wav`, `Death.wav`, `Jump.wav`, `Parry.wav` — Crossy 게임플레이용. 이름엔 Crossy 없음.
- `KraftonEngine\Asset\UI\` 안의 `Intro.rml`, `HUD.rml`, `GameOver.rml`, `PauseMenu.rml` 등 — Crossy UI용. 파일명엔 Crossy 없음.
- `CrossyGameModule.cpp:113-118`이 이 자산들을 직접 로드(`Sound/BackgroundMusic.wav` 등) 하므로, Crossy 분리 시 자산도 같이 옮겨야 동작. `[확인 필요]` (자산을 같이 옮길 것인지 메인이 가질 것인지)

---

## 결합 지점 요약

| 카테고리 | 결합 강도 | 대상 파일 수 | 주요 메커니즘 |
|---|---|---|---|
| 2-A 빌드 시스템 (py) | **강** | 1 (py 단일 파일에 ~45건) | 상수 + 함수 호출 + sln 생성 |
| 2-B 호스트 등록 | **중** | 4 (LinkedRuntimeModules.cpp + EditorPackageSettings.h + EditorMainPanel.cpp + GamePackageBuilder.cpp) | 매크로 + 정적 등록 + UI fallback |
| 2-C 외부 참조 | **약** | 2 (LinkedRuntimeModules.cpp INCLUDE 1건 + KraftonEngine.vcxproj ProjectReference 1건) | 헤더 1건, msbuild ref 1건 |
| 2-D 매크로 정의 | **명확** | 3 (vcxproj 7라인 + py 1라인 + cpp fallback) | `WITH_CROSSY_GAME_MODULE` |
| 2-E Lua 인터페이스 | **단방향** | 0 (엔진→Crossy 역방향 결합 없음) | `AddBindingRegistrar` generic |

**주의 지점**:
- B-3 (`ProjectSettings.ini` Modules 비어있음)이 의미하는 바는 `[확인 필요]`. 현재 빈 배열로도 GameClient가 동작한다면, RuntimeModules 로딩 경로는 **유효 사용처가 EditorPackageSettings(패키징 시 산출물 작성)뿐**일 가능성이 큼.
- E-3, E-4 (Lua 스크립트와 자산)은 텍스트상 Crossy를 모르지만 **의미적으로 Crossy 전용**. 분리 결정 시 함께 옮길지 결정 필요.
- B-4 EditorMainPanel.cpp의 자동 스캔 로직 — `Source/Games/*` 디렉터리를 자동 발견. 분리 후 디렉터리 삭제 시 fallback 라인 801도 같이 정리해야 함.
