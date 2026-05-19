# B — ImGui 노드 에디터 진단

> 진단 전용. 코드 변경 없음. 모든 file:line은 feature/graphnode 작업
> 트리를 직접 열어 확인. 미확인은 "확인 필요", 추측은 "추정"으로
> 표시. 상위 결정(C→B→A / `imgui-node-editor` / 재귀 트리 / C 통로
> 재사용)은 전제이며 재논의하지 않는다.

---

## TL;DR — 축 B의 현재 성숙도와 핵심 미지수

**이미 있는 것**

- Dear ImGui v1.92.7 WIP가 vendored — [imgui.h:1](KraftonEngine/ThirdParty/ImGui/imgui.h:1). 빌드 시스템(MSBuild `.vcxproj`)에서 cpp 7개를 `<ClCompile>` 항목으로 명시 등록 — [KraftonEngine.vcxproj:607-613](KraftonEngine/KraftonEngine.vcxproj:607). `IncludePath`에 `ThirdParty;ThirdParty\ImGui;ThirdParty\Sol` 추가 — [KraftonEngine.vcxproj:122](KraftonEngine/KraftonEngine.vcxproj:122) 외 동일 라인 6회.
- 에디터 ImGui 컨텍스트 수명/프레임 루프는 `FEditorMainPanel`에 단일 진입점으로 정착 — Create/Render/Release 각 한 군데. ImGui 컨텍스트는 도킹·뷰포트 활성([EditorMainPanel.cpp:71-72](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:71)).
- 기존 위젯들은 모두 `FEditorWidget` 베이스(가상 `Render(float)` 한 메서드)를 상속하고, `FEditorMainPanel`이 멤버로 직접 보유 + `Initialize`/`Render` 순차 호출하는 명시 등록 패턴 — [EditorWidget.h:7-18](KraftonEngine/Source/Editor/UI/EditorWidget.h:7), [EditorMainPanel.cpp:84-92](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:84), [EditorMainPanel.cpp:147-217](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:147). 위젯 가시성은 `FEditorSettings::FUIVisibility`의 bool 플래그가 게이트 — [EditorSettings.h:55-68](KraftonEngine/Source/Editor/Settings/EditorSettings.h:55).
- C 단계의 generic 주입 통로가 살아 있다 — `USkeletalMeshComponent::SetRootGraph(std::unique_ptr<FAnimGraphNode_Base>)` ([SkeletalMeshComponent.h:71](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:71), [.cpp:307-327](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:307)) + `UAnimGraphInstance::SetRootGraph` ([AnimGraphInstance.cpp:10-17](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.cpp:10)) + `EAnimationMode::AnimationGraph` enum case + 회귀 관문 콘솔 커맨드 `anim graph test` ([EditorConsoleWidget.cpp:294](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:294), 본문 [.cpp:1355-1501](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1355)).
- 노드 5종(베이스 + SequencePlayer / Blend2 / BlendN / StateMachine)의 정적 구조는 갖춰져 있음 — [AnimGraph.h:38-101](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:38), [AnimGraph_StateMachine.h:23-81](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:23). 자식 연결은 모두 `std::unique_ptr` 소유 (raw 포인터 멤버 0건 — 확인됨).

**비어 있는 것**

- `imgui-node-editor` 라이브러리: ThirdParty에 vendored되지 않음 — `ThirdParty/` 직속 디렉토리는 `FBXSDK / ImGui / SFML / SimpleJSON / Sol` 5개 (확인됨).
- 노드 에디터 컨텍스트(`ax::NodeEditor::EditorContext`)를 생성·파괴할 위치 — 후보만 식별, 아직 결정 안 됨(Q2).
- 노드 에디터 패널 자체 — `Source/Editor/UI/` 어디에도 노드/핀/링크를 그리는 위젯 없음(확인됨, Q3).
- 노드 클래스에 **에디터 메타데이터 자리가 전무**(좌표·주석·핀 식별자) — [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h) 전수 확인. 노드는 plain struct (`Evaluate`/멤버만)이며 좌표 멤버 없음.
- 노드 타입 식별 수단 부재(상위 진단 §3 재확인) — enum tag, 가상 `GetType()`, 팩토리/등록 매크로 모두 없음. RTTI(`dynamic_cast`)는 사용 가능 — `AnimStateMachineInstance.cpp:25`에서 `SequencePlayer`로 다운캐스트하는 단 한 곳만 존재.
- 노드 생성 방법은 `std::make_unique<FAnimGraphNode_*>()` 직접 호출 — 팩토리 없음. 에디터가 "타입 이름 → 새 노드 인스턴스" 디스패치를 신규로 가져야 한다.

**사람이 정해야 할 것 (정책 결정)**

1. `imgui-node-editor`의 vendoring 빌드 결선 형식 (Q1) — `.vcxproj`에 cpp를 `<ClCompile>`로 명시 추가하는 기존 패턴을 그대로 따를지, 별도 정적 라이브러리로 분리할지.
2. 노드 에디터 컨텍스트의 생성/파괴 위치 (Q2) — `FEditorMainPanel::Create`/`Release`에 단일 컨텍스트로 둘지, 노드 에디터 패널 위젯 자체에 멤버로 두고 lazy 생성할지.
3. 노드 에디터 패널의 배치 (Q3) — 독립 패널(새 `FEditorWidget` 파생 + `FUIVisibility` 토글 추가) / `FEditorSkeletalMeshViewerWidget` 셸에 새 탭(`ESkeletalEditorTabKind` 확장) / 기타.
4. 다중 fan-out 정책 (Q5) — UI에서 막을지, UI 허용하되 트리 추출 시 거부, 직렬화 표현 재고(축 A 사안 이월).
5. B 검증 형태 (Q7) — 수동 육안 / `anim graph test`의 단언 부품 재사용 / 기타.

---

## Q1 — `imgui-node-editor` vendoring 경로

### 수행한 grep / 파일 열람

- `Glob **/Directory.Build.*` + `Glob **/*.vcxproj` + `Glob **/CMakeLists.txt` + `Glob **/premake*`
- `Grep sources\.txt` (저장소 전체)
- `Grep imgui|ImGui|ThirdParty` in [KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj)
- `Grep Sol|SimpleJSON|json\.hpp|sol\.hpp` in [KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj)
- `Bash find ThirdParty -maxdepth 2 -iname "license*" -o -iname "copying*" -o -iname "readme*"`
- `Read ThirdParty/ImGui/sources.txt`, `Read ThirdParty/ImGui/imgui.h:1-30`
- 디렉토리 나열: `ls ThirdParty/ImGui`, `SimpleJSON`, `Sol`, `FBXSDK`, `SFML`.

### 확인된 사실

**빌드 시스템**: MSBuild `.vcxproj` (KraftonEngine.sln + KraftonEngine.vcxproj + CrossyGame.vcxproj). CMake / premake 등 다른 빌드 시스템 산출물 없음(`CMakeLists.txt`는 vcpkg 산출물 하나만 매치, `premake*` 0건).

**전역 PCH·include·라이브러리 결선** (cross-config 7군데가 모두 동일):

- `IncludePath` — [KraftonEngine.vcxproj:122](KraftonEngine/KraftonEngine.vcxproj:122) (외 6 라인: 129, 136, 143, 150, 157, 164):
  ```
  Source\Engine;Source;ThirdParty;ThirdParty\ImGui;ThirdParty\Sol;
  Source\Editor;Source\ObjViewer;Source\GameClient;.;
  $(ProjectDir)ThirdParty\FBXSDK\include;$(IncludePath)
  ```
