# 04 — 위험과 권고

---

## 4-A. 정량 평가표

채점 규칙은 명세 그대로. 가이드 문서 부재로 축 4는 세 옵션 모두 5점(동점) 처리.

| 평가 축 | 옵션 A (완전 제거) | 옵션 B (외부 분리) | 옵션 C (py 일반화) |
|---|---|---|---|
| 1. 작업 분량 (적을수록 5점) | **2점** — 12 파일 (자산 결정 시 +α) | **2점** — 13~14 파일 + 외부 리포 신규 | **3점** — 5~6 파일 + manifest 1 |
| 1. 근거 | 03표 기준 11~15 구간. 자산 처리까지 포함하면 16+ 가능. | 옵션 A의 모든 작업 + 외부 빌드 인프라 신규(.props·환경변수). 외부 리포 자체 파일은 별도. | 5~6 파일이라 7~10 구간 상한, manifest 신규 1 포함하면 정확히 7. |
| 2. 미래 게임 추가 비용 (낮을수록 5점) | **N/A → 평가 곤란, 보수적 1점** | **2점** — 외부 게임 추가 시 메인 측 .props 수정 + 매크로 추가 | **5점** — 새 게임 추가 시 py 수정 0건 (manifest 1줄 + 모듈 cpp만) |
| 2. 근거 | Crossy 제거 = "게임 추가" 능력 자체가 사라짐. 다음 게임이 들어오면 다시 인프라부터 작성해야 함 → 사실상 가이드 4단계 이상 작업. | 외부 게임마다 메인 측 .props 신규 작성 + 매크로 추가. 가이드 4단계 그대로 = 2점 구간. | LinkedRuntimeModules.cpp까지 자동 생성 시 정확히 0건. manifest 한 줄 추가까지만 보면 4점이지만, 본 분석에서는 자동 생성을 권고하므로 5점. |
| 3. 회귀 위험 (낮을수록 5점) | **4점** — 영향 범위가 GameClient + Editor의 등록부 일부 | **2점** — 빌드 시스템 핵심 + 헤더 export·외부 의존성 매트릭스 | **3점** — 빌드 시스템 핵심을 손대지만 동작 패턴은 그대로 유지 |
| 3. 근거 | `LinkedRuntimeModules.cpp` 변경 + EditorMainPanel 1 라인. Crossy 외 모듈은 영향 없음. 다만 Editor에서도 Register가 호출되므로 약간의 회귀 위험. | 외부 .lib 링크가 환경변수·경로에 민감. 메인의 공유 헤더(`Source\Engine`, `Source\Core` 등)를 외부에서 어떻게 import할지가 가장 큰 함정 (`[확인 필요]`). | py 리팩터지만 함수 구조는 이미 매개변수화되어 있어 큰 회귀 가능성 적음. LinkedRuntimeModules.cpp 자동 생성 도입이 핵심 변동. |
| 4. 가이드 수정 필요도 (적을수록 5점) | **5점** | **5점** | **5점** |
| 4. 근거 | 가이드 자체가 존재하지 않음 → 수정 대상 없음. 세 옵션 동점. | 동상. | 동상. (분리 작업 후 가이드 작성을 별도 권고) |
| 5. 분석 불확실성 (낮을수록 5점) | **3점** — `[확인 필요]` 3건 영향 (자산 처리, ProjectSettings.ini 의미, Lua 재활용) | **3점** — `[확인 필요]` 4건 (위 3건 + 공유 헤더 export 방식) | **3점** — `[확인 필요]` 3~4건 (다른 게임 부재로 패턴 미검증, manifest 키 결정, LinkedRuntimeModules 자동화 범위, 자산 정책) |
| 5. 근거 | 자산 정책 + ProjectSettings.ini 빈 Modules의 의미 + Lua 스크립트 재활용 가능성. 3개 → 3점. | 옵션 A의 3개 + 외부 헤더 export 방식 1개 = 4개. 그래도 3점 구간. | 다른 게임 부재로 일반화 패턴 검증 불가. manifest 형식 합의 필요. 자동 생성 범위 결정 필요. 3~4개 → 3점. |
| **합계** | **15점** | **14점** | **19점** |

### 합계 산출
- A: 2 + 1 + 4 + 5 + 3 = **15**
- B: 2 + 2 + 2 + 5 + 3 = **14**
- C: 3 + 5 + 3 + 5 + 3 = **19**

---

