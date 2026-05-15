# 03 — 옵션별 영향 평가

2단계의 결합 지점을 기준으로 세 옵션의 수정 범위를 산정.

가이드 문서가 존재하지 않으므로 "가이드에 명시되어 있는가" 컬럼은 모든 행이 **N/A (가이드 미존재)**. 참고용으로만 남김.

---

## 옵션 A — 완전 제거

CrossyGame을 솔루션에서 영구 삭제하고 다른 게임도 추가 계획이 없는 경우.

| 파일 | 수정 종류 | 라인 수 (영향) | 가이드 명시 |
|---|---|---|---|
| `Scripts\GenerateProjectFiles.py` | 편집 (Crossy 관련 ~45 라인 삭제) | ~45 (전체 901 라인 중) | N/A |
| `KraftonEngine.sln` | 편집 (라인 8 삭제 + ProjectConfigurationPlatforms 안 crossy_guid 라인 ~7개 삭제) | ~8 | N/A |
| `KraftonEngine\KraftonEngine.vcxproj` | 편집 (PreprocessorDefinitions 7군데에서 `WITH_CROSSY_GAME_MODULE=...;` 토큰 삭제, 라인 1282 ProjectReference 블록 삭제) | ~8 (라인 일부 토큰만 수정) | N/A |
| `KraftonEngine\Source\GameClient\LinkedRuntimeModules.cpp` | 편집 (16 라인 중 Crossy 관련 ~14 라인 삭제 → 빈 함수만 남김) | ~14 | N/A |
| `KraftonEngine\Source\Editor\Packaging\EditorPackageSettings.h` | 편집 (라인 21~22, 43의 기본값을 빈 문자열/빈 배열로 변경) | 3 | N/A |
| `KraftonEngine\Source\Editor\UI\EditorMainPanel.cpp` | 편집 (라인 801 fallback `push_back({"Crossy", ...})` 삭제 또는 빈 처리) | 1 | N/A |
| `KraftonEngine\Source\Editor\Packaging\GamePackageBuilder.cpp` | 편집 (라인 430 주석에서 "CrossyGame.vcxproj" 예시 일반화) | 1 | N/A |
| `KraftonEngine\CrossyGame.vcxproj` | **삭제** | 286 (전체 파일) | N/A |
| `KraftonEngine\CrossyGame.vcxproj.filters` | **삭제** | 96 (전체 파일) | N/A |
| `KraftonEngine\CrossyGame.vcxproj.user` | **삭제** | 4 (전체 파일) | N/A |
| `KraftonEngine\Source\Games\Crossy\` | **디렉터리 삭제** | 21개 파일 | N/A |
| `KraftonEngine\LuaScripts\Game\RowGenerator.lua` | 편집 (라인 97 주석에서 "Crossy" 단어만 제거) | 1 | N/A |
| `KraftonEngine\LuaScripts\Game\*.lua` (10개) | **유지/삭제 결정 필요** — Crossy 게임 전용 흐름이지만 텍스트는 generic | `[확인 필요]` | N/A |
| `KraftonEngine\Asset\Sound\*.wav` (6개) | **유지/삭제 결정 필요** — Crossy 사운드 자산 | `[확인 필요]` | N/A |
| `KraftonEngine\Asset\UI\*.rml/*.rcss` | **유지/삭제 결정 필요** — Crossy UI 자산 | `[확인 필요]` | N/A |
| `KraftonEngine\Bin\` 및 `Build\` 안 Crossy 산출물 | 자동 재생성 → 무시 가능 | — | N/A |

**파일 수**: 코드/빌드 측 **12개 파일**. 자산(.lua/.wav/.rml) 결정에 따라 +0~20개 추가.

**핵심 작업 흐름**:
1. py에서 Crossy 관련 상수·함수 호출 삭제
2. py 재실행 → vcxproj/sln 재생성 (KraftonEngine.vcxproj와 sln의 Crossy 흔적 자동 제거)
3. C++ 측 `LinkedRuntimeModules.cpp`, `EditorPackageSettings.h`, `EditorMainPanel.cpp` 편집
4. `Source/Games/Crossy/` 디렉터리 + `CrossyGame.vcxproj*` 3개 파일 삭제
5. 빌드 후 매크로 `WITH_CROSSY_GAME_MODULE` 잔존 참조 없는지 확인

---

## 옵션 B — 외부 디렉터리/리포지토리로 분리

옵션 A의 모든 작업을 수행한 뒤, Crossy를 별도 위치로 이동하고 메인 솔루션이 그것을 외부 라이브러리로 link.

### 옵션 A에 추가로 필요한 작업

| 항목 | 내용 | 비고 |
|---|---|---|
| Crossy 디렉터리/리포 위치 결정 | `D:\Games\CrossyGame\`, GitHub repo 등 | `[확인 필요]` |
| Crossy 측 자체 빌드 시스템 | CrossyGame.vcxproj + 자체 GenerateProjectFiles.py(또는 CMake/.sln) | 옵션 A의 vcxproj 286 라인을 그대로 옮기면 됨 |
| 산출물(.lib) 경로를 메인이 알게 | 두 방법: (a) 외부 솔루션의 `<ProjectReference>` 절대/상대 경로, (b) `<Reference>` + `<AdditionalDependencies>` + `<AdditionalLibraryDirectories>` | (b)가 더 일반적 |
| 헤더 검색 경로 추가 | 메인 vcxproj에 `<AdditionalIncludeDirectories>` 항목 추가 (`D:\...\CrossyGame\Source\Games\Crossy\..` 또는 환경변수 `$(CROSSY_DIR)`) | 환경변수 권장 |
| 환경변수/캐시 변수 도입 | `CROSSY_GAME_DIR=D:\Games\CrossyGame` 같은 시스템/사용자 환경변수 또는 .props 파일 (`Crossy.props`) | .props가 깔끔 |
| 메인 빌드가 Crossy 빌드를 트리거할지 | 옵션 1: 트리거 안 함(외부에서 미리 빌드, .lib만 link). 옵션 2: msbuild ProjectReference로 트리거 | 옵션 1이 분리 의도에 부합 |
| 빌드 구성 매칭 규칙 | 메인의 GameClient\|x64 빌드 시 Crossy의 GameClient\|x64 .lib 사용. Debug\|x64 ↔ Debug\|x64 등. 매칭 테이블을 .props에 명시 | 옵션 A의 `CROSSY_LINK_CONFIGURATIONS`와 동일 |
| `RegisterCrossyGameModule()` 진입점 | 외부 .lib에 들어 있어야 함. 메인은 `extern "C"` 선언 또는 외부 헤더 include | 외부 헤더 경로 확보 필요 |
| `WITH_CROSSY_GAME_MODULE` 매크로 | 메인 vcxproj에서 환경변수 존재 시 1로 설정하는 로직 | py로 처리 가능 |

### 옵션 B 표 (총 수정 파일 수)
| 파일 | 수정 종류 | 비고 |
|---|---|---|
| 옵션 A의 12개 파일 | 동일 | — |
| 신규: 메인 측 `Crossy.props` 또는 .targets | 신규 작성 | 외부 .lib·헤더 경로 보유 |
| 메인 `KraftonEngine.vcxproj` | 추가 편집 (`<Import Project="Crossy.props">` + `<AdditionalIncludeDirectories>` + `<AdditionalDependencies>`) | py가 생성하도록 함 |
| `Scripts\GenerateProjectFiles.py` | 추가 편집 (외부 경로/매크로 주입 로직) | A보다 코드량 증가 |
| **외부 위치**: Crossy 디렉터리 자체 빌드 인프라 | 신규 (자체 sln/vcxproj/py 또는 CMake) | 새 리포의 모든 파일 |

**파일 수**: 메인 측 **13~14 파일** (옵션 A + 1~2). 외부 측 별도 리포 새로 구성.

**핵심 검증 항목**:
- Crossy를 외부 트리에 둔 상태에서 메인 빌드만으로 GameClient.exe가 빌드/실행 되는가? (msbuild 외부 .lib 경로 인식)
- 외부 Crossy 빌드 후 .lib을 메인이 link 못 하면 빌드 실패. 빌드 매트릭스 검증 필요.
- 공유 헤더(`Engine`, `Core` 등) 의존성 — Crossy는 `Source\Engine`, `Source` 등 메인 측 헤더를 include. 외부 분리 시 메인 헤더를 외부에 export 해야 함. **이게 가장 큰 함정**. `[확인 필요]`

---

## 옵션 C — GenerateProjectFiles.py 일반화

py에서 게임별 하드코딩을 제거하고 manifest/디렉터리 자동 스캔 기반으로 전환. Crossy는 그대로 유지하되 "여러 게임 중 하나"로 처리.

### 일반화 위치 (2-A의 결과 그대로)

| 현재 위치 | 일반화 후 처리 | 추가로 필요한 것 |
|---|---|---|
| `CROSSY_PROJECT_NAME = "CrossyGame"` (py:31) | `for game_dir in glob("Source/Games/*"): name = game_dir.name + "Game"` | 명명 규약 정착 |
| `CROSSY_PROJECT_GUID = "{a6df3d49...}"` (py:48) | manifest 파일(예: `Source/Games/Crossy/Game.toml`)에 `guid = "{...}"` 키 또는 결정적 해시(`md5("Crossy")` 등)로 자동 생성 | manifest 도입 또는 결정적 알고리즘 합의 |
| `CROSSY_ROOT_NAMESPACE` (py:50) | 보통 `<Name>Game`으로 통일 | — |
| `CROSSY_LINK_CONFIGURATIONS` (py:65) | 게임 공통 set으로 추출 (모든 게임 동일하다 가정) 또는 manifest에서 override 가능 | — |
| `CROSSY_CONFIG_PROPS` (py:94) | 게임 공통 dict로 추출 | — |
| `CROSSY_SCAN_DIRS` (py:123) | `[f"Source\\Games\\{game.name}"]` 자동 생성 | — |
| `ENGINE_EXCLUDE_PREFIXES`의 `"Source\\Games\\Crossy"` (py:108) | `[f"Source\\Games\\{g.name}" for g in games]` 자동 생성 | — |
| `CROSSY_INCLUDE_PATHS` (py:152) | 게임 공통 list | — |
| 라인 587 매크로 주입 | `for g in games: base_defs.append(f"WITH_{g.name.upper()}_GAME_MODULE={1 if (cfg,plat) in g.link_configs else 0}")` | LinkedRuntimeModules.cpp 자동 생성으로 가야 깔끔 |
| 라인 735~736 sln Project 항목 | `for g in games: lines.append(...)` | — |
| 라인 759~761 sln 구성 매핑 | `for g in games: ...` | — |
| 라인 823~892 main() 안의 Crossy 호출 4 블록 | `for g in games: scan_files(...); generate_vcxproj(...); generate_filters(...)` | — |

### 신규 도입 항목

| 항목 | 내용 |
|---|---|
| 게임 manifest 형식 | `Source/Games/<Name>/Game.toml` 같은 파일. 키: `guid`, `link_configurations`, `extra_defines` 등 |
| 디렉터리 자동 스캔 | `Source/Games/*/Game.toml` glob → 게임 list 생성 |
| GUID 자동 생성 (선택) | manifest 없을 때 `f"{{{md5(name)[:8]}-{...}}}"`로 결정적 생성 |
| LinkedRuntimeModules.cpp 자동 생성 | py가 게임 list에서 이 파일을 통째로 생성. `Register<Name>GameModule()` 호출을 자동 작성 |
| 게임 새로 추가 시 워크플로 | (1) `Source/Games/<NewGame>/` 디렉터리 + Game.toml 생성, (2) `<NewGame>GameModule.cpp/.h` 작성 + `Register<NewGame>GameModule()` 정의, (3) `python Scripts/GenerateProjectFiles.py` 실행 → vcxproj/sln/LinkedRuntimeModules.cpp 모두 자동 갱신 |

### 옵션 C 표

| 파일 | 수정 종류 | 라인 수 |
|---|---|---|
| `Scripts\GenerateProjectFiles.py` | 대폭 리팩터 (Crossy 상수들을 게임 list 순회로 변환, manifest 로더 추가) | ~50~80 라인 변경 + 신규 ~30 라인 |
| `KraftonEngine\Source\GameClient\LinkedRuntimeModules.cpp` | py가 자동 생성하도록 변경. 또는 수동으로 두되 generic loop으로 리팩터. | 16 → ~20 라인 |
| `KraftonEngine\Source\Games\Crossy\Game.toml` (신규) | 신규 작성 | ~10 라인 |
| `KraftonEngine\Source\Editor\UI\EditorMainPanel.cpp` | 라인 801 fallback 제거 가능 (스캔이 항상 동작하면 불필요) | 1 |
| `KraftonEngine\Source\Editor\Packaging\EditorPackageSettings.h` | "Crossy" 기본값을 동적 결정으로 (예: 첫 번째 발견 게임) — 또는 그대로 둠 | 0~3 |
| `KraftonEngine\Source\Editor\Packaging\GamePackageBuilder.cpp` | 주석 일반화 (선택) | 1 |
| (자동 생성) `KraftonEngine.vcxproj` | py 재실행 시 generic 형태로 갱신 | — |
| (자동 생성) `KraftonEngine\CrossyGame.vcxproj` | py 재실행 시 그대로 재생성 | — |
| (자동 생성) `KraftonEngine.sln` | py 재실행 시 generic 형태로 갱신 | — |

**파일 수**: 코드 측 **5~6 파일** (편집) + manifest **1 파일** (신규). 자동 생성물(vcxproj/sln/LinkedRuntimeModules.cpp)은 py가 처리.

### "새 게임 추가 시 py 수정량" 평가
- 옵션 C 완성 후: **0** (manifest + 모듈 cpp만 작성).
- 단, `LinkedRuntimeModules.cpp` 자동 생성 단계까지 가지 않으면 새 게임당 이 파일 수정 1회 필요.
- 그리고 **다른 게임이 현재 0개라서 패턴 검증 불가**. 현재 코드는 사실상 단일 게임에 최적화되어 있으며, 두 번째 게임을 도입하는 시점에 unforeseen 결합이 드러날 수 있음. `[확인 필요]`

---

## 옵션 영향 정량 비교

| 지표 | 옵션 A | 옵션 B | 옵션 C |
|---|---|---|---|
| 수정 파일 수 (코드/빌드) | 12 | 13~14 + 외부 리포 | 5~6 + manifest 1 |
| 신규 도입 인프라 | 없음 | 외부 빌드 시스템·.props·환경변수 | manifest 형식·자동 스캔 로직 |
| `Source/Games/Crossy/` 처리 | 삭제 | 외부 이동 | 유지 |
| 자산(.lua/.wav/.rml) 처리 결정 | 함께 결정 필요 | 함께 결정 필요 | 그대로 유지 |
| 새 게임 추가 시 py 수정 | (제거 후 새 게임 없음 — 가정상 N/A) | (외부 게임이라 동일) | 0~1 |
| Crossy 외 다른 게임 패턴 검증 | 불필요 | 불필요 | **검증 불가** (현재 0개) |

⚠️ 가이드 문서 부재로 모든 항목의 "가이드 명시 여부" 컬럼은 N/A. 분리 작업 후 가이드 작성을 권고.