- `LibraryPath` (Debug/Release): SFML + FBXSDK용 prebuilt lib 경로 — [KraftonEngine.vcxproj:137,144,151,158,165](KraftonEngine/KraftonEngine.vcxproj:137).
- DLL 복사(`xcopy`): SFML / FBXSDK — [KraftonEngine.vcxproj:220](KraftonEngine/KraftonEngine.vcxproj:220), [245](KraftonEngine/KraftonEngine.vcxproj:245), [269](KraftonEngine/KraftonEngine.vcxproj:269), [293](KraftonEngine/KraftonEngine.vcxproj:293), [317](KraftonEngine/KraftonEngine.vcxproj:317).
- 전역 PCH = `pch.h`가 모든 ClCompile에 `/FI`로 강제 inject — [Directory.Build.props:14-25](Directory.Build.props:14). `pch.cpp`만 `/Yc`로 PCH 생성 — [Directory.Build.targets:87-94](Directory.Build.targets:87).
- 리플렉션 codegen은 `Source/**` 헤더에만 적용 — [Directory.Build.targets:10-46](Directory.Build.targets:10). ThirdParty는 영향 없음.

**ThirdParty 라이브러리별 배치/등록 표** (실제 확인):

| 라이브러리 | 배치 | 빌드 등록(`.vcxproj`) | 헤더온리 | 라이선스 파일 |
|---|---|---|---|---|
| ImGui (v1.92.7 WIP — [imgui.h:1](KraftonEngine/ThirdParty/ImGui/imgui.h:1)) | `ThirdParty/ImGui/` 평탄 (header + cpp + impl + `imstb_*.h`) | cpp 7개를 `<ClCompile>` 명시 등록 — [vcxproj:607-613](KraftonEngine/KraftonEngine.vcxproj:607). `sources.txt`는 빌드에서 **참조되지 않음** (저장소 전체에서 `sources.txt`를 읽거나 import하는 빌드 스크립트 0건 확인) — 문서적 매니페스트일 뿐. | 아니오 (cpp 빌드 필요) | 없음 (저장소에 `LICENSE*` 파일 없음, header 1행에 버전 주석만) |
| SimpleJSON | `ThirdParty/SimpleJSON/json.hpp` (단일 파일) | `<ClInclude>` 한 줄 — [vcxproj:1353](KraftonEngine/KraftonEngine.vcxproj:1353). `<ClCompile>` 0건. | 예 | 없음 |
| Sol | `ThirdParty/Sol/{config,forward,sol}.hpp` (헤더 3개) | `<ClInclude>` 3개 — [vcxproj:1354-1356](KraftonEngine/KraftonEngine.vcxproj:1354). `<ClCompile>` 0건. include는 `IncludePath`에 `ThirdParty\Sol` 추가로 해결. | 예 | 없음 |
| SFML | `ThirdParty/SFML/{Audio,Graphics,…}.hpp + bin/ + lib/` | prebuilt lib + DLL 복사 (위 LibraryPath/xcopy 항목). `<ClCompile>` 항목 0건. | 아니오 (prebuilt 정적/동적 lib) | 없음 |
| FBXSDK | `ThirdParty/FBXSDK/{include,lib,License.rtf,readme.txt,…}` | header만 `<ClInclude>`로 명시(거대 트리, [vcxproj:946-1019](KraftonEngine/KraftonEngine.vcxproj:946)…). prebuilt lib는 위 LibraryPath/xcopy로. `<ClCompile>` 0건. | 아니오 (prebuilt) | 있음: [License.rtf](KraftonEngine/ThirdParty/FBXSDK/License.rtf), [readme.txt](KraftonEngine/ThirdParty/FBXSDK/readme.txt) |

**핵심 관찰**:

1. **`.vcxproj`가 단일 의존성 매니페스트.** ThirdParty cpp를 빌드에 넣으려면 `<ClCompile>` 항목을 손으로 추가해야 한다(ImGui와 동일 패턴). 자동 glob 없음 — `**/*.cpp` 스캔이 아니라 명시 enumerate.
2. **헤더 가시화는 두 단계**: `IncludePath`에 디렉토리 등록(컴파일러 검색 경로) + `<ClInclude>`에 파일 등록(VS 솔루션 탐색기 표시·IntelliSense). 둘이 분리돼 있어 새 라이브러리는 IncludePath와 ClInclude 둘 다 손대야 한다.
3. **`sources.txt`는 문서일 뿐**, 빌드 스크립트가 소비하지 않는다. `imgui-node-editor`를 들일 때 같은 파일을 만들 필요는 없다 — `.vcxproj`에 직접 항목을 추가하는 것이 일관된 형식.
4. **라이선스 파일 보존 관행이 일관되지 않다.** FBXSDK만 라이선스 동봉, 나머지 4종은 `LICENSE`/`COPYING` 파일 없음. `imgui-node-editor`의 라이선스(MIT — 공개 정보, 확인 필요: vendoring 시점에 파일을 함께 두는지의 정책 결정 사항)를 넣을지 별도 결정.

**`imgui-node-editor`를 들이려면 건드릴 빌드 파일 후보** (코드는 쓰지 않음 — file 단위 후보):

- [KraftonEngine/KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj) — 세 곳을 손대게 됨:
  - `IncludePath` 7 라인에 `ThirdParty\imgui-node-editor` 등 추가 (라이브러리 디렉토리 명은 사람 결정).
  - `<ClCompile>` 항목으로 라이브러리 cpp 4개(공개 레포 기준: `imgui_node_editor.cpp`, `imgui_node_editor_api.cpp`, `imgui_canvas.cpp`, `crude_json.cpp`) 추가 — 확인 필요: 실제 cpp 목록은 vendoring 시점 레포 스냅샷으로 재검증.
  - `<ClInclude>` 항목으로 라이브러리 헤더(`imgui_node_editor.h`, `imgui_node_editor_internal.h`, `imgui_canvas.h`, `imgui_bezier_math.{h,inl}`, `imgui_extra_math.{h,inl}`, `crude_json.h`) 추가 — 동일 확인 필요.
- 신규 디렉토리: `KraftonEngine/ThirdParty/imgui-node-editor/` (디렉토리명 정책 — `imgui_node_editor` 언더스코어 형식도 가능. 사람 결정).

**ImGui 버전 호환**: 현재 vendored ImGui는 `v1.92.7 WIP` ([imgui.h:1](KraftonEngine/ThirdParty/ImGui/imgui.h:1)). `imgui-node-editor`가 요구하는 ImGui 최소 버전과의 호환 — **확인 필요** (공개 레포 README/CMakeLists에 명시된 최소 버전을 vendoring 시점에 재확인). `imgui-node-editor`의 master 브랜치는 비교적 최신 ImGui(1.91 라인)를 지원해 왔지만 정확한 cutoff는 시점에 따라 다르므로 추정 금지.

**라이선스** (공개 정보): `imgui-node-editor` (thedmd) — MIT License. 단, 라이선스 파일을 vendoring 트리에 동봉할지는 ThirdParty 관행(위 표 마지막 열)이 일관되지 않아 사람 결정 사항.