## 4-B. 옵션 간 트레이드오프

### A vs B — "완전 제거" 대신 "외부 분리"를 선택하면
**얻음**: Crossy 코드/자산 자체는 보존되어 외부에서 계속 개발 가능. 메인 엔진과 게임 코드를 깔끔히 분리한 정통적 아키텍처. 외부 게임 다수를 호스팅 가능.
**잃음**: 메인 측 빌드 인프라가 더 복잡해짐(.props·환경변수·헤더 export). 외부 디렉터리/리포의 빌드 일관성을 별도로 관리해야 함. 메인의 엔진 헤더 변경이 외부 Crossy 빌드를 깨뜨릴 위험. 시스템 환경(환경변수 등) 의존성으로 신규 개발자 셋업 비용 증가.

### A vs C — "완전 제거" 대신 "py 일반화"를 선택하면
**얻음**: Crossy를 그대로 두면서 미래 게임 확장성 확보(축 2에서 5점 vs 1점). 빌드 인프라가 진정한 멀티게임 지원으로 진화. 향후 게임 추가 비용 0에 수렴.
**잃음**: 단일 게임만 하다 끝낼 거라면 일반화 코드가 데드 코드. 두 번째 게임이 도입되기 전까지는 검증 불가능한 추상화 추가 (CLAUDE.md "Simplicity First" 원칙과 충돌 가능). manifest 형식 결정 등 합의 비용.

### B vs C — "외부 분리" 대신 "py 일반화"를 선택하면
**얻음**: 메인 트리 안에서 모든 게임을 관리하므로 빌드/디버깅이 단순. 외부 빌드 인프라·헤더 export 함정 회피. 회귀 위험 낮음(축 3: 3점 vs 2점).
**잃음**: 게임 코드와 엔진 코드가 같은 리포에 공존 → 게임 측 변경이 엔진 PR과 섞일 위험. 게임이 많아져 리포 비대해질 가능성. 게임별 권한 분리(외부 팀/조직)에 부적합.

---

## 4-C. 단계적 경로 검토

### A → C (Crossy 제거 후 py 일반화)
- **가능**: Crossy를 먼저 지운 뒤, 다음 게임 도입 시점에 py를 일반화.
- **위험**: Crossy를 지우면 일반화 검증할 게임이 0개가 되어, 두 번째 게임이 들어올 때까지 일반화 결과를 테스트할 수 없음. YAGNI 적용 시점이 모호.
- **추천도**: 낮음. "다음 게임"이 확실치 않으면 의미 없음.

### C → A (py 일반화 후 Crossy 처리)
- **가능**: py를 먼저 일반화해 manifest 기반으로 전환한 뒤, Crossy를 (a) 그대로 둘지 (b) 삭제할지 (c) 외부로 옮길지 결정.
- **장점**: 일반화를 Crossy로 검증할 수 있음. 그 후 Crossy를 빼도 인프라는 보존됨. 두 번째 게임이 들어와도 추가 작업 0.
- **위험**: Crossy를 결국 빼야 한다면 일반화 후 작업이 한 번 더 발생.
- **추천도**: **높음**. 가장 보수적이고 안전한 경로.

### C → B (py 일반화 후 외부 분리)
- **가능**: py 일반화로 게임 단위 빌드 추상화를 완성한 뒤, Crossy만 외부 위치로 이동.
- **장점**: 외부 분리 시점에 일반화 인프라가 받쳐줌 → 외부 게임 호스팅 능력으로 자연스럽게 발전.
- **위험**: 두 번의 큰 변경이 누적되어 회귀 가능성 증가.
- **추천도**: 중간. 미래 외부 게임 호스팅 계획이 확실할 때만.

### 조합 불가능?
조합 불가능한 쌍은 없음. 다만 A → B는 "지운 것을 다시 외부에서 가져오는" 형태가 되어 비효율. A → C 또한 검증 대상 부재로 비효율.

---

## 4-D. 최종 추천

