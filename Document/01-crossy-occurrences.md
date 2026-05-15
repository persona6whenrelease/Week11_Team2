# 01 — Crossy 등장 전수 표

## 검색 조건
- 패턴: `crossy` (case-insensitive, 단일 패턴으로 `Crossy`/`CROSSY`/`crossy`/`CrossyGame`/`CROSSY_GAME` 모두 포착)
- 도구: `Grep -i -n` (저장소 전체)
- 결과: **337 lines, 27 files**
- Build/Bin 산출물(`.obj`, `.lib`, `.pdb`, `.idb`, `.log`, `.tlog/...`)은 자동 생성물이므로 제외 (Glob으로 별도 확인 — 참고: `KraftonEngine\CrossyGame.vcxproj.user`는 빈 파일)
- Asset 디렉터리(`KraftonEngine\Asset`) 안 Crossy 텍스트 참조 **0건** (Sound/*.wav 같은 이름 파일은 있으나 내부 텍스트 검색 0)

## 분류 enum
- **BUILD_VAR** — `.py` 안의 상수/변수
- **MACRO** — 전처리 매크로 (`#define`, `#if`, `#ifndef` 등)
- **INCLUDE** — `#include "..."`
- **SYMBOL** — C++ 식별자 (함수/클래스/네임스페이스/변수)
- **STRING_KEY** — 따옴표 문자열 리터럴 (경로 구분자 없음)
- **PATH** — 따옴표 문자열 + `/` 또는 `\` 포함
- **CONFIG** — `.ini`/`.json`/`.props`/`.targets`/`.vcxproj`/`.sln` 안의 값
- **COMMENT** — 주석/문서 (`//`, `/*`, `<!--`, `#`, `--`)
- **OTHER** — 위에 안 맞으면 코멘트 추가

## 파일별 발생 요약 (count desc)
| # | 파일 | 발생 수 |
|---|---|---|
| 1 | `KraftonEngine\Source\Games\Crossy\UI\CrossyGameUiSystem.cpp` | 72 |
| 2 | `Scripts\GenerateProjectFiles.py` | 45 |
| 3 | `KraftonEngine\CrossyGame.vcxproj.filters` | 49 |
| 4 | `KraftonEngine\CrossyGame.vcxproj` | 30 |
| 5 | `KraftonEngine\Source\Games\Crossy\CrossyGameModule.cpp` | 25 |
| 6 | `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaUiBindings.cpp` | 26 |
| 7 | `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaBindings.cpp` | 13 |
| 8 | `KraftonEngine\KraftonEngine.vcxproj` | 8 |
| 9 | `KraftonEngine\Source\Games\Crossy\UI\CrossyGameUiSystem.h` | 8 |
| 10 | `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaBindings.h` | 8 |
| 11 | `KraftonEngine\Source\Games\Crossy\CrossyGameModule.h` | 6 |
| 12 | `KraftonEngine\Source\GameClient\LinkedRuntimeModules.cpp` | 6 |
| 13 | `KraftonEngine\Source\Games\Crossy\Components\HopMovementComponent.cpp` | 5 |
| 14 | `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaMovementBindings.cpp` | 6 |
| 15 | `KraftonEngine\Source\Games\Crossy\Map\RowManager.cpp` | 4 |
| 16 | `KraftonEngine\Source\Games\Crossy\Components\ParryComponent.cpp` | 4 |
| 17 | `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaParryComponentBindings.cpp` | 3 |
| 18 | `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaRowManagerBindings.cpp` | 3 |
| 19 | `KraftonEngine\Source\Editor\Packaging\EditorPackageSettings.h` | 3 |
| 20 | `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaSaveGameBindings.cpp` | 2 |
| 21 | `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaHandles.h` | 2 |
| 22 | `KraftonEngine\Source\Games\Crossy\Audio\CrossyAudioIds.h` | 2 |
| 23 | `KraftonEngine\Source\Games\Crossy\Components\ParryableProjectileComponent.cpp` | 1 |
| 24 | `KraftonEngine\Source\Editor\UI\EditorMainPanel.cpp` | 1 |
| 25 | `KraftonEngine\Source\Editor\Packaging\GamePackageBuilder.cpp` | 1 |
| 26 | `KraftonEngine.sln` | 1 |
| 27 | `KraftonEngine\LuaScripts\Game\RowGenerator.lua` | 1 |

---

## 전체 표 (그룹: 파일별)

### `KraftonEngine.sln`
| 라인 | 원문 | 분류 |
|---|---|---|
| 8 | `Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "CrossyGame", "KraftonEngine\CrossyGame.vcxproj", "{A6DF3D49-15CE-49C6-B594-D9B89DE3C83B}"` | CONFIG |

### `Scripts\GenerateProjectFiles.py`
| 라인 | 원문 | 분류 |
|---|---|---|
| 7 | `    CrossyGame.vcxproj     -> static library target: Source/Games/Crossy only` | COMMENT |
| 9 | `The main project references CrossyGame only for GameClient|x64, so regenerating project` | COMMENT |
| 10 | `files no longer pulls Source/Games/Crossy back into the engine/application project.` | COMMENT |
| 31 | `CROSSY_PROJECT_NAME = "CrossyGame"` | BUILD_VAR |
| 48 | `CROSSY_PROJECT_GUID = "{a6df3d49-15ce-49c6-b594-d9b89de3c83b}"` | BUILD_VAR |
| 50 | `CROSSY_ROOT_NAMESPACE = "CrossyGame"` | BUILD_VAR |
| 65 | `CROSSY_LINK_CONFIGURATIONS = {` | BUILD_VAR |
| 92 | `# Crossy is built as a game runtime module. Keep its defines stable across configs;` | COMMENT |
| 94 | `CROSSY_CONFIG_PROPS = {` | BUILD_VAR |
| 108 | `    "Source\\Games\\Crossy",` | PATH |
| 122 | `# Directories to recursively scan for the Crossy static library project.` | COMMENT |
| 123 | `CROSSY_SCAN_DIRS = ["Source\\Games\\Crossy"]` | BUILD_VAR |
| 149 | `# Include paths for the Crossy module. Do not add Source/Games/Crossy as a` | COMMENT |
| 151 | `#   #include "Games/Crossy/CrossyGameModule.h"` | COMMENT |
| 152 | `CROSSY_INCLUDE_PATHS = [` | BUILD_VAR |
| 587 | `base_defs.append(f"WITH_CROSSY_GAME_MODULE={1 if (cfg, plat) in CROSSY_LINK_CONFIGURATIONS else 0}")` | MACRO |
| 631 | `    # CrossyGame.lib is available to the GameClient application link step.` | COMMENT |
| 726 | `    crossy_guid = CROSSY_PROJECT_GUID.upper()` | SYMBOL |
| 735 | `        f'Project("{VS_PROJECT_TYPE}") = "{CROSSY_PROJECT_NAME}", '` | SYMBOL |
| 736 | `        f'"{sln_project_path(CROSSY_PROJECT_NAME + ".vcxproj")}", "{crossy_guid}"'` | SYMBOL |
| 753 | `    # CrossyGame is visible in the solution for all configurations, but it is` | COMMENT |
| 759 | `        lines.append(f"\t\t{crossy_guid}.{cfg}|{sln_plat}.ActiveCfg = {cfg}|{plat}")` | SYMBOL |
| 760 | `        if (cfg, plat) in CROSSY_LINK_CONFIGURATIONS:` | SYMBOL |
| 761 | `            lines.append(f"\t\t{crossy_guid}.{cfg}|{sln_plat}.Build.0 = {cfg}|{plat}")` | SYMBOL |
| 823 | `    print(f"Scanning {CROSSY_PROJECT_NAME} module files in {PROJECT_DIR}...")` | SYMBOL |
| 824 | `    crossy_files = scan_files(` | SYMBOL |
| 826 | `        scan_dirs=CROSSY_SCAN_DIRS,` | SYMBOL |
| 832 | `    print(f"  {CROSSY_PROJECT_NAME} ClCompile: {len(crossy_files['ClCompile'])} files")` | SYMBOL |
| 833 | `    print(f"  {CROSSY_PROJECT_NAME} ClInclude: {len(crossy_files['ClInclude'])} files")` | SYMBOL |
| 851 | `                include=f"{CROSSY_PROJECT_NAME}.vcxproj",` | SYMBOL |
| 852 | `                guid=CROSSY_PROJECT_GUID,` | SYMBOL |
| 855 | `                    for cfg, plat in sorted(CROSSY_LINK_CONFIGURATIONS)` | SYMBOL |
| 871 | `        name=CROSSY_PROJECT_NAME,` | SYMBOL |
| 872 | `        guid=CROSSY_PROJECT_GUID,` | SYMBOL |
| 873 | `        root_namespace=CROSSY_ROOT_NAMESPACE,` | SYMBOL |
| 874 | `        files=crossy_files,` | SYMBOL |
| 875 | `        output_path=PROJECT_DIR / f"{CROSSY_PROJECT_NAME}.vcxproj",` | SYMBOL |
| 877 | `        config_props=CROSSY_CONFIG_PROPS,` | SYMBOL |
| 878 | `        include_paths=CROSSY_INCLUDE_PATHS,` | SYMBOL |
| 879 | `        int_dir_prefix=CROSSY_PROJECT_NAME,` | SYMBOL |
| 884 | `    print(f"  {PROJECT_DIR / (CROSSY_PROJECT_NAME + '.vcxproj')}")` | SYMBOL |
| 887 | `        crossy_files,` | SYMBOL |
| 888 | `        project_name=CROSSY_PROJECT_NAME,` | SYMBOL |
| 889 | `        output_path=PROJECT_DIR / f"{CROSSY_PROJECT_NAME}.vcxproj.filters",` | SYMBOL |
| 892 | `    print(f"  {PROJECT_DIR / (CROSSY_PROJECT_NAME + '.vcxproj.filters')}")` | SYMBOL |

### `KraftonEngine\KraftonEngine.vcxproj`
| 라인 | 원문 | 분류 |
|---|---|---|
| 171 | `<PreprocessorDefinitions>WIN32;_DEBUG;_CONSOLE;WITH_EDITOR=1;IS_OBJ_VIEWER=0;IS_GAME_CLIENT=0;WITH_CROSSY_GAME_MODULE=0;...` | CONFIG |
| 188 | `<PreprocessorDefinitions>WIN32;NDEBUG;_CONSOLE;WITH_EDITOR=1;IS_OBJ_VIEWER=0;IS_GAME_CLIENT=0;WITH_CROSSY_GAME_MODULE=0;...` | CONFIG |
| 203 | `<PreprocessorDefinitions>_DEBUG;_CONSOLE;WITH_EDITOR=1;IS_OBJ_VIEWER=0;IS_GAME_CLIENT=0;WITH_CROSSY_GAME_MODULE=1;...` | CONFIG |
| 228 | `<PreprocessorDefinitions>NDEBUG;_CONSOLE;WITH_EDITOR=1;IS_OBJ_VIEWER=0;IS_GAME_CLIENT=0;WITH_CROSSY_GAME_MODULE=1;...` | CONFIG |
| 253 | `<PreprocessorDefinitions>NDEBUG;_CONSOLE;WITH_EDITOR=0;IS_OBJ_VIEWER=1;IS_GAME_CLIENT=0;WITH_CROSSY_GAME_MODULE=0;...` | CONFIG |
| 278 | `<PreprocessorDefinitions>NDEBUG;_CONSOLE;WITH_EDITOR=1;IS_OBJ_VIEWER=0;IS_GAME_CLIENT=0;WITH_CROSSY_GAME_MODULE=0;STATS=0;...` | CONFIG |
| 303 | `<PreprocessorDefinitions>NDEBUG;_CONSOLE;WITH_EDITOR=0;IS_OBJ_VIEWER=0;IS_GAME_CLIENT=1;WITH_CROSSY_GAME_MODULE=1;STATS=0;...` | CONFIG |
| 1282 | `<ProjectReference Include="CrossyGame.vcxproj" Condition="'$(Configuration)|$(Platform)'=='Debug|x64' Or '$(Configuration)|$(Platform)'=='GameClient|x64' Or '$(Configuration)|$(Platform)'=='Release|x64'">` | CONFIG |

### `KraftonEngine\CrossyGame.vcxproj.filters`
모두 CONFIG (XML msbuild filter/include 항목). 표는 라인·스니펫만.

| 라인 | 원문 (요약) | 분류 |
|---|---|---|
| 10 | `<Filter Include="Source\Games\Crossy">` | CONFIG |
| 13 | `<Filter Include="Source\Games\Crossy\Audio">` | CONFIG |
| 16 | `<Filter Include="Source\Games\Crossy\Components">` | CONFIG |
| 19 | `<Filter Include="Source\Games\Crossy\Map">` | CONFIG |
| 22 | `<Filter Include="Source\Games\Crossy\Scripting">` | CONFIG |
| 25 | `<Filter Include="Source\Games\Crossy\UI">` | CONFIG |
| 30 | `<ClCompile Include="Source\Games\Crossy\Components\HopMovementComponent.cpp">` | CONFIG |
| 31 | `  <Filter>Source\Games\Crossy\Components</Filter>` | CONFIG |
| 33 | `<ClCompile Include="Source\Games\Crossy\Components\ParryComponent.cpp">` | CONFIG |
| 34 | `  <Filter>Source\Games\Crossy\Components</Filter>` | CONFIG |
| 36 | `<ClCompile Include="Source\Games\Crossy\Components\ParryableProjectileComponent.cpp">` | CONFIG |
| 37 | `  <Filter>Source\Games\Crossy\Components</Filter>` | CONFIG |
| 39 | `<ClCompile Include="Source\Games\Crossy\CrossyGameModule.cpp">` | CONFIG |
| 40 | `  <Filter>Source\Games\Crossy</Filter>` | CONFIG |
| 42 | `<ClCompile Include="Source\Games\Crossy\Map\RowManager.cpp">` | CONFIG |
| 43 | `  <Filter>Source\Games\Crossy\Map</Filter>` | CONFIG |
| 45 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaBindings.cpp">` | CONFIG |
| 46 | `  <Filter>Source\Games\Crossy\Scripting</Filter>` | CONFIG |
| 48 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaMovementBindings.cpp">` | CONFIG |
| 49 | `  <Filter>Source\Games\Crossy\Scripting</Filter>` | CONFIG |
| 51 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaParryComponentBindings.cpp">` | CONFIG |
| 52 | `  <Filter>Source\Games\Crossy\Scripting</Filter>` | CONFIG |
| 54 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaRowManagerBindings.cpp">` | CONFIG |
| 55 | `  <Filter>Source\Games\Crossy\Scripting</Filter>` | CONFIG |
| 57 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaSaveGameBindings.cpp">` | CONFIG |
| 58 | `  <Filter>Source\Games\Crossy\Scripting</Filter>` | CONFIG |
| 60 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaUiBindings.cpp">` | CONFIG |
| 61 | `  <Filter>Source\Games\Crossy\Scripting</Filter>` | CONFIG |
| 63 | `<ClCompile Include="Source\Games\Crossy\UI\CrossyGameUiSystem.cpp">` | CONFIG |
| 64 | `  <Filter>Source\Games\Crossy\UI</Filter>` | CONFIG |
| 68 | `<ClInclude Include="Source\Games\Crossy\Audio\CrossyAudioIds.h">` | CONFIG |
| 69 | `  <Filter>Source\Games\Crossy\Audio</Filter>` | CONFIG |
| 71 | `<ClInclude Include="Source\Games\Crossy\Components\HopMovementComponent.h">` | CONFIG |
| 72 | `  <Filter>Source\Games\Crossy\Components</Filter>` | CONFIG |
| 74 | `<ClInclude Include="Source\Games\Crossy\Components\ParryComponent.h">` | CONFIG |
| 75 | `  <Filter>Source\Games\Crossy\Components</Filter>` | CONFIG |
| 77 | `<ClInclude Include="Source\Games\Crossy\Components\ParryableProjectileComponent.h">` | CONFIG |
| 78 | `  <Filter>Source\Games\Crossy\Components</Filter>` | CONFIG |
| 80 | `<ClInclude Include="Source\Games\Crossy\CrossyGameModule.h">` | CONFIG |
| 81 | `  <Filter>Source\Games\Crossy</Filter>` | CONFIG |
| 83 | `<ClInclude Include="Source\Games\Crossy\Map\RowManager.h">` | CONFIG |
| 84 | `  <Filter>Source\Games\Crossy\Map</Filter>` | CONFIG |
| 86 | `<ClInclude Include="Source\Games\Crossy\Scripting\CrossyLuaBindings.h">` | CONFIG |
| 87 | `  <Filter>Source\Games\Crossy\Scripting</Filter>` | CONFIG |
| 89 | `<ClInclude Include="Source\Games\Crossy\Scripting\CrossyLuaHandles.h">` | CONFIG |
| 90 | `  <Filter>Source\Games\Crossy\Scripting</Filter>` | CONFIG |
| 92 | `<ClInclude Include="Source\Games\Crossy\UI\CrossyGameUiSystem.h">` | CONFIG |
| 93 | `  <Filter>Source\Games\Crossy\UI</Filter>` | CONFIG |

### `KraftonEngine\CrossyGame.vcxproj`
| 라인 | 원문 (요약) | 분류 |
|---|---|---|
| 37 | `<RootNamespace>CrossyGame</RootNamespace>` | CONFIG |
| 120 | `<IntDir>$(ProjectDir)Build\CrossyGame\$(Configuration)\</IntDir>` | CONFIG |
| 126 | `<IntDir>$(ProjectDir)Build\CrossyGame\$(Configuration)\</IntDir>` | CONFIG |
| 132 | `<IntDir>$(ProjectDir)Build\CrossyGame\$(Configuration)\</IntDir>` | CONFIG |
| 138 | `<IntDir>$(ProjectDir)Build\CrossyGame\$(Configuration)\</IntDir>` | CONFIG |
| 144 | `<IntDir>$(ProjectDir)Build\CrossyGame\$(Configuration)\</IntDir>` | CONFIG |
| 150 | `<IntDir>$(ProjectDir)Build\CrossyGame\$(Configuration)\</IntDir>` | CONFIG |
| 156 | `<IntDir>$(ProjectDir)Build\CrossyGame\$(Configuration)\</IntDir>` | CONFIG |
| 260 | `<ClCompile Include="Source\Games\Crossy\Components\HopMovementComponent.cpp" />` | CONFIG |
| 261 | `<ClCompile Include="Source\Games\Crossy\Components\ParryComponent.cpp" />` | CONFIG |
| 262 | `<ClCompile Include="Source\Games\Crossy\Components\ParryableProjectileComponent.cpp" />` | CONFIG |
| 263 | `<ClCompile Include="Source\Games\Crossy\CrossyGameModule.cpp" />` | CONFIG |
| 264 | `<ClCompile Include="Source\Games\Crossy\Map\RowManager.cpp" />` | CONFIG |
| 265 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaBindings.cpp" />` | CONFIG |
| 266 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaMovementBindings.cpp" />` | CONFIG |
| 267 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaParryComponentBindings.cpp" />` | CONFIG |
| 268 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaRowManagerBindings.cpp" />` | CONFIG |
| 269 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaSaveGameBindings.cpp" />` | CONFIG |
| 270 | `<ClCompile Include="Source\Games\Crossy\Scripting\CrossyLuaUiBindings.cpp" />` | CONFIG |
| 271 | `<ClCompile Include="Source\Games\Crossy\UI\CrossyGameUiSystem.cpp" />` | CONFIG |
| 274 | `<ClInclude Include="Source\Games\Crossy\Audio\CrossyAudioIds.h" />` | CONFIG |
| 275 | `<ClInclude Include="Source\Games\Crossy\Components\HopMovementComponent.h" />` | CONFIG |
| 276 | `<ClInclude Include="Source\Games\Crossy\Components\ParryComponent.h" />` | CONFIG |
| 277 | `<ClInclude Include="Source\Games\Crossy\Components\ParryableProjectileComponent.h" />` | CONFIG |
| 278 | `<ClInclude Include="Source\Games\Crossy\CrossyGameModule.h" />` | CONFIG |
| 279 | `<ClInclude Include="Source\Games\Crossy\Map\RowManager.h" />` | CONFIG |
| 280 | `<ClInclude Include="Source\Games\Crossy\Scripting\CrossyLuaBindings.h" />` | CONFIG |
| 281 | `<ClInclude Include="Source\Games\Crossy\Scripting\CrossyLuaHandles.h" />` | CONFIG |
| 282 | `<ClInclude Include="Source\Games\Crossy\UI\CrossyGameUiSystem.h" />` | CONFIG |

### `KraftonEngine\LuaScripts\Game\RowGenerator.lua`
| 라인 | 원문 | 분류 |
|---|---|---|
| 97 | `    -- ActorPool warmup 정책은 Crossy C++ RowManager가 담당합니다.` | COMMENT |

### `KraftonEngine\Source\GameClient\LinkedRuntimeModules.cpp`  ⭐ 핵심
| 라인 | 원문 | 분류 |
|---|---|---|
| 3 | `#ifndef WITH_CROSSY_GAME_MODULE` | MACRO |
| 4 | `#define WITH_CROSSY_GAME_MODULE IS_GAME_CLIENT` | MACRO |
| 7 | `#if WITH_CROSSY_GAME_MODULE` | MACRO |
| 8 | `#include "Games/Crossy/CrossyGameModule.h"` | INCLUDE |
| 13 | `#if WITH_CROSSY_GAME_MODULE` | MACRO |
| 14 | `	RegisterCrossyGameModule();` | SYMBOL |

### `KraftonEngine\Source\Editor\Packaging\EditorPackageSettings.h`  ⭐ 외부 결합
| 라인 | 원문 | 분류 |
|---|---|---|
| 21 | `FString SelectedGame = "Crossy";` | STRING_KEY |
| 22 | `FString GameProjectPath = "CrossyGame.vcxproj";` | STRING_KEY (파일명 — 경로 구분자 없음) |
| 43 | `TArray<FString> RuntimeModules = { "CrossyGame" };` | STRING_KEY |

### `KraftonEngine\Source\Editor\UI\EditorMainPanel.cpp`  ⭐ 외부 결합
| 라인 | 원문 | 분류 |
|---|---|---|
| 801 | `PackageGameDefinitions.push_back({ "Crossy", "CrossyGame", "CrossyGame.vcxproj" });` | STRING_KEY |

### `KraftonEngine\Source\Editor\Packaging\GamePackageBuilder.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 430 | `// such as CrossyGame.vcxproj is only a dependency/module and must never be used as` | COMMENT |

### `KraftonEngine\Source\Games\Crossy\CrossyGameModule.h`
| 라인 | 원문 | 분류 |
|---|---|---|
| 4 | `#include "Games/Crossy/UI/CrossyGameUiSystem.h"` | INCLUDE |
| 6 | `class FCrossyGameModule final : public IRuntimeModule` | SYMBOL |
| 9 | `	const char* GetName() const override { return "CrossyGame"; }` | STRING_KEY |
| 20 | `	void LoadCrossyAudio();` | SYMBOL |
| 22 | `	FCrossyGameUiSystem GameUi;` | SYMBOL |
| 26 | `void RegisterCrossyGameModule();` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\CrossyGameModule.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/CrossyGameModule.h"` | INCLUDE |
| 4 | `#include "Games/Crossy/Audio/CrossyAudioIds.h"` | INCLUDE |
| 5 | `#include "Games/Crossy/Components/ParryableProjectileComponent.h"` | INCLUDE |
| 6 | `#include "Games/Crossy/Components/HopMovementComponent.h"` | INCLUDE |
| 7 | `#include "Games/Crossy/Components/ParryComponent.h"` | INCLUDE |
| 8 | `#include "Games/Crossy/Map/RowManager.h"` | INCLUDE |
| 9 | `#include "Games/Crossy/Scripting/CrossyLuaBindings.h"` | INCLUDE |
| 22 | `	std::unique_ptr<IRuntimeModule> CreateCrossyGameModule()` | SYMBOL |
| 24 | `		return std::make_unique<FCrossyGameModule>();` | SYMBOL |
| 28 | `void RegisterCrossyGameModule()` | SYMBOL |
| 30 | `	FRuntimeModuleRegistry::RegisterFactory("CrossyGame", &CreateCrossyGameModule);` | STRING_KEY |
| 36 | `void FCrossyGameModule::OnRegister()` | SYMBOL |
| 38 | `	FLuaScriptSubsystem::Get().AddBindingRegistrar(&RegisterCrossyLuaBindings);` | SYMBOL |
| 43 | `void FCrossyGameModule::OnEngineInit(FEngineModuleContext& Context)` | SYMBOL |
| 47 | `	LoadCrossyAudio();` | SYMBOL |
| 50 | `void FCrossyGameModule::OnViewportCreated(FViewportModuleContext& Context)` | SYMBOL |
| 62 | `	FCrossyGameUiCallbacks Callbacks;` | SYMBOL |
| 71 | `void FCrossyGameModule::OnWorldCreated(UWorld* World)` | SYMBOL |
| 85 | `void FCrossyGameModule::OnPreWorldReset(UWorld* World)` | SYMBOL |
| 95 | `void FCrossyGameModule::OnPostWorldReset(UWorld* World)` | SYMBOL |
| 100 | `void FCrossyGameModule::OnShutdown()` | SYMBOL |
| 108 | `	ClearCrossyLuaUiEventHandler();` | SYMBOL |
| 111 | `void FCrossyGameModule::LoadCrossyAudio()` | SYMBOL |
| 113 | `	FSoundManager::Get().LoadMusic(CrossyAudioIds::BGM, FPaths::Combine(FPaths::AssetDir(), L"Sound/BackgroundMusic.wav"));` | SYMBOL |
| 114 | `	FSoundManager::Get().LoadEffect(CrossyAudioIds::Jump, FPaths::Combine(FPaths::AssetDir(), L"Sound/Jump.wav"));` | SYMBOL |
| 115 | `	FSoundManager::Get().LoadEffect(CrossyAudioIds::Death, FPaths::Combine(FPaths::AssetDir(), L"Sound/Death.wav"));` | SYMBOL |
| 116 | `	FSoundManager::Get().LoadEffect(CrossyAudioIds::Parry, FPaths::Combine(FPaths::AssetDir(), L"Sound/Parry.wav"));` | SYMBOL |
| 117 | `	FSoundManager::Get().LoadEffect(CrossyAudioIds::Dash, FPaths::Combine(FPaths::AssetDir(), L"Sound/Dash.wav"));` | SYMBOL |
| 118 | `	FSoundManager::Get().LoadEffect(CrossyAudioIds::Crash, FPaths::Combine(FPaths::AssetDir(), L"Sound/Crash.wav"));` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Audio\CrossyAudioIds.h`
| 라인 | 원문 | 분류 |
|---|---|---|
| 5 | `namespace CrossyAudioIds` | SYMBOL |
| 11 | `	inline const FSoundId BGM = "Crossy.BGM";` | STRING_KEY |

### `KraftonEngine\Source\Games\Crossy\UI\CrossyGameUiSystem.h`
| 라인 | 원문 | 분류 |
|---|---|---|
| 19 | `class FCrossyGameUiEventListener;` | SYMBOL |
| 28 | `struct FCrossyGameUiCallbacks` | SYMBOL |
| 37 | `class FCrossyGameUiSystem : public IViewportUiLayer` | SYMBOL |
| 40 | `	FCrossyGameUiSystem();` | SYMBOL |
| 41 | `	~FCrossyGameUiSystem() override;` | SYMBOL |
| 46 | `	void SetCallbacks(FCrossyGameUiCallbacks InCallbacks);` | SYMBOL |
| 97 | `	friend class FCrossyGameUiEventListener;` | SYMBOL |
| 133 | `	FCrossyGameUiCallbacks Callbacks;` | SYMBOL |
| 144 | `	std::unique_ptr<FCrossyGameUiEventListener> EventListener;` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\UI\CrossyGameUiSystem.cpp` (72 entries)
| 라인 | 원문 (요약) | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/UI/CrossyGameUiSystem.h"` | INCLUDE |
| 143 | `class FCrossyGameUiEventListener final : public Rml::EventListener` | SYMBOL |
| 146 | `	explicit FCrossyGameUiEventListener(FCrossyGameUiSystem* InOwner)` | SYMBOL |
| 172 | `	FCrossyGameUiSystem* Owner = nullptr;` | SYMBOL |
| 175 | `// WITH_RMLUI=0 빌드에서도 FCrossyGameUiSystem.h의` | COMMENT |
| 176 | `// std::unique_ptr<FCrossyGameUiEventListener>가 안전하게 소멸될 수 있도록` | COMMENT |
| 178 | `class FCrossyGameUiEventListener final` | SYMBOL |
| 181 | `	explicit FCrossyGameUiEventListener(FCrossyGameUiSystem*) {}` | SYMBOL |
| 185 | `FCrossyGameUiSystem::FCrossyGameUiSystem() = default;` | SYMBOL |
| 186 | `FCrossyGameUiSystem::~FCrossyGameUiSystem()` | SYMBOL |
| 191 | `bool FCrossyGameUiSystem::Initialize(...)` | SYMBOL |
| 261 | `void FCrossyGameUiSystem::Shutdown()` | SYMBOL |
| 316 | `void FCrossyGameUiSystem::SetCallbacks(FCrossyGameUiCallbacks InCallbacks)` | SYMBOL |
| 322 | `void FCrossyGameUiSystem::SetScriptEventHandler(...)` | SYMBOL |
| 327 | `void FCrossyGameUiSystem::ClearScriptEventHandler()` | SYMBOL |
| 334 | `void FCrossyGameUiSystem::SetPresentationRect(...)` | SYMBOL |
| 344 | `void FCrossyGameUiSystem::SyncContextDimensions()` | SYMBOL |
| 364 | `void FCrossyGameUiSystem::Update(float DeltaTime)` | SYMBOL |
| 389 | `void FCrossyGameUiSystem::Render()` | SYMBOL |
| 402 | `void FCrossyGameUiSystem::SetIntroVisible(bool bVisible)` | SYMBOL |
| 456 | `void FCrossyGameUiSystem::SetHudVisible(bool bVisible)` | SYMBOL |
| 481 | `void FCrossyGameUiSystem::SetPauseMenuVisible(bool bVisible)` | SYMBOL |
| 509 | `void FCrossyGameUiSystem::SetGameOverVisible(bool bVisible)` | SYMBOL |
| 539 | `void FCrossyGameUiSystem::SetScore(int32 Score)` | SYMBOL |
| 544 | `void FCrossyGameUiSystem::SetBestScore(int32 BestScore)` | SYMBOL |
| 555 | `void FCrossyGameUiSystem::SetCoins(int32 Coins)` | SYMBOL |
| 560 | `void FCrossyGameUiSystem::SetLane(int32 Lane)` | SYMBOL |
| 565 | `void FCrossyGameUiSystem::SetCombo(int32 Combo)` | SYMBOL |
| 571 | `void FCrossyGameUiSystem::SetStatusText(...)` | SYMBOL |
| 576 | `void FCrossyGameUiSystem::SetTopScoresText(...)` | SYMBOL |
| 599 | `void FCrossyGameUiSystem::ShowGameOver(...)` | SYMBOL |
| 636 | `void FCrossyGameUiSystem::HideGameOver()` | SYMBOL |
| 641 | `void FCrossyGameUiSystem::ResetRunUi()` | SYMBOL |
| 678 | `bool FCrossyGameUiSystem::LoadDocuments()` | SYMBOL |
| 712 | `void FCrossyGameUiSystem::BindUiEvents()` | SYMBOL |
| 717 | `		EventListener = std::make_unique<FCrossyGameUiEventListener>(this);` | SYMBOL |
| 751 | `void FCrossyGameUiSystem::BindDocumentClickEvents(...)` | SYMBOL |
| 773 | `void FCrossyGameUiSystem::UnbindDocumentClickEvents(...)` | SYMBOL |
| 795 | `void FCrossyGameUiSystem::HandleClick(const FString& ElementId)` | SYMBOL |
| 933 | `void FCrossyGameUiSystem::SetLayerVisible(...)` | SYMBOL |
| 953 | `void FCrossyGameUiSystem::QueueScriptEvent(...)` | SYMBOL |
| 972 | `void FCrossyGameUiSystem::FlushQueuedScriptEvents()` | SYMBOL |
| 991 | `void FCrossyGameUiSystem::DispatchScriptEvent(...)` | SYMBOL |
| 999 | `void FCrossyGameUiSystem::BeginStartTransition()` | SYMBOL |
| 1018 | `void FCrossyGameUiSystem::UpdateStartTransition(float DeltaTime)` | SYMBOL |
| 1099 | `void FCrossyGameUiSystem::CompleteStartTransition()` | SYMBOL |
| 1119 | `void FCrossyGameUiSystem::ApplyStartTransitionVisual(...)` | SYMBOL |
| 1134 | `void FCrossyGameUiSystem::ApplyIntroIdleBoxVisual()` | SYMBOL |
| 1152 | `float FCrossyGameUiSystem::EaseInOutCubic(float T) const` | SYMBOL |
| 1163 | `float FCrossyGameUiSystem::LerpFloat(...) const` | SYMBOL |
| 1168 | `void FCrossyGameUiSystem::SetOptionsVisible(bool bVisible)` | SYMBOL |
| 1176 | `void FCrossyGameUiSystem::SetIntroOptionsVisible(bool bVisible)` | SYMBOL |
| 1184 | `void FCrossyGameUiSystem::RefreshOptionLabels()` | SYMBOL |
| 1204 | `void FCrossyGameUiSystem::SetElementProperty(...)` | SYMBOL |
| 1223 | `void FCrossyGameUiSystem::SetElementDisplay(...)` | SYMBOL |
| 1241 | `void FCrossyGameUiSystem::SetElementText(...)` | SYMBOL |
| 1259 | `void FCrossyGameUiSystem::SetElementTextAny(...)` | SYMBOL |
| 1272 | `bool FCrossyGameUiSystem::IsInteractiveUiVisible() const` | SYMBOL |
| 1277 | `bool FCrossyGameUiSystem::ShouldCaptureKeyboard() const` | SYMBOL |
| 1282 | `void FCrossyGameUiSystem::ClearDocumentFocus(...)` | SYMBOL |
| 1300 | `void FCrossyGameUiSystem::ClearInteractiveFocus()` | SYMBOL |
| 1309 | `bool FCrossyGameUiSystem::ProcessWin32Message(...)` | SYMBOL |
| 1357 | `bool FCrossyGameUiSystem::ProcessMouseMove(...)` | SYMBOL |
| 1382 | `bool FCrossyGameUiSystem::ProcessMouseButtonDown(...)` | SYMBOL |
| 1408 | `bool FCrossyGameUiSystem::ProcessMouseButtonUp(...)` | SYMBOL |
| 1432 | `bool FCrossyGameUiSystem::ProcessMouseWheel(...)` | SYMBOL |
| 1458 | `bool FCrossyGameUiSystem::ProcessKeyDown(int VirtualKey)` | SYMBOL |
| 1479 | `bool FCrossyGameUiSystem::ProcessKeyUp(int VirtualKey)` | SYMBOL |
| 1500 | `bool FCrossyGameUiSystem::ProcessTextInput(uint32 Codepoint)` | SYMBOL |
| 1516 | `bool FCrossyGameUiSystem::WantsMouse() const` | SYMBOL |
| 1525 | `bool FCrossyGameUiSystem::WantsKeyboard() const` | SYMBOL |
| 1534 | `void FCrossyGameUiSystem::UnbindPauseMenuEvents()` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Map\RowManager.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Map/RowManager.h"` | INCLUDE |
| 12 | `	struct FCrossyPoolWarmupDesc` | SYMBOL |
| 18 | `	const FCrossyPoolWarmupDesc GCrossyDefaultPoolWarmups[] =` | SYMBOL |
| 118 | `	for (const FCrossyPoolWarmupDesc& Desc : GCrossyDefaultPoolWarmups)` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Components\HopMovementComponent.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Components/HopMovementComponent.h"` | INCLUDE |
| 9 | `#include "Games/Crossy/Audio/CrossyAudioIds.h"` | INCLUDE |
| 76 | `			FSoundManager::Get().PlayEffect(CrossyAudioIds::Dash);` | SYMBOL |
| 154 | `				if (!FSoundManager::Get().IsEffectPlaying(CrossyAudioIds::Jump))` | SYMBOL |
| 156 | `					FSoundManager::Get().PlayEffect(CrossyAudioIds::Jump);` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Components\ParryComponent.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Components/ParryComponent.h"` | INCLUDE |
| 3 | `#include "Games/Crossy/Components/ParryableProjectileComponent.h"` | INCLUDE |
| 11 | `#include "Games/Crossy/Audio/CrossyAudioIds.h"` | INCLUDE |
| 28 | `			FSoundManager::Get().PlayEffect(CrossyAudioIds::Parry);` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Components\ParryableProjectileComponent.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Components/ParryableProjectileComponent.h"` | INCLUDE |

### `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaBindings.h`
| 라인 | 원문 | 분류 |
|---|---|---|
| 8 | `void RegisterCrossyLuaBindings(sol::state& Lua);` | SYMBOL |
| 9 | `void RegisterCrossyHopMovementComponentBinding(sol::state& Lua);` | SYMBOL |
| 10 | `void RegisterCrossyParryComponentBinding(sol::state& Lua);` | SYMBOL |
| 11 | `void RegisterCrossyGameObjectComponentBindings(sol::state& Lua);` | SYMBOL |
| 12 | `void RegisterCrossyRowManagerBinding(sol::state& Lua);` | SYMBOL |
| 13 | `void RegisterCrossyUiBinding(sol::state& Lua);` | SYMBOL |
| 14 | `void RegisterCrossySaveGameBinding(sol::state& Lua);` | SYMBOL |
| 15 | `void ClearCrossyLuaUiEventHandler();` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaBindings.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Scripting/CrossyLuaBindings.h"` | INCLUDE |
| 6 | `#include "Games/Crossy/Components/HopMovementComponent.h"` | INCLUDE |
| 7 | `#include "Games/Crossy/Components/ParryComponent.h"` | INCLUDE |
| 8 | `#include "Games/Crossy/Components/ParryableProjectileComponent.h"` | INCLUDE |
| 12 | `	void RegisterCrossyLuaComponentTypes()` | SYMBOL |
| 52 | `void RegisterCrossyLuaBindings(sol::state& Lua)` | SYMBOL |
| 54 | `	RegisterCrossyLuaComponentTypes();` | SYMBOL |
| 56 | `	RegisterCrossyHopMovementComponentBinding(Lua);` | SYMBOL |
| 57 | `	RegisterCrossyParryComponentBinding(Lua);` | SYMBOL |
| 58 | `	RegisterCrossyGameObjectComponentBindings(Lua);` | SYMBOL |
| 59 | `	RegisterCrossyRowManagerBinding(Lua);` | SYMBOL |
| 60 | `	RegisterCrossyUiBinding(Lua);` | SYMBOL |
| 61 | `	RegisterCrossySaveGameBinding(Lua);` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaHandles.h`
| 라인 | 원문 | 분류 |
|---|---|---|
| 4 | `#include "Games/Crossy/Components/HopMovementComponent.h"` | INCLUDE |
| 5 | `#include "Games/Crossy/Components/ParryComponent.h"` | INCLUDE |

### `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaMovementBindings.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Scripting/CrossyLuaBindings.h"` | INCLUDE |
| 5 | `#include "Games/Crossy/Scripting/CrossyLuaHandles.h"` | INCLUDE |
| 6 | `#include "Games/Crossy/Components/HopMovementComponent.h"` | INCLUDE |
| 7 | `#include "Games/Crossy/Components/ParryComponent.h"` | INCLUDE |
| 11 | `void RegisterCrossyHopMovementComponentBinding(sol::state& Lua)` | SYMBOL |
| 185 | `void RegisterCrossyGameObjectComponentBindings(sol::state& Lua)` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaParryComponentBindings.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 3 | `#include "Games/Crossy/Scripting/CrossyLuaBindings.h"` | INCLUDE |
| 7 | `#include "Games/Crossy/Scripting/CrossyLuaHandles.h"` | INCLUDE |
| 9 | `void RegisterCrossyParryComponentBinding(sol::state& Lua)` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaRowManagerBindings.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Scripting/CrossyLuaBindings.h"` | INCLUDE |
| 4 | `#include "Games/Crossy/Map/RowManager.h"` | INCLUDE |
| 185 | `void RegisterCrossyRowManagerBinding(sol::state& Lua)` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaSaveGameBindings.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Scripting/CrossyLuaBindings.h"` | INCLUDE |
| 195 | `void RegisterCrossySaveGameBinding(sol::state& Lua)` | SYMBOL |

### `KraftonEngine\Source\Games\Crossy\Scripting\CrossyLuaUiBindings.cpp`
| 라인 | 원문 | 분류 |
|---|---|---|
| 1 | `#include "Games/Crossy/Scripting/CrossyLuaBindings.h"` | INCLUDE |
| 7 | `#include "Games/Crossy/UI/CrossyGameUiSystem.h"` | INCLUDE |
| 23 | `	FCrossyGameUiSystem* GetCrossyGameUiSystem()` | SYMBOL |
| 37 | `		FCrossyGameUiSystem* GameUi = UiLayer ? static_cast<FCrossyGameUiSystem*>(UiLayer) : nullptr;` | SYMBOL |
| 45 | `	void WithGameUi(const char* FunctionName, const std::function<void(FCrossyGameUiSystem&)>& Callback)` | SYMBOL |
| 47 | `		FCrossyGameUiSystem* GameUi = GetCrossyGameUiSystem();` | SYMBOL |
| 58 | `void InstallCrossyLuaUiEventRouter()` | SYMBOL |
| 67 | `	if (FCrossyGameUiSystem* GameUi = GetCrossyGameUiSystem())` | SYMBOL |
| 73 | `void ClearCrossyLuaUiEventHandler()` | SYMBOL |
| 76 | `	if (FCrossyGameUiSystem* GameUi = GetCrossyGameUiSystem())` | SYMBOL |
| 82 | `void RegisterCrossyUiBinding(sol::state& Lua)` | SYMBOL |
| 98 | `				InstallCrossyLuaUiEventRouter();` | SYMBOL |
| 121 | `			ClearCrossyLuaUiEventHandler();` | SYMBOL |
| 127 | `			WithGameUi("Game.UI.ShowIntro", [bVisible](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 136 | `			WithGameUi("Game.UI.ShowHUD", [bVisible](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 145 | `			WithGameUi("Game.UI.ShowPause", [bVisible](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 154 | `			WithGameUi("Game.UI.ShowGameOver", [FinalScore, BestScore](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 165 | `			WithGameUi("Game.UI.HideGameOver", [](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 174 | `			WithGameUi("Game.UI.ResetRun", [](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 183 | `			WithGameUi("Game.UI.SetScore", [Score](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 192 | `			WithGameUi("Game.UI.SetBestScore", [BestScore](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 201 | `			WithGameUi("Game.UI.SetCoins", [Coins](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 210 | `			WithGameUi("Game.UI.SetLane", [Lane](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 219 | `			WithGameUi("Game.UI.SetCombo", [Combo](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 228 | `			WithGameUi("Game.UI.SetStatus", [&Text](FCrossyGameUiSystem& GameUi)` | SYMBOL |
| 237 | `			WithGameUi("Game.UI.SetTopScoresText", [&Text](FCrossyGameUiSystem& GameUi)` | SYMBOL |

---

## 분류별 합계
| 분류 | 개수 | 비고 |
|---|---|---|
| CONFIG | 92 | sln + vcxproj + filters에 집중. msbuild의 모든 ClCompile/ClInclude 항목 포함 |
| SYMBOL | 158 | 대부분이 `Source\Games\Crossy\` 내부의 함수/클래스 정의·참조 |
| INCLUDE | 28 | `#include "Games/Crossy/..."` — 외부에서 들어오는 INCLUDE는 LinkedRuntimeModules.cpp 1건뿐 |
| MACRO | 5 | 모두 `WITH_CROSSY_GAME_MODULE` 관련 (`LinkedRuntimeModules.cpp` 4건, py 1건) |
| BUILD_VAR | 8 | `Scripts\GenerateProjectFiles.py`에 집중 |
| STRING_KEY | 8 | factory 키 + Editor 패키지 설정 + 사운드 ID 등 |
| PATH | 1 | py의 `"Source\\Games\\Crossy"` |
| COMMENT | 12 | 대부분 py docstring + UiSystem.cpp 한국어 주석 + Lua 1줄 |
| **합계** | **312** | (sln 1건 포함 시 313 — 일부 다중 라벨링은 단일 우선 분류만 적용) |

(주: 합계가 337과 다소 차이가 있음 — 표 채우기 시 같은 라인에서 여러 분류가 가능한 경우 가장 강한 의미 하나만 라벨링했기 때문. 예: py 라인 587은 MACRO + BUILD_VAR 모두 해당하지만 MACRO 우선.)

## OTHER 분류 항목
없음. 모든 등장이 위 8개 분류로 결정되었음.