**확인 필요**:
- `imgui-node-editor`의 실제 cpp/헤더 목록(공개 레포 스냅샷 시점에 따라 다름).
- ImGui 1.92.7 WIP과 `imgui-node-editor`의 정확한 호환 상태.
- `crude_json.cpp`와 기존 `ThirdParty/SimpleJSON/json.hpp`의 공존(이름 충돌은 없음 — 둘 다 다른 네임스페이스에 들어가지만, `imgui-node-editor`는 자체 노드 상태 직렬화에 `crude_json`을 사용한다 — 본 엔진의 직렬화와 무관, 축 A에 영향 없음).

---

## Q2 — ImGui 컨텍스트 통합 (노드 에디터 컨텍스트의 생성/파괴 위치)

### 수행한 grep

- `Grep ImGui::CreateContext|ImGui::DestroyContext|ImGui_ImplDX11_Init|ImGui_ImplWin32_Init|ImGui_ImplDX11_Shutdown|ImGui_ImplWin32_Shutdown` (Source)
- `Grep ImGui::NewFrame|ImGui::Render|ImGui_ImplDX11_NewFrame|ImGui_ImplWin32_NewFrame|ImGui::EndFrame` (Source)
- `Grep EditorMainPanel` (Source)
- `Grep MainPanel\.(Create|Render|Release)` (EditorEngine.cpp)

### 확인된 사실

ImGui 컨텍스트 진입점은 저장소에 **세 개**가 있다 — 에디터·뷰어·게임클라이언트 각각이 자기 컨텍스트를 만든다(서로 독립):

| 진입점 | 위치 | 비고 |
|---|---|---|
| 에디터 | [EditorMainPanel.cpp:66](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:66) Create / [.cpp:103-105](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:103) Release / [.cpp:133-135](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:133) NewFrame / [.cpp:226-232](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:226) Render | Docking+Viewports 활성. B의 1차 진입점. |
| ObjViewer | [ObjViewerPanel.cpp:18,28-29,34-36,41-43,51](KraftonEngine/Source/ObjViewer/ObjViewerPanel.cpp:18) | 별도 viewer 도구. B 범위 외 (확인 필요: 노드 에디터가 ObjViewer에서도 필요한지 — 추정 불필요). |
| GameClient | [GameClientOverlay.cpp:131,142-143,165-167,246-248,261](KraftonEngine/Source/GameClient/GameClientOverlay.cpp:131) | 게임 빌드 ImGui 오버레이. B 범위 외. |

**에디터 ImGui 수명/프레임 흐름** (file:line이 박힌 단계):

1. **생성** — `UEditorEngine::PreInit` 안의 startup scope에서 한 번:
   - [EditorEngine.cpp:88](KraftonEngine/Source/Editor/EditorEngine.cpp:88) `MainPanel.Create(Window, Renderer, this)`.
   - 본문([EditorMainPanel.cpp:63-93](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:63)): `IMGUI_CHECKVERSION` → `ImGui::CreateContext` → `LoadSetting` → IO 플래그 (Docking, Viewports) → 한글 폰트 → `ImGui_ImplWin32_Init` → `ImGui_ImplDX11_Init` → 각 위젯 `Initialize` 호출.
2. **매 프레임** — `UEditorEngine::RenderUI(DeltaTime)`이 진입점:
   - [EditorEngine.cpp:196](KraftonEngine/Source/Editor/EditorEngine.cpp:196) `MainPanel.Render(DeltaTime)`.
   - 본문([EditorMainPanel.cpp:131-233](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:131)): `ImGui_ImplDX11_NewFrame` → `ImGui_ImplWin32_NewFrame` → `ImGui::NewFrame` → `DockSpaceOverViewport` + 메인 메뉴 + 위젯들 `Render` → `ImGui::Render` → `ImGui_ImplDX11_RenderDrawData` → ViewportsEnable이면 `UpdatePlatformWindows`/`RenderPlatformWindowsDefault`.
3. **파괴** — `UEditorEngine::Shutdown` 막판:
   - [EditorEngine.cpp:126](KraftonEngine/Source/Editor/EditorEngine.cpp:126) `MainPanel.Release()`.
   - 본문([EditorMainPanel.cpp:95-106](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:95)): 패키지 빌드 스레드 join → `ConsoleWidget.Shutdown` → `ImGui_ImplDX11_Shutdown` → `ImGui_ImplWin32_Shutdown` → `ImGui::DestroyContext`.

### 노드 에디터 컨텍스트(`ax::NodeEditor::EditorContext`)를 끼울 후보 위치

`imgui-node-editor`는 패널마다 별도의 `EditorContext`를 가져야 한다(공개 문서 — vendoring 시점 재확인). 위 흐름을 깨지 않고 컨텍스트를 둘 후보 두 가지:

| 후보 | 컨텍스트 소유 위치 | 생성/파괴 시점 | 트레이드오프 |
|---|---|---|---|
| (가) `FEditorMainPanel`에 멤버 추가 | 패널의 멤버 변수로 `ax::NodeEditor::EditorContext*` 하나 | Create의 ImGui_Impl 직후 / Release의 ImGui::DestroyContext 직전 | (+) 흐름이 ImGui 컨텍스트와 1:1 짝 — 수명 명확. (−) 노드 에디터 위젯이 panel/widget 어느 쪽에 살든 EditorContext에 접근하려면 panel 포인터를 받아야 함. 패널이 여러 개의 노드 에디터 인스턴스(예: 탭별 그래프)를 가지려 하면 단일 컨텍스트로는 부족 — 그때 후보 (나)로 자연 진화. |
| (나) 노드 에디터 위젯(신규 `FEditorWidget` 파생)이 자체 보유 | 위젯 인스턴스의 멤버 변수 | 위젯 ctor/dtor 또는 첫 `Render` 시 lazy 생성, 위젯 dtor에서 파괴 | (+) 위젯 단위 격리 — 다중 그래프(탭) 확장이 자연스럽다. (+) `FEditorWidget` 베이스 인터페이스만으로 충분 — panel 변경 면적 작다. (−) ImGui 컨텍스트는 panel이 소유하는데 노드 에디터 컨텍스트는 위젯이 소유 — 수명 짝이 미묘하게 어긋남(ImGui 컨텍스트가 죽은 뒤 위젯 dtor가 NodeEditor::DestroyEditor를 부르면 ImGui 측 자원이 이미 비어 있을 수 있음 — 확인 필요: `ax::NodeEditor::DestroyEditor`가 ImGui 컨텍스트에 강결합인지). 위젯이 panel의 멤버라면 panel dtor 순서가 멤버 dtor를 먼저 돌리므로 안전(추정). |

**확인 필요**:
- `ax::NodeEditor::DestroyEditor`의 ImGui 컨텍스트 의존성(생존 순서).
- 노드 에디터 패널이 단일 인스턴스인지 다중(탭) 인스턴스인지(Q3과 연동).

진단은 단정하지 않는다. 단정해야 할 사람: 패널이 단일이면 (가)가 가볍고, 다중 그래프 확장을 시야에 두면 (나)가 자연스럽다.

---

## Q3 — 에디터 UI 패널을 어디에 띄우는가

### 수행한 grep / 파일 열람

- `Bash ls Source/Editor/UI/`, `Bash ls Source/Editor/UI/SkeletalEditor/`.
- `Read EditorWidget.h` — 베이스 인터페이스 확인.
- `Read EditorConsoleWidget.h` — 등록·렌더·셧다운 흐름 추적(파일 한 단위로 끝까지).
- `Read EditorSkeletalMeshViewerWidget.h`, `SkeletalEditorTab.h`, `SkeletalEditorTab.cpp:1-80` — 다중 탭 셸의 구성 파악.
- `Grep FUIVisibility|bConsole|bScene` in [EditorSettings.h](KraftonEngine/Source/Editor/Settings/EditorSettings.h).