> **추천: 옵션 C (py 일반화)**
>
> **근거 점수**: 4-A 표에서 옵션 C 합계 **19점** (최고점, A=15, B=14)
>
> **결정적 이유** (3문장 이내):
> 1. 작업 분량이 가장 적고(5~6 파일), 미래 게임 추가 비용이 0에 수렴하므로 일회성 작업이 영구 자산이 됨.
> 2. 외부 분리(B)의 가장 큰 함정인 "공유 엔진 헤더 export" 문제를 회피하면서, 완전 제거(A)가 잃는 "게임 추가 능력"을 오히려 강화함.
> 3. 현재 py가 이미 `generate_vcxproj`/`generate_filters` 같은 매개변수화된 함수 구조를 갖고 있어, 일반화에 필요한 리팩터 표면적이 작음.
>
> **첫 작업 3개** (우선순위 순):
> 1. `Scripts/GenerateProjectFiles.py:31-158` — Crossy 전용 상수 8개를 list of dataclass로 변환하고, `Source/Games/*/Game.toml` 스캔 로직을 `main()` 진입부에 추가. (Game.toml은 우선 Crossy 폴더에 손으로 작성: `name = "Crossy"`, `module = "CrossyGame"`, `guid = "{a6df3d49-...}"` 등)
> 2. `Scripts/GenerateProjectFiles.py:823-892` — Crossy 전용 호출 4 블록(`scan_files`, `generate_vcxproj`, `generate_filters`, ProjectReference 빌드)을 게임 list 순회로 변환. 그 후 py를 실행해 vcxproj/sln이 동일하게 재생성됨을 git diff로 확인.
> 3. `KraftonEngine/Source/GameClient/LinkedRuntimeModules.cpp` — py가 이 파일을 자동 생성하도록 코드 추가 (또는 일단 수동 유지하되 새 게임 추가 시 한 줄만 늘리는 형태로 정리). 매크로 이름은 `WITH_<NAME>_GAME_MODULE` 패턴 유지.
>
> **사용자가 진행 전 확인해야 할 사항**:
> - **`[확인 필요]` 다른 게임 부재로 일반화 패턴 검증 불가**: 두 번째 게임 도입 계획이 없다면 옵션 C는 추상화의 가치가 떨어짐 (YAGNI). 단일 게임으로 종결할 거라면 옵션 A가 더 합리적.
> - **`[확인 필요]` LinkedRuntimeModules.cpp 자동 생성 범위**: py가 이 파일을 통째 생성할지(=수동 편집 금지), 아니면 매크로 정의만 자동화하고 본문은 수동 유지할지. 후자가 안전하지만 새 게임당 한 줄 수정이 남음.
> - **`[확인 필요]` GUID 관리 정책**: manifest에 GUID를 명시할 것인지, 게임 이름으로부터 결정적으로 파생할 것인지. 후자는 이름 변경 시 재생성된 vcxproj가 다른 GUID를 가지게 되어 sln 호환성 깨질 수 있음.

---

## 추가 분석 섹션

### 확실히 알 수 있는 것 (정적 분석으로 100% 확인)
1. `Source/Games/` 하위에는 Crossy 외 다른 게임 디렉터리가 **없음** (`Glob` 확인).
2. `WITH_CROSSY_GAME_MODULE` 매크로의 정의처는 **세 곳**: `KraftonEngine.vcxproj` 7라인 + `LinkedRuntimeModules.cpp:4` fallback + `GenerateProjectFiles.py:587` (생성기).
3. `LinkedRuntimeModules.cpp`는 **양쪽 엔진에서 호출**됨: `GameClientEngine.cpp:71`, `EditorEngine.cpp:59`.
4. `Source/Games/Crossy/` 외부에서 Crossy 헤더를 직접 include하는 곳은 **`LinkedRuntimeModules.cpp:8` 단 1곳**.
5. msbuild ProjectReference로 Crossy를 참조하는 곳은 **`KraftonEngine.vcxproj:1282` 단 1곳**.
6. 엔진 측 `FLuaScriptSubsystem`은 Crossy를 **모름**(0건). LuaScript → Crossy 역방향 결합 없음.
7. CI/CD 인프라(`.yml`/`.yaml`/`Jenkinsfile`/`tests/`) **부재** → 분리 작업이 CI 변경을 동반하지 않음.
8. `EditorMainPanel.cpp:780-796`이 이미 `Source/Games/*` 디렉터리 자동 스캔 로직을 가짐. 라인 801은 fallback 일 뿐.
9. `KraftonEngine/Asset/`와 `KraftonEngine/LuaScripts/` 안의 Crossy 관련 자산은 **파일명/파일내용에 "Crossy" 문자열을 포함하지 않음** (사운드 파일명: `BackgroundMusic.wav`, `Jump.wav` 등 generic).