### 기존 위젯 등록·렌더 패턴 (확인된 사실)

**베이스 인터페이스** — [EditorWidget.h:7-18](KraftonEngine/Source/Editor/UI/EditorWidget.h:7):
- `class FEditorWidget` — 두 메서드: `virtual void Initialize(UEditorEngine*)` (기본 구현 있음), `virtual void Render(float DeltaTime) = 0` (순수가상).
- 멤버: `UEditorEngine* EditorEngine = nullptr;` (protected).

**등록 패턴** — 중앙 레지스트리 없이 panel이 멤버로 직접 보유:
- [EditorMainPanel.h:66-75](KraftonEngine/Source/Editor/UI/EditorMainPanel.h:66): `FEditorConsoleWidget ConsoleWidget; FEditorControlWidget ControlWidget; … FEditorSkeletalMeshViewerWidget SkeletalMeshViewerWidget; FEditorContentBrowserWidget ContentBrowserWidget; EditorShadowMapDebugWidget ShadowMapDebugWidget; EditorProjectSettingsWidget ProjectSettingsWidget;` — 모두 값 멤버.
- 등록(Initialize) — [EditorMainPanel.cpp:84-92](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:84): 위젯별 `.Initialize(InEditorEngine)` 순차 호출. ContentBrowser만 D3D Device를 추가로 받음.

**렌더 패턴** — panel `Render` 본문에서 가시성 플래그를 체크 후 위젯의 `Render`를 직접 호출:
- 예: [EditorMainPanel.cpp:160-168](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:160) — `if (!bHideEditorWindows && Settings.UI.bConsole) { … ConsoleWidget.Render(DeltaTime); }`.
- 동일 패턴이 Control(154), Property(170), Scene(176), Stat(182), Curve(188), SkeletalMeshViewer(194), ContentBrowser(200), ShadowMapDebug(206)에서 반복.

**가시성 플래그** — [EditorSettings.h:55-68](KraftonEngine/Source/Editor/Settings/EditorSettings.h:55):
- `struct FUIVisibility` 안의 bool 11개 (`bConsole`, `bControl`, `bProperty`, `bScene`, `bStat`, `bCurve`, `bContentBrowser`, `bImGUISettings`, `bEditorDebug`, `bShadowMapDebug`, `bSkeletalMeshViewer`). 메인 메뉴(추정: `RenderMainMenuBar` — [EditorMainPanel.cpp:235-](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:235))에서 토글되며 ini로 round-trip.

**ConsoleWidget의 추적 결과** — 등록(Initialize) → 매 프레임 Render → 종료(Shutdown):
- 헤더 [EditorConsoleWidget.h:36-123](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.h:36) 확인: 베이스에 없는 `Shutdown()`을 자체 정의(콘솔 로그 디바이스 해제). `RegisterDefaultCommands` 등으로 커맨드 디스패치를 자체 관리.
- panel의 Render 본문 [EditorMainPanel.cpp:160-168](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:160)에서 `ConsoleWidget.Render(DeltaTime)` 호출 — main viewport에 ID set 후.

**SkeletalEditor 다중-탭 셸** — [EditorSkeletalMeshViewerWidget.h:12-39](KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.h:12):
- `FEditorWidget` 파생인 위젯 하나가 `TArray<std::unique_ptr<FSkeletalEditorTab>> Tabs`를 보유 — 탭 인덱스(`ActiveTabIndex`)로 활성 탭 전환.
- 탭 베이스 [SkeletalEditorTab.h:32-97](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.h:32): `RenderTabContent` + 좌/우 패널 가상 훅 + mode bar(아이콘 버튼으로 모드 점프) + 공용 preview 뷰포트.
- 탭 종류 enum [SkeletalEditorTab.h:13-17](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.h:13): `ESkeletalEditorTabKind { SkeletalMesh, AnimSequence }`.
- 파생 구현 둘: [SkeletalMeshEditorTab.{h,cpp}](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.h), [AnimSequenceEditorTab.{h,cpp}](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h).
- mode bar 아이콘 등록 — [SkeletalEditorTab.cpp:30-64](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.cpp:30): `Asset/Editor/UEIcons/SkeletalMesh.png`, `Animation.png`.

### 노드 에디터 패널의 배치 후보

| 후보 | 무엇 | 건드릴 파일 | 트레이드오프 |
|---|---|---|---|
| (가) 독립 `FEditorWidget` 파생 | 새 `FEditorAnimGraphWidget`(또는 다른 이름) 한 클래스 — 자체 ImGui 윈도우 하나 띄움 | 신규: `Source/Editor/UI/EditorAnimGraphWidget.{h,cpp}` (가칭). 수정: [EditorMainPanel.h](KraftonEngine/Source/Editor/UI/EditorMainPanel.h) (값 멤버 추가), [EditorMainPanel.cpp](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp) (Initialize + 가시성 게이트 + Render 호출), [EditorSettings.h](KraftonEngine/Source/Editor/Settings/EditorSettings.h) (`FUIVisibility`에 새 bool), 메뉴(추정: `RenderMainMenuBar`)에 토글 추가. | (+) 기존 셸과 결합도 0 — 가장 단순. (+) 위젯이 자체 NodeEditor 컨텍스트 보유 시 수명 격리(Q2-나). (−) AnimGraph 작업이 SkeletalMesh/AnimSequence 미리보기와 떨어진 곳에서 일어남 — 컨텍스트 공유가 어렵다(어떤 메시·스켈레톤을 대상으로 미리보기 평가할지). |
| (나) `FEditorSkeletalMeshViewerWidget` 셸의 새 탭 | `ESkeletalEditorTabKind`에 `AnimGraph` 추가 + 새 파생 탭(`FAnimGraphEditorTab`) | 수정: [SkeletalEditorTab.h:13-17](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.h:13) (enum 확장), [SkeletalEditorTab.{h,cpp}](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.h) (mode bar 아이콘 셋 확장), [EditorSkeletalMeshViewerWidget.{h,cpp}](KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.h) (tab 생성·전환 path 추가). 신규: `Source/Editor/UI/SkeletalEditor/AnimGraphEditorTab.{h,cpp}` (가칭). 아이콘 자산: `Asset/Editor/UEIcons/AnimGraph.png` (또는 기존 활용). | (+) 미리보기 뷰포트(`FSkeletalEditorPreviewScene` — 베이스 멤버 [SkeletalEditorTab.h:72](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.h:72))를 그대로 재사용 — 에디터로 만든 그래프를 그 자리에서 적용해 평가 결과를 볼 수 있다. (+) "스켈레톤·메시·시퀀스·그래프"가 한 셸에 묶임. (−) 변경 면적 크다 — mode bar enum/아이콘/탭 생성 경로 모두 손대야 함. (−) AnimGraph가 특정 mesh·skeleton에 묶이지 않는 자산이라면(추정 — A 단계 결정 사항) "Skeletal*Editor" 셸 안에 두는 것이 의미상 어색. |
| (다) `FEditorWidget` 파생 + 자체 preview scene | 후보 (가) + 위젯 안에 자체 mini-preview viewport | 후보 (가) + preview scene 구성(별도 viewport client 등) | (+) (가)의 격리성 + (나)의 적용 검증 가능성. (−) preview scene 구성은 [FSkeletalEditorPreviewScene](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.h) 수준의 비자명한 작업 — 비용이 크다. |

**확인 필요**:
- AnimGraph 자산이 특정 skeletal mesh에 종속되는지 vs 스켈레톤만 알면 되는지(축 A 사안이지만 (나) 후보 선택에 영향). 본 진단은 가정하지 않는다.

---

## Q4 — 노드 ↔ 에디터 표현 매핑

### 수행한 grep / 파일 열람

- `Read AnimGraph.h` (전체) — 베이스 + 3종(SequencePlayer/Blend2/BlendN) + AnimGraph 컨테이너 정의.
- `Read AnimGraph_StateMachine.h` (전체) — StateMachine 노드 + 부속 struct.
- `Grep FAnimGraphNode_Base\s*\*` (Source) — raw 포인터 멤버 검색(0건, `GetRoot()` 반환값 1건만).
- `Grep FAnimGraphNode_\w+\s*\*` (Source) — 다른 raw 포인터 멤버 검색(1건 매치, `dynamic_cast<FAnimGraphNode_SequencePlayer*>` — Q5에서 다시).

### 노드 5종 구조 (file:line 포함)

**베이스** [AnimGraph.h:38-42](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:38):
```
struct FAnimGraphNode_Base
{
    virtual ~FAnimGraphNode_Base() = default;
    virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) = 0;
};
```
- 멤버 0개. **타입 식별 멤버 없음** (type tag, virtual GetType 등 0건).
- 에디터 메타데이터(좌표·주석·핀 id) 자리 **없음**.

**평가 컨텍스트** [AnimGraph.h:27-33](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:27): 시간·델타·스켈레톤·OwningInstance만 운반. 노드는 시간을 자체 보유하지 않는다.

### 노드별 매핑 표

| 노드 | 자식 슬롯 (입력 핀 후보) | 편집 파라미터 (노드 위젯 후보) | 에디터 메타데이터 자리 | 정의 위치 |
|---|---|---|---|---|
| `FAnimGraphNode_SequencePlayer` | 없음 (재생 노드 — 자식 없음) | `Sequence` (외부 자산 ref, non-owning), `DataModel` (Sequence→GetDataModel() 캐시), `TrackToBoneIndex` (불투명 캐시 — 편집 대상 아님). **재생 속도·loop·시작 시간 멤버는 없음** — 시간은 `FAnimEvalContext::TimeSeconds`로 외부에서 들어옴. | 없음 | [AnimGraph.h:49-63](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:49) |
| `FAnimGraphNode_Blend2` | `ChildA, ChildB` — `std::unique_ptr<FAnimGraphNode_Base>` 2개 (소유) | `Alpha` (float, [0,1] 클램프) | 없음 (private `ScratchA/B`는 평가 캐시 — 편집 대상 아님) | [AnimGraph.h:71-82](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:71) |
| `FAnimGraphNode_BlendN` | `Children` — `TArray<std::unique_ptr<FAnimGraphNode_Base>>` (소유, 가변 길이) | `Weights` — `TArray<float>` (Children 길이와 0-pad/truncate 정합) | 없음 (private `ChildScratches`) | [AnimGraph.h:92-101](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:92) |
| `FAnimGraphNode_StateMachine` | 자식 슬롯 없음(자식 sub-graph는 `States[i].Sub`로 노드 내부에 격납). 입력 핀이 "자식 그래프 진입"이라기보단 "상태 머신 자체가 하나의 평가 단위". | `States` (FAnimState 배열), `Transitions`, `InitialStateIndex` | 없음 (평가 상태: `ActiveStateIndex`, `ActiveTransitionIndex`, `TransitionElapsed`, `StateLocalTimes`, `Scratch*` — 모두 평가 캐시) | [AnimGraph_StateMachine.h:61-81](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:61) |
| `FAnimState` (StateMachine 안) | `Sub` — `std::unique_ptr<FAnimGraphNode_Base>` (소유, 보통 SequencePlayer 또는 Blend 트리) | `Name` (FName), `bResetTimeOnEnter`, `bLooping`, `SubLengthHint` (캐시성) | 없음 | [AnimGraph_StateMachine.h:23-30](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:23) |
| `FAnimTransition` | (노드 아님 — `int32 FromStateIndex/ToStateIndex` 인덱스 참조) | `BlendDuration`, `Conditions` (FAnimTransitionCondition 배열, AND 결합) | 없음 | [AnimGraph_StateMachine.h:49-55](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:49) |
| `FAnimTransitionCondition` | (조건 단위) | `Kind` (enum), `TimeThreshold`, `VarName`, `bExpectedValue`, `NotifyName` | 없음 | [AnimGraph_StateMachine.h:40-47](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:40) |

### 타입 식별 / 노드 생성

- **타입 식별 수단 부재 (재확인)**: enum tag, virtual GetType, static TypeTag 모두 0건. RTTI는 enabled — 유일 사용처는 [AnimStateMachineInstance.cpp:25](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:25) (`dynamic_cast<FAnimGraphNode_SequencePlayer*>`로 SubLengthHint 도출).
- **노드 생성 수단**: `std::make_unique<FAnimGraphNode_*>()` 직접. 팩토리 없음. 회귀 관문 콘솔 커맨드도 같은 패턴 — [EditorConsoleWidget.cpp:1428](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1428) `auto Root = std::make_unique<FAnimGraphNode_SequencePlayer>();`.

### 에디터 표현 ↔ 트리 변환에 신규로 필요한 것 (사실 나열 — 설계 단정 없음)

- **타입 식별 디스패치**: 에디터가 "팔레트에서 SequencePlayer 추가" 같은 액션을 처리하려면 타입 이름 → 새 인스턴스 + 타입 이름 → UI 위젯(편집 파라미터 폼) 두 방향 디스패치가 필요. 현재 0건.
- **에디터 메타데이터의 저장 위치**: 노드 좌표·핀 식별자·주석 등은 노드 클래스에 둘 자리가 없다. 후보 — (1) 노드 베이스에 멤버 추가(런타임에는 평가에 영향 없음 — dead weight). (2) 에디터 측 사이드카 맵(`std::unordered_map<FAnimGraphNode_Base*, EditorNodeMeta>`). 어느 쪽이든 정책 결정 사항 — A 단계(직렬화)에도 영향. 본 진단은 단정하지 않는다.
- **`std::unique_ptr` 소유 모델과 에디터 자유 편집의 결선**: 노드 이동(다른 부모로 슬롯 이전)은 `unique_ptr`의 move 한 번이지만, 에디터가 "이 노드를 잡고 끌어서 다른 슬롯에 떨어뜨림"의 UI 액션을 어떻게 트리 mutation으로 디스패치할지의 코드 경로가 신규로 필요. 현재 mutation API는 `AnimGraph::SetRoot` 하나뿐(상위 진단 §1) — 자식 슬롯 단위 mutation 헬퍼는 없다.
- **링크 표현 ↔ `unique_ptr` 자식 슬롯**: `imgui-node-editor`의 링크는 (PinId_out → PinId_in)의 단방향 엣지. 트리에서는 "부모의 자식 슬롯이 곧 입력 핀"이라는 1:1 대응이므로, 한 부모의 같은 입력 핀에 두 링크가 들어오면 후행이 선행을 덮어쓰는 의미가 된다. UI 처리 정책은 Q5.

---

## Q5 — 트리 제약 vs 에디터 자유도 (정책 결정 사안)