### 추정에 그치는 것 (런타임/동적 동작)
1. **`ProjectSettings.ini`의 `Modules: []`가 의미하는 바**: 현재 ProjectSettings 경로로는 어떤 모듈도 로드되지 않음. Crossy는 (a) 빌드 시간 매크로로 등록되고, (b) `EditorPackageSettings.RuntimeModules`가 패키징 시 산출물에 기록되어 그것을 통해 활성화되는 것으로 **추정**. 정확한 활성 경로는 `[추정]`.
2. **`RegisterCrossyGameModule()`이 Editor 빌드에서도 호출됨**: `WITH_CROSSY_GAME_MODULE=1`인 Debug|x64/Release|x64에서 등록되지만, 그 후 `LoadModules` 호출로 실제 인스턴스화 되는 시점이 언제인지(Editor 메뉴 동작 시점인지 시작 시인지)는 `[추정]`.
3. **Lua 스크립트의 Crossy 의존성**: 텍스트상 0이지만 의미상(GameManager.lua의 점수 흐름 등) Crossy 게임에 맞춰져 있을 가능성. 다른 게임에 재활용 시 큰 폭의 수정 필요할 가능성. `[추정]`.

### 옵션별 권고 (실현 가능성)
- **옵션 A**: **HIGH** — 결합 지점이 명확하고 적음(12 파일). 기술적 난이도 낮음. 단, "Crossy를 영원히 안 쓸 것인가"의 비즈니스 결정이 선결.
- **옵션 B**: **MEDIUM** — 외부 빌드 인프라 신규 + 공유 헤더 export 함정 존재. 외부 게임 호스팅이 분명한 목표일 때만 추천.
- **옵션 C**: **HIGH** — 기존 함수 구조가 매개변수화되어 있어 리팩터 표면적 작음. 다만 다른 게임이 0개라 검증 한계 있음.

### 미리 차단해야 할 함정

1. **모듈 등록 다른 entry point**:
   - `RegisterLinkedRuntimeModules()`는 `LinkedRuntimeModules.cpp` 한 곳에서만 정의되며, `GameClientEngine.cpp:71`과 `EditorEngine.cpp:59` 두 곳에서 호출됨. 다른 entry point 없음. ✓
   - `FRuntimeModuleRegistry::RegisterFactory`는 `RuntimeModuleManager.cpp`의 generic 함수. Crossy 등록은 이 함수에 `"CrossyGame"` 키로 호출하는 것이 유일.

2. **Crossy 자산이 빌드 산출물에 포함되는 경로**:
   - `EditorPackageSettings.h:51-58` `IncludePackagePaths = ["Asset/**", "LuaScripts/**", "Data/**", ...]`로 통째 포함. Crossy를 분리해도 `Asset/Sound/Jump.wav` 같은 파일이 패키지에 들어감. → 옵션 A/B 선택 시 자산 폴더 정리도 필요. `[확인 필요]`
   - `CrossyGameModule.cpp:113-118`이 자산 경로를 코드에 박아둠 (`Sound/BackgroundMusic.wav` 등). 분리 시 이 경로의 유효성도 같이 확인.

3. **테스트 코드가 Crossy를 참조하는가**:
   - `tests/` 디렉터리 부재(Glob 확인). Test 파일명(`*test*`, `*Test*`) 검색 결과도 0. ✓ 영향 없음.

4. **CI/CD 스크립트가 Crossy를 언급하는가**:
   - `.yml`/`.yaml`/`Jenkinsfile` 부재. ✓ 영향 없음.
   - `.bat` 파일 (`DemoBuild.bat`, `GenerateProjectFiles.bat`, `ReleaseBuild.bat`)은 Crossy 직접 언급 없음(Grep 확인 — `RegisterCrossyGameModule|WITH_CROSSY_GAME_MODULE|CrossyGame` 패턴에 .bat 미포함). ✓

5. **`.vcxproj.user` 파일**:
   - `KraftonEngine\CrossyGame.vcxproj.user`는 빈 PropertyGroup. 무해. 옵션 A/B 시 함께 삭제. ✓

---

## 콘솔 한 줄 요약

```
세 옵션의 실현 가능성: A=HIGH, B=MEDIUM, C=HIGH. 권고: 옵션 C (이유: 기존 매개변수화된 빌드 함수를 재사용해 일반화 비용이 낮고, 외부 분리의 헤더 export 함정을 회피하면서 미래 게임 추가 비용을 0에 수렴시키기 때문)
```