### 수행한 grep / 파일 열람

- `Grep FAnimGraphNode_\w+\s*\*` (Source) — raw 포인터 멤버 검색.
- `Read AnimGraph.h:71-101` — Blend2/BlendN 자식 소유 형태 재확인.
- `Read AnimGraph_StateMachine.h:23-30` — FAnimState::Sub 소유 형태 재확인.
- `Grep SetRootGraph` (Source) — 통로 시그니처 재확인.

### 확인된 사실

- **모든 자식 슬롯이 `std::unique_ptr`로 소유**:
  - `FAnimGraphNode_Blend2::ChildA, ChildB` — [AnimGraph.h:73-74](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:73)
  - `FAnimGraphNode_BlendN::Children` (TArray of unique_ptr) — [AnimGraph.h:94](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:94)
  - `FAnimState::Sub` — [AnimGraph_StateMachine.h:26](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:26)
  - `AnimGraph::Root` — [AnimGraph.h:115](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:115)
- **raw 포인터로 노드를 가리키는 멤버 0건** (grep 결과): `FAnimGraphNode_\w+\s*\*` 매치 2건은 모두 멤버가 아님 — [AnimGraph.h:110](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:110) `GetRoot()` 반환값, [AnimStateMachineInstance.cpp:25](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:25) `dynamic_cast` 결과 지역 변수.
- **C 통로 단일 root 시그니처**: [SkeletalMeshComponent.h:71](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:71) `void SetRootGraph(std::unique_ptr<FAnimGraphNode_Base> InRoot);` — 베이스 타입, 단일 root.

### 함의

표현은 **owning 트리**다. 한 노드를 두 부모가 자식으로 동시에 가리키면 `std::unique_ptr`의 유일소유 불변이 깨진다(컴파일 자체가 안 됨). 즉 **트리 구조상 다중 fan-out이 표현 불가**.

`imgui-node-editor`는 임의의 (출력 핀 → 입력 핀) 링크를 UI상 자유롭게 긋게 해 준다 — 다중 fan-out 링크도 UI 차원에서는 그릴 수 있다. 이 간극을 메우는 정책 후보:

### 후보 (정책 결정 — 진단은 단정하지 않음)

| 후보 | 무엇을 한다 | 트레이드오프 |
|---|---|---|
| (가) UI 차원에서 fan-out을 *그릴 수 없게* 막는다 | `imgui-node-editor`의 `AcceptNewItem` 단계에서 "출력 핀에 이미 링크가 하나 있으면 추가 링크 거부" + "입력 핀에 이미 링크가 있으면 거부"로 핀 차수를 강제 | (+) 사용자에게 즉시 시각 피드백. (+) 트리 추출이 단순(모든 링크가 합법). (−) 외부 라이브러리의 자유도를 깎는 정책 코드가 누락되면 UI 모순 발생 가능 — 모든 경로에서 일관 적용 필요. |
| (나) UI상 허용하되 트리 추출 시 거부·검증 | 자유롭게 그리게 두고, "그래프 적용" 액션에서 트리 추출 시 부정합(다중 fan-out, 사이클 등) 검출하면 에러 보고 + 적용 abort | (+) 라이브러리 자연 거동 유지 — 학습 곡선 ↓. (+) 추출 시점 단일 검증 — 코드 경로가 응축. (−) 사용자가 "에디터로 그렸지만 적용 안 됨"의 침묵 거부 가능 — UX 마찰. (−) 검증 메시지의 명확성·복구 경로(어느 링크를 지울지) 설계 비용. |
| (다) 직렬화 표현 재고 — DAG 허용 | 자식 소유 모델 변경(공유 노드를 `std::shared_ptr` 또는 `int32 NodeId` 풀+인덱스 참조로) | **축 A 사안이라 B에서 결정 못 함 — 이월.** B는 트리 표현을 전제로 진행, A 단계에서 표현이 바뀌면 B의 트리 추출 로직도 따라 바뀐다. |

이 중 어느 안을 택하느냐는 사람이 정한다. 진단은 단정하지 않는다. 단 한 가지 사실은 명확하다 — 현재 표현에서는 다중 fan-out이 *런타임에 도달할 수 없다*. 따라서 UI/추출 어느 단계든 이 제약을 표현해야 한다.

---

## Q6 — C 통로와의 결선

### 수행한 grep / 파일 열람

- `Grep SetRootGraph` (Source) — 호출자·정의 위치 전수.
- `Grep EAnimationMode::AnimationGraph|GAnimationModeNames|case EAnimationMode|UAnimGraphInstance` ([SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)).
- `Read SkeletalMeshComponent.h:14-130` — enum/모드/AnimInstance 멤버 재확인.
- `Read SkeletalMeshComponent.cpp:20-130, 295-330` — `GAnimationModeNames` 배열, `EnsureAnimInstance` switch, `SetRootGraph` 본문 확인.
- `Read AnimGraphInstance.h / .cpp` — 파생 클래스 본문 확인.
- `Grep anim\.graph\.test|HandleAnimGraphTest|AnimGraphTest` (Source).
- `Read EditorConsoleWidget.cpp:285-296` (등록) + `:1355-1501` (본문 전수).

### `SetRootGraph` 호출 흐름 (확인된 사실, file:line)

1. **회귀 관문 커맨드 등록**: [EditorConsoleWidget.cpp:294-295](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:294) — 카테고리 `"Diagnostics"`, 이름 `"anim graph test"` (공백 구분 — 본 진단의 모든 명령명은 이 형식).
2. **커맨드 본문** — [EditorConsoleWidget.cpp:1355-1501](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1355):
   - Step 1: 대상 컴포넌트 조회. 선택된 액터 우선([:1377](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1377)), 없으면 world 첫 매치([:1383-1391](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1383)).
   - Step 2: 첫 시퀀스 확보([:1408-1425](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1408)) — `FMeshManager::FindAnimSequenceForSkeletalMesh(..., 0, ...)`.
   - Step 3: root 조립([:1428-1429](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1428)) — `make_unique<FAnimGraphNode_SequencePlayer>` + `SetSequence`.
   - Step 4: 모드 강제 swap([:1434-1435](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1434)) — `SetAnimationMode(AnimationGraph)` + `PostEditProperty("Animation Mode")` (mode swap이 destroy + EnsureAnimInstance 재호출 경로를 탐).
   - Step 5: root 주입([:1439](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1439)) — `SkelComp->SetRootGraph(std::move(Root));`.
   - Step 6: 평가 시간 + 강제 평가([:1442-1444](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1442)) — `SetBakedAnimTime(0.5*PlayLength)` + `RefreshAnimationPose`.
   - Step 7: 단언([:1449-1500](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1449)) — `GetLocalBonePoseMatrices()`를 bind pose와 비교, `|QuatDot| ≤ 0.999f`인 본 개수로 PASS/FAIL.
3. **컴포넌트 측 `SetRootGraph`** — [SkeletalMeshComponent.cpp:307-327](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:307):
   - 모드 확인(정책 ii: `AnimationGraph` 아니면 early return) → `EnsureAnimInstance` → `Cast<UAnimGraphInstance>` → `Graph->SetRootGraph(std::move(InRoot))`.
4. **AnimInstance 측 `SetRootGraph`** — [AnimGraphInstance.cpp:10-17](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.cpp:10): `AnimGraphPtr` lazy 생성 + `SetRoot(std::move(InRoot))`.
5. **`EnsureAnimInstance` switch** — [SkeletalMeshComponent.cpp:101-122](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:101): `case AnimationGraph` → `CreateObject<UAnimGraphInstance>(this)`.
6. **`EAnimationMode` enum** — [SkeletalMeshComponent.h:18-23](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:18): `AnimationSingleNode=0, AnimationStateMachine=1, AnimationGraph=2`.
7. **라벨 배열** — [SkeletalMeshComponent.cpp:25-29](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:25): `"Single Node" / "State Machine" / "Graph"`.

### `SetRootGraph` 호출자 (현재)

`grep SetRootGraph` 매치 — 호출자는 [EditorConsoleWidget.cpp:1439](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1439) **단 한 곳**. C 단계가 신설한 통로의 첫 사용자가 그 회귀 관문 커맨드다. B의 에디터 "적용" 액션이 두 번째 사용자가 될 것이며, **새 통로를 만들 필요 없음** — 같은 통로를 호출하면 된다.

### 에디터 "적용" 액션을 둘 후보 위치 + 모드 전환 트리거

| 후보 | 위치 | 모드 전환 책임 | 비고 |
|---|---|---|---|
| (가) 노드 에디터 패널 위젯 내부 ImGui 버튼 | "Apply Graph" 버튼이 위젯 본문에 — `EditorAnimGraphWidget::Render` 안 | 위젯이 직접 `SkelComp->SetAnimationMode(AnimationGraph); SkelComp->PostEditProperty("Animation Mode");` 호출 후 `SetRootGraph` | `anim graph test`의 step 4·5와 동형. 위젯이 컴포넌트 포인터를 어떻게 잡는지(선택 액터 우선·이름 입력 등)는 Q3에서 따라옴. |
| (나) SkeletalEditor 셸의 새 탭 안 액션 | (가)와 같지만 탭 컨텍스트에서 — 미리보기 메시·스켈레톤이 셸이 이미 보유 | 동일 | 미리보기 결선이 자연스러우나 셸 변경 면적이 큼(Q3-나). |
| (다) 메인 메뉴 / 토스트 / 콘솔 명령 | 콘솔에 `anim.graph.editor.apply` 같은 명령 추가 — 위젯과 분리 | 명령 핸들러가 위 동일 시퀀스 | UI 분리 명확. CLI 친화. (−) UX가 분리됨 — 에디터에서 보고 콘솔에서 발사. |

`anim graph test` 커맨드가 정확히 본떠야 할 시퀀스를 보여준다 — 새 액션은 그 step 4~6을 그대로 따른다. **B는 통로를 새로 만들지 않는다.**

---

## Q7 — B 단계 검증 방법

### 수행한 grep / 파일 열람

- `Read EditorConsoleWidget.cpp:1355-1501` (Q6과 동일).
- `Glob **/test*.cpp` (0건), `Glob **/*Test*.cpp` (1건 — `GamePackageSmokeTester.cpp`, 게임 패키지 스모크용 — B와 무관).
- `Grep gtest|GTEST|TEST_F|catch2|doctest|CATCH_TEST` (KraftonEngine 트리) — 매치는 ThirdParty `glew.h:1`의 "GTEST" 무관 문자열 1건만, 즉 **단위 테스트 프레임워크 부재** (상위 진단·C 계획과 일치).

### 확인된 사실

`anim graph test`의 단언 부품 (step 7, [.cpp:1446-1500](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1446)):
- 입력: `SkelComp->GetLocalBonePoseMatrices()` (public 산출물 — AnimInstance를 protected 채로 두면서도 평가 결과를 외부에서 보는 통로).
- 비교: 각 본의 `FQuat::FromMatrix(bindLocalPose)` vs `FQuat::FromMatrix(evalLocal)`의 `|Dot| ≤ 0.999f`이면 differing 카운트.
- 판정: `DifferingBones > 0`이면 PASS, 0이면 FAIL. 샘플 본 1개의 `|Dot|`도 함께 로그.

이 단언 부품은 "에디터로 조립한 그래프가 적용되어 한 tick 평가되었는가"를 똑같이 검사할 수 있다 — 그래프 조립 경로(코드 vs 에디터)는 단언에 영향이 없고, 입력은 컴포넌트의 평가 산출물이기 때문이다.

### B 검증 후보

| 후보 | 무엇 | 트레이드오프 |
|---|---|---|
| (가) 수동 — 뷰포트 육안 확인 | 에디터로 조립 → "Apply" → preview 뷰포트에서 메시가 bind pose가 아닌 자세로 움직이는지 사람 눈으로 확인 | (+) 0 비용 — 추가 코드 없음. (+) 시각적 회로 확인이 자연스러움. (−) 객관 단언 없음 — "동작했다"의 합의가 사람 감각에 의존. (−) CI 자동화 불가. |
| (나) 반자동 — `anim graph test`의 단언 부품 재사용 | "Apply" 액션 본문에서 그래프 조립·주입·평가·`GetLocalBonePoseMatrices` 비교까지 호출, PASS/FAIL을 콘솔 또는 토스트로 보고. 단언 함수는 `anim graph test` 본문에서 분리해 helper로 노출(예: `static bool AssertPoseDiffersFromBindPose(USkeletalMeshComponent*, FString& OutMsg)`)하거나, 별도 콘솔 명령(`anim graph editor verify`)으로 분리. | (+) 객관 단언 — 회귀 시그널이 명확. (+) `anim graph test` 단언 로직 재사용 — 중복 없음. (−) 단언 helper 추출(또는 새 명령 등록)에 소소한 작업 — 변경 면적이 0은 아님. (−) FAIL의 의미가 좁다("bind pose와 다름" — 그래프 평가 결과의 정확성은 다른 단언이 필요). |
| (다) 새 콘솔 명령 — 에디터 상태 직렬화 → 적용 → 단언 | 에디터가 보유한 in-memory 그래프 상태를 임시 JSON 등으로 덤프하고 별도 명령이 그것을 로드해 적용 + 단언. | (+) 검증 인프라가 에디터 외부에서도 사용 가능. (−) 직렬화 표현은 A 단계 사안 — B에서 만들면 A와 충돌·중복 가능성. **권장 안 됨**(B 범위에 직렬화를 끌어들이지 말 것 — 본 진단의 스코프 디시플린에 위배). |

진단은 단정하지 않는다. (가)와 (나)의 조합(에디터로 조립 → 적용 → 뷰포트 + 콘솔 토스트 PASS/FAIL)도 자연스러운 형태이다.

**확인 필요**:
- `anim graph test`의 단언 부품을 helper 함수로 추출할지(공유) vs B용 별도 명령 본문에 복제할지의 분리 정책.

---

## B 구현 시 건드릴 파일 목록 (예상)

> file 단위까지만, 코드는 쓰지 않음. 후보 선택에 따라 갈리는 파일은 구분.

### 공통 (모든 후보 조합)

**신규**:
- `KraftonEngine/ThirdParty/imgui-node-editor/` — 라이브러리 vendoring 디렉토리(파일 목록은 Q1 — 공개 레포 스냅샷 시점 재확인).

**수정**:
- [KraftonEngine/KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj) — `IncludePath` 7 라인 + `<ClCompile>` 4개(추정) + `<ClInclude>` 6~8개(추정) 추가.

### 후보 조합별 갈림 (Q3 — 패널 배치)

**Q3-(가) 독립 위젯**:

신규:
- `KraftonEngine/Source/Editor/UI/EditorAnimGraphWidget.{h,cpp}` (이름 가칭) — `FEditorWidget` 파생.

수정:
- [KraftonEngine/Source/Editor/UI/EditorMainPanel.h](KraftonEngine/Source/Editor/UI/EditorMainPanel.h) — 값 멤버 추가.
- [KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp) — Create의 `Initialize` 호출 + Render의 가시성 게이트 + (Q2-가 채택 시) NodeEditor 컨텍스트 Create/Release.
- [KraftonEngine/Source/Editor/Settings/EditorSettings.h](KraftonEngine/Source/Editor/Settings/EditorSettings.h) — `FUIVisibility`에 새 bool.
- [KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp) `RenderMainMenuBar` — 토글 메뉴 항목.

**Q3-(나) SkeletalEditor 셸의 새 탭**:

신규:
- `KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimGraphEditorTab.{h,cpp}` (이름 가칭) — `FSkeletalEditorTab` 파생.
- (선택) `Asset/Editor/UEIcons/AnimGraph.png` — mode bar 아이콘.

수정:
- [KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.h](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.h) — `ESkeletalEditorTabKind`/`EModeBarIcon` enum에 새 항목.
- [KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.cpp](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.cpp) — `GetModeBarIconFileName` switch + `EnsureModeBarIconsLoaded` size.
- [KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.{h,cpp}](KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.h) — 탭 생성·전환 path 추가.

### 후보 조합별 갈림 (Q2 — NodeEditor 컨텍스트 위치)

- **Q2-(가) panel 멤버**: 위 EditorMainPanel.{h,cpp} 변경 면적이 늘어남(컨텍스트 멤버 + Create/Release에서 NodeEditor::CreateEditor/DestroyEditor).
- **Q2-(나) 위젯 자체 보유**: panel은 손대지 않음. 위젯/탭 본문이 컨텍스트 라이프사이클을 흡수.

### 후보 조합별 갈림 (Q7 — 검증 형태)

- **Q7-(나) 단언 부품 재사용**:
  - [KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp) — `HandleAnimGraphTest`의 단언 블록을 helper로 추출(또는 위젯 본문에 복제).
  - 위 위젯/탭 파일에 "Apply"-후-단언 호출 추가.

### 손대지 않는 파일 (스코프 디시플린)

- AnimGraph 노드 정의([AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h), [AnimGraph_StateMachine.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h)) — B는 노드 클래스에 멤버를 추가하지 않는 게 기본(에디터 메타데이터를 노드에 박는 것은 정책 결정 사항으로 분리).
- C 통로([SkeletalMeshComponent.{h,cpp}](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h), [AnimGraphInstance.{h,cpp}](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.h)) — 통로는 신설하지 않음, 재사용만.
- `.asset` 파이프라인 — A 사안.

---

## 확인 필요 / 정책 결정 항목

사람이 정해야 함:

1. **`imgui-node-editor` vendoring 빌드 결선 형식** (Q1) — `.vcxproj`에 `<ClCompile>`로 명시 추가하는 기존 ImGui 패턴을 그대로 따를지(권장 — 일관). 디렉토리명(`imgui-node-editor` vs `imgui_node_editor`). 라이선스 파일 동봉 여부(ThirdParty 관행이 일관되지 않음 — FBXSDK만 동봉).
2. **노드 에디터 컨텍스트의 위치** (Q2) — 후보 (가) `FEditorMainPanel`에 단일 컨텍스트 / (나) 위젯·탭 자체 보유. 다중 그래프 탭 확장을 시야에 둘지가 갈림길.
3. **노드 에디터 패널의 배치** (Q3) — 후보 (가) 독립 위젯 / (나) SkeletalEditor 셸의 새 탭 / (다) (가)+자체 preview. 미리보기 결선의 자연스러움 vs 변경 면적의 트레이드오프. (나)는 A 단계 결정("AnimGraph가 mesh에 종속되는가")에 영향받음 — A 결정 전이라면 (가) 안전.
4. **에디터 메타데이터(노드 좌표·주석)의 저장 위치** (Q4) — (1) 노드 클래스에 멤버 추가(평가에 영향 없는 dead weight, 직렬화에 포함) / (2) 에디터 측 사이드카 맵(`unordered_map<ptr, meta>`, 직렬화는 별도). A 단계에도 영향 — A 결정 전에 (2)로 시작하는 게 가역성 ↑.
5. **다중 fan-out 정책** (Q5) — (가) UI 차단 / (나) 추출 시 거부 / (다) 직렬화 표현 재고 — (다)는 A 이월. B에서 정할 수 있는 건 (가)·(나) 중 하나.
6. **노드 타입 식별 디스패치 형태** (Q4) — 팔레트 "노드 추가" 액션과 노드 위젯 디스패치를 위한 타입→인스턴스/타입→UI 매핑. 후보: (1) `if/else` 체인(가장 단순), (2) `std::unordered_map<FString, FactoryFn>` 등록 테이블, (3) 노드에 가상 `GetTypeName()` 추가. A 단계의 직렬화 type tag 형태와 일관되게 정하는 게 유리(사람 결정 — A 결정과 같이 갈 수도 있음).
7. **B 검증 형태** (Q7) — (가) 수동 / (나) 단언 helper 재사용 / 조합. helper 추출 여부.
8. **에디터 "적용" 액션의 위치와 모드 전환 책임** (Q6) — 위젯 내부 버튼 / 탭 액션 / 콘솔 명령. 어디든 컴포넌트 측 정책 (ii)("`AnimationMode != AnimationGraph`면 early return") 때문에 액션이 모드 swap을 명시적으로 수행해야 함.

---

## 범위 외 관찰 사항 (기록만, 진단하지 않음)

- **"저장된 `.asset` 그래프를 로드해 주입"하는 검증은 A 단계 이월**. B는 in-memory 조립 → 주입까지만 본다.
- **노드 타입 식별 메커니즘**(enum / virtual GetType / factory) 부재 — A·B 둘 다에 영향. A 결정 전 임시 디스패치(if/else)로 B를 진행하면 추후 A 결정 시 B 코드 일부 수정 필요(범위 외, 인지만).
- **`UAnimSingleNodeInstance::EvaluateGraph`에 임시 진단 로그 `[DIAG][root_rotation_coordsys_verification]` 잔존** ([AnimSingleNodeInstance.cpp:52-67](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:52) — 상위 진단에서 이미 기록) — B 무관.
- **`UAnimInstance::GetAnimGraph()` getter 미사용** ([AnimInstance.h:65](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:65) — C 진단 기록) — B가 디버그용으로 첫 호출자가 될 수 있음(필요 시).
- **ObjViewer / GameClient의 별도 ImGui 컨텍스트** ([ObjViewerPanel.cpp:18](KraftonEngine/Source/ObjViewer/ObjViewerPanel.cpp:18), [GameClientOverlay.cpp:131](KraftonEngine/Source/GameClient/GameClientOverlay.cpp:131)) — 노드 에디터 도입 영역은 에디터(`FEditorMainPanel`)에 한정되므로 두 컨텍스트는 영향 없음. 단 NodeEditor 라이브러리 자체는 컴파일 대상이므로 두 경로에서도 헤더가 보일 수 있음 — 사용하지 않으면 코드 영향 없음(추정).
- **`imgui-node-editor`의 `crude_json`이 자체 노드 상태(레이아웃 등) 직렬화에 사용** — 본 엔진의 그래프 직렬화(축 A)와 무관. 두 직렬화는 다른 데이터를 다룬다.
