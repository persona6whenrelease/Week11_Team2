# ImGui AnimGraph 에디터 & Root Graph 주입 진단

> 진단 전용. 코드 변경 없음. 모든 file:line은 feature/graphnode 작업 트리를
> 직접 열어 확인. 미확인은 "확인 필요"로 표시. 추측은 "추정"으로 표시.

## TL;DR — 세 축(직렬화 / 에디터 / 런타임)의 현재 성숙도

- **축 A 직렬화** — AnimGraph 노드 트리는 `std::unique_ptr` 부모-자식 소유 구조로 표현돼 있고(`AnimGraph.h:73-74`, `AnimGraph.h:94`, `AnimGraph_StateMachine.h:26`), pointer fix-up 없이 트리 순회로 직렬화 가능한 형태다. 단 **노드 타입 식별 수단(type enum / 팩토리 / 등록 테이블)이 존재하지 않고**(`AnimGraph.h:38-42`), **모든 AnimGraph 노드 클래스에 `Serialize` / `operator<<`가 0건이다** (`Source/Engine/Asset/Animation/Core` 범위 grep 결과 — Skeleton/AnimSequence/AnimDataModel에만 존재). `.asset` 헤더 파이프라인(`FAssetFileHeader`, `EAssetType`, `FWindowsBinReader/Writer`)은 그대로 재사용 가능 — 단 `EAssetType`에 새 enumerant이 필요하다(`AssetTypes.h:16-26`).

- **축 B 에디터** — Dear ImGui v1.92.7 WIP가 `ThirdParty/ImGui/`에 vendored(`ThirdParty/ImGui/imgui.h:1`), DX11+Win32 백엔드로 통합 운용 중이다. **노드 에디터 라이브러리(`imnodes` / `ax::NodeEditor` 등)는 vendored되어 있지 않다** — `grep -i "node-editor|NodeEditor|imnodes|ax::NodeEditor"` 결과 0건. ThirdParty 디렉토리 하위 후보도 없음(`FBXSDK / ImGui / SFML / SimpleJSON / Sol`).

- **축 C 런타임** — `UAnimInstance::AnimGraphPtr`은 owned `std::unique_ptr<AnimGraph>`로 이미 보유 가능(`AnimInstance.h:91`). 그래프 주입 진입점은 현재 **`UAnimStateMachineInstance::SetStateMachineGraph`** 하나뿐이고(`AnimStateMachineInstance.cpp:11`), 시그니처가 `unique_ptr<FAnimGraphNode_StateMachine>` — **임의 root 타입을 받지 못한다**. `USkeletalMeshComponent`에서 외부로 `AnimInstance`를 노출하는 public API도 없다(`AnimInstance` 멤버는 protected — `SkeletalMeshComponent.h:116`). root graph 주입을 위한 최종 진입점은 컴포넌트 측에 신규로 추가되어야 한다.

### 사전 전제 재확인 (Q5·Q6의 file:line 재검증)

- **"런타임 클립 교체(`PlayAnimation`/`SetAnimation`)는 AnimationSingleNode 모드 전용"** — **확인**. `USkeletalMeshComponent::SetAnimation`은 `Cast<UAnimSingleNodeInstance>(AnimInstance)`로 분기해, Single 인스턴스일 때만 `SetAnimation`을 전달한다 (`SkeletalMeshComponent.cpp:152-155`). StateMachine 모드에서는 cast가 실패해 no-op이다.

- **"`UAnimStateMachineInstance::SetStateMachineGraph`로 그래프를 통째 교체"** — **확인. 단, 시그니처가 StateMachine 전용임을 명시한다.** `void SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine> InRoot)` (`AnimStateMachineInstance.h:31`). 본체에서 `AnimGraphPtr` lazy 생성 후 `SetRoot(std::move(InRoot))` 호출 (`AnimStateMachineInstance.cpp:11-36`). `AnimGraph::SetRoot` 자체는 베이스 타입을 받지만(`AnimGraph.h:109`), AnimInstance 공개 API는 StateMachine 루트로 한정돼 있어 임의 root(Blend2/SequencePlayer가 root)에는 그대로 재사용 불가.

---

## 축 A — 직렬화

### 1. AnimGraph 컨테이너 구조 [Q1]

- `class AnimGraph` 정의: [AnimGraph.h:106-116](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:106)
- 노드 보관 방식: **단일 root 슬롯**. `std::unique_ptr<FAnimGraphNode_Base> Root;` ([AnimGraph.h:115](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:115))
- 다중 노드/연결 표현, 노드 추가·제거·교체 API는 없다. `SetRoot` 한 함수만 존재 ([AnimGraph.h:109](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:109)).
- root 외 노드들은 root 노드 자신이 자식으로 보유 — 컨테이너는 **소유 트리** 그 자체.
- 평가 진입점: `AnimGraph::Evaluate(const FAnimEvalContext&, TArray<FTransform>&)` ([AnimGraph.cpp:13-21](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp:13)). root가 null이면 bind pose로 안전 폴백.
- 노드 평가 시 공유 컨텍스트: `struct FAnimEvalContext` ([AnimGraph.h:27-33](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:27)) — `Skeleton`, `TimeSeconds`, `DeltaTime`, `OwningInstance`.

### 2. 노드 간 연결 표현 [Q2 — 직렬화 난이도의 핵심]

| 부모 노드 | 자식 참조 방식 | file:line |
| --- | --- | --- |
| `FAnimGraphNode_Blend2` | `std::unique_ptr<FAnimGraphNode_Base> ChildA, ChildB` (소유) | [AnimGraph.h:73-74](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:73) |
| `FAnimGraphNode_BlendN` | `TArray<std::unique_ptr<FAnimGraphNode_Base>> Children` (소유) | [AnimGraph.h:94](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:94) |
| `FAnimState` (StateMachine 안) | `std::unique_ptr<FAnimGraphNode_Base> Sub` (소유) | [AnimGraph_StateMachine.h:26](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:26) |
| `FAnimTransition` | `int32 FromStateIndex / ToStateIndex` (States 배열 인덱스) | [AnimGraph_StateMachine.h:51-52](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:51) |

**핵심 관찰** — 현재 표현은 **부모가 자식을 `unique_ptr`로 소유하는 트리**다. 노드 간 가로/공유 연결(같은 자식을 두 부모가 참조 등)이 없고, raw 포인터로 다른 노드를 가리키는 멤버도 없다. State→State 전이만 `int32` 인덱스로 표현된다.

**직렬화 난이도 측면 함의** —
- 일반적인 "raw 포인터 fix-up" 부담은 **없음**. 트리 순회로 깊이우선 저장/로드가 가능하다.
- 단, 직렬화 시 각 자식 슬롯이 nullptr인지 아닌지를 1바이트 플래그로 기록하는 패턴(예: `UAnimSequence::Serialize`에서 `bHasDataModel` — [AnimSequence.cpp:48-49](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:48))이 필요하다.
- `FAnimState::SubLengthHint`는 SequencePlayer일 때 `SetStateMachineGraph` 시점에 자동 도출돼 들어가는 캐시성 값이다([AnimStateMachineInstance.cpp:22-32](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:22)) — 직렬화 시 저장 정책 결정 필요 (저장 vs 재계산).

### 3. 노드 타입 식별 / 다형성 [Q3]

- 공통 베이스: `struct FAnimGraphNode_Base { virtual ~...; virtual void Evaluate(...) = 0; };` ([AnimGraph.h:38-42](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:38)).
- **타입 식별 enum: 없음.** 노드에 type id 같은 멤버가 없다.
- **RTTI 사용 흔적**: `dynamic_cast<FAnimGraphNode_SequencePlayer*>(S.Sub.get())` — [AnimStateMachineInstance.cpp:25](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:25). 즉 C++ RTTI는 빌드에 enabled.
- **팩토리·등록 테이블: 없음.** `REGISTER_FACTORY` 매크로는 `UObject` 파생(`UAnimInstance`, `UAnimSequence` 등 — [AnimSequence.cpp:13-16](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:13))에는 쓰이지만, `FAnimGraphNode_*`는 plain struct (UCLASS 아님)라 등록 대상이 아니다.
- **로드 시 타입 복원 수단: 부재.** 저장된 그래프를 읽을 때 "이 노드가 어떤 파생 타입인지"를 알려줄 디스크상 식별자 + 그 식별자를 받아 객체를 생성해주는 디스패치 둘 다 신규 도입 필요.
- 후보 (정책 결정 항목 — 진단에서 단정하지 않음):
  - (가) 노드별 `static constexpr uint32 TypeTag`를 직렬화 시 prefix로 기록 + switch/팩토리 함수 테이블로 디스패치.
  - (나) `EAnimGraphNodeType` enum 도입 + 각 노드가 자신의 enum을 반환하는 가상 함수 + 팩토리.
  - 어떤 형태든 "신규 노드 추가 시 한 곳에만 등록하면 되는" 등록 매크로 같은 패턴을 동반하는 게 일반적이지만, 도입 여부는 후속 설계에서 결정한다.

### 4. 노드별 Serialize 유무 [Q4]

`Source/Engine/Asset/Animation/Core` 범위에서 `Serialize|operator<<` grep — `Serialize`는 `Skeleton`, `FSkeleton`, `UAnimDataModel`, `UAnimSequence`에만 존재. AnimGraph 노드 군에는 0건.

존재하는 모든 노드 타입 (베이스 + 파생 4종) — `FAnimGraphNode` grep 결과 src 파일은 `AnimGraph.{h,cpp}`, `AnimGraph_StateMachine.{h,cpp}`, `AnimSingleNodeInstance.{h,cpp}`, `AnimStateMachineInstance.{h,cpp}`로 한정됨. 다른 노드 타입은 발견되지 않았다.

| 노드 타입 | Serialize 유무 | 정의 위치 |
| --- | --- | --- |
| `FAnimGraphNode_Base` (베이스) | 없음 | [AnimGraph.h:38-42](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:38) |
| `FAnimGraphNode_SequencePlayer` | 없음 | [AnimGraph.h:49-63](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:49) |
| `FAnimGraphNode_Blend2` | 없음 | [AnimGraph.h:71-82](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:71) |
| `FAnimGraphNode_BlendN` | 없음 | [AnimGraph.h:92-101](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:92) |
| `FAnimGraphNode_StateMachine` | 없음 | [AnimGraph_StateMachine.h:61-81](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:61) |

부속 데이터 타입(노드 일부지만 별도 struct):

| 타입 | Serialize 유무 | 정의 위치 |
| --- | --- | --- |
| `FAnimState` | 없음 | [AnimGraph_StateMachine.h:23-30](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:23) |
| `FAnimTransition` | 없음 | [AnimGraph_StateMachine.h:49-55](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:49) |
| `FAnimTransitionCondition` | 없음 | [AnimGraph_StateMachine.h:40-47](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:40) |
| `EAnimTransitionConditionKind` (enum) | trivial copy로 자동 직렬화 가능 (`uint8` 기반) | [AnimGraph_StateMachine.h:32-38](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:32) |

**참고 패턴** — 기존 직렬화 함수 `UAnimSequence::Serialize` ([AnimSequence.cpp:28-60](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:28))은 `FAssetFileHeader` 검증 → 자식 객체(`DataModel`)는 nullptr 플래그로 가드 후 재귀 호출하는 패턴이다. AnimGraph 노드 트리도 같은 패턴(자식 슬롯마다 1바이트 플래그 + 타입 태그 + 재귀)으로 일반화 가능하다 — 단 타입 태그 메커니즘(Q3) 도입이 선결.

**추가 노드 존재 가능성** — 현재 grep으로는 위 5종(베이스 1 + 파생 4) 외 추가 노드 클래스는 발견되지 않았다. 단, 표 외 파생 클래스가 다른 위치에 신규로 추가될 가능성은 항상 있다 — 직렬화 메커니즘 설계는 신규 노드 추가가 한 곳 등록으로 끝나도록 하는 것이 권장된다(정책 결정 항목).

### A-요약: `.asset` 파이프라인 재사용 가능성

확인된 사실로 한정:

- `FAssetFileHeader` (`Magic='ASET'(0x54455341)`, `EAssetType`, `Version`, `PayloadSize`) — [AssetFileHeader.h:17-39](KraftonEngine/Source/Engine/Asset/AssetFileHeader.h:17). `FArchive operator<<`도 함께 제공.
- `EAssetType` 현재 enumerant: Unknown / StaticMesh / SkeletalMesh / Skeleton / AnimSequence / Material / Texture2D / FbxScene — [AssetTypes.h:16-26](KraftonEngine/Source/Engine/Asset/AssetTypes.h:16). AnimGraph용 enumerant은 **없다** → 신규 enumerant 추가 필요.
- 파일 I/O: `FWindowsBinWriter / FWindowsBinReader` ([WindowsArchive.h](KraftonEngine/Source/Engine/Serialization/WindowsArchive.h)) — 그대로 재사용 가능.
- 기본 타입 직렬화 인프라: `FArchive operator<<` for trivially copyable, `std::string`, `FName`(문자열 라운드트립), `TArray<T>` (trivial은 bulk write) — [Archive.h:28-80](KraftonEngine/Source/Engine/Serialization/Archive.h:28).

→ 파이프라인 자체는 그대로 재사용 가능. AnimGraph asset에 필요한 신규 작업은 (i) `EAssetType::AnimGraph` enumerant 추가, (ii) 노드 타입 태그/디스패치(Q3), (iii) 각 노드의 `Serialize`(Q4) 셋이다. 파이프라인 자체에 손은 대지 않는다 (범위 외).

---

## 축 B — ImGui 노드 에디터

### 7. ImGui / 노드 에디터 현황 [Q7]

**ImGui 통합**:
- 위치: `KraftonEngine/ThirdParty/ImGui/`
- 버전: Dear ImGui v1.92.7 WIP — [ThirdParty/ImGui/imgui.h:1](KraftonEngine/ThirdParty/ImGui/imgui.h:1)
- 백엔드: DX11 (`imgui_impl_dx11.{h,cpp}`) + Win32 (`imgui_impl_win32.{h,cpp}`)
- 빌드 대상 cpp 목록: `ThirdParty/ImGui/sources.txt` (`imgui.cpp`, `imgui_demo.cpp`, `imgui_draw.cpp`, `imgui_impl_dx11.cpp`, `imgui_impl_win32.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`)
- 에디터 위젯 다수가 ImGui 사용 — 예: `Source/Editor/UI/ImGuiSetting.{h,cpp}`, `Source/Editor/UI/EditorPropertyWidget.cpp`, `Source/Editor/UI/SkeletalEditor/*` 다수.

**노드 에디터 라이브러리 vendored 여부**:
- `grep -ri "node-editor|NodeEditor|imnodes|ax::NodeEditor"` 결과 **0건**.
- `ThirdParty/` 직속 하위 디렉토리: `FBXSDK / ImGui / SFML / SimpleJSON / Sol` — 노드 에디터 라이브러리 디렉토리 없음.
- 결론: **노드 에디터 라이브러리는 전혀 vendored되어 있지 않다.**

**후보 (정책 결정 항목 — 단정하지 않음)**:
- (가) 외부 라이브러리 vendoring — 대표 후보로 [`imgui-node-editor` (thedmd/ax::NodeEditor)](https://github.com/thedmd/imgui-node-editor) 와 [`imnodes` (Nelarius)](https://github.com/Nelarius/imnodes) 가 있다. 라이센스, 기능 범위, 학습 곡선, 외부 의존 추가 비용이 트레이드오프 변수다.
- (나) 직접 구현 — ImGui DrawList 위에 베지어 와이어/노드/핀을 자체 구현. 단순한 그래프 한정으로는 가능하지만, 패닝/줌·핀 hit-test·다중 선택·undo·자동 레이아웃 등이 들어가면 비용이 빠르게 늘어난다.
- (다) 노드 에디터 UX 없이 트리/리스트 UI로 그래프를 구성 — 시각화는 약하지만 구현·유지보수 비용이 가장 낮다.
- 어느 안이 적절한지는 **사람 결정** 사안 (라이브러리 도입은 빌드/의존성 정책 결정).

---

## 축 C — 런타임 주입

### 5. AnimInstance ↔ AnimGraph 결선 [Q5]

- `UAnimInstance::AnimGraphPtr`: `std::unique_ptr<AnimGraph>` (owned) — [AnimInstance.h:91](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:91).
- 생성자에서 자동 생성하지 않음 (`UAnimInstance() = default` — [AnimInstance.cpp:11](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:11)). 파생 책임.
- public getter: `AnimGraph* GetAnimGraph() const` — [AnimInstance.h:65](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:65).
- base `UAnimInstance::EvaluateGraph`는 `AnimGraphPtr->Evaluate`를 그대로 호출 — [AnimInstance.cpp:87-108](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:87).

**기존 주입 진입점 표**:

| 인스턴스 | 그래프 보유 방식 | 외부 주입 API | file:line |
| --- | --- | --- | --- |
| `UAnimSingleNodeInstance` | `AnimGraphPtr` 미사용. 값-보유 `FAnimGraphNode_SequencePlayer` 한 개를 직접 평가. | `SetAnimation(UAnimSequence*)` — graph 전체가 아니라 시퀀스 ref만 교체. | [AnimSingleNodeInstance.h:37](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h:37), [AnimSingleNodeInstance.cpp:12-17](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:12), `EvaluateGraph` 우회 — [AnimSingleNodeInstance.cpp:29-50](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:29) |
| `UAnimStateMachineInstance` | base `AnimGraphPtr`에 `FAnimGraphNode_StateMachine`을 root로 set. | `SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine>)` — **StateMachine root 전용 시그니처.** | [AnimStateMachineInstance.h:31](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h:31), [AnimStateMachineInstance.cpp:11-36](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:11) |

**호출처 상황** — `grep "SetStateMachineGraph"`를 src 범위로 좁히면 정의·선언 위치 외 호출자가 0건이다. 즉 코드 경로상 외부에서 그래프를 주입하는 사용자가 아직 없다.

**런타임 교체 가능성**:
- StateMachine 인스턴스에 대해서는 가능 — `SetStateMachineGraph` 재호출 시 기존 `AnimGraphPtr`의 root가 `std::move`로 교체.
- 단, root가 StateMachine이 아닌 임의 노드(예: 단일 Blend2가 root, 혹은 단일 SequencePlayer가 root)인 그래프 주입은 **불가능**. `SetStateMachineGraph`의 인자 타입이 `FAnimGraphNode_StateMachine`으로 강타입화돼 있어 베이스 타입은 받지 않는다.
- 후보 (단정하지 않음):
  - (가) `AnimGraph::SetRoot(unique_ptr<FAnimGraphNode_Base>)`는 이미 베이스를 받으므로([AnimGraph.h:109](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:109)), AnimInstance 측에 베이스 타입을 받는 새 진입점(`SetAnimGraphRoot(unique_ptr<FAnimGraphNode_Base>)` 등)을 추가하거나, 그래프 자체를 한 단위로 받는 `SetAnimGraph(unique_ptr<AnimGraph>)`를 추가하는 방안.
  - (나) `SetStateMachineGraph`를 일반화해 시그니처 확장.
  - 어느 형태든 정책 결정. 새 노드 베이스를 받는 진입점이 추가되면 SubLengthHint 자동 도출 같은 StateMachine 특수 로직([AnimStateMachineInstance.cpp:22-32](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:22))을 어디로 옮길지 함께 결정 필요.

### 6. SkeletalMeshComponent → AnimInstance 경로 [Q6]

- 보유 멤버: `UAnimInstance* AnimInstance = nullptr;` (protected) — [SkeletalMeshComponent.h:116](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:116).
- 라이프사이클: 컴포넌트 소멸 시 `UObjectManager::DestroyObject` — [SkeletalMeshComponent.cpp:89-96](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:89).
- 인스턴스 선택 분기: `EAnimationMode` enum — `AnimationSingleNode` / `AnimationStateMachine` — [SkeletalMeshComponent.h:14-18](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:14).
- 인스턴스 생성: `EnsureAnimInstance` (protected, lazy) — [SkeletalMeshComponent.cpp:98-116](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:98). switch로 `UAnimSingleNodeInstance` / `UAnimStateMachineInstance` 선택.
- 모드 변경 → `PostEditProperty("Animation Mode")`에서 기존 인스턴스 destroy 후 재-Ensure — [SkeletalMeshComponent.cpp:257-282](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:257). 런타임 모드 변경 후 자동 재생성은 미지원이라고 헤더 주석에 명시 — [SkeletalMeshComponent.h:99-103](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:99).
- 외부에 인스턴스를 노출하는 public API: **없음** (멤버는 protected, getter도 없다). 컴포넌트 외부에서 `AnimInstance->GetAnimGraph`에 접근하려면 신규 API가 필요.
- 시리얼라이즈: `USkeletalMeshComponent::Serialize`는 `AnimToPlayPath`와 `AnimationMode`만 기록 — [SkeletalMeshComponent.cpp:191-201](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:191). AnimGraph asset 참조 경로는 아직 없다.

**root graph 주입의 최종 진입점 후보** (단정하지 않음):
- (가) 컴포넌트에 신규 public 메서드 — 예: `SetAnimGraphAsset(UAnimGraphAsset*)` 또는 `SetAnimGraphFromPath(FString)`. 모드 확장(`EAnimationMode::AnimGraph` enumerant 추가)과 함께 `EnsureAnimInstance` switch에 케이스 추가.
- (나) 기존 `EAnimationMode::AnimationStateMachine` 모드를 재해석 — 저장된 그래프를 로드해 root가 StateMachine이면 `SetStateMachineGraph`로 주입. root가 다른 타입이면 Q5의 신규 진입점이 선결.
- (다) 더 일반화된 새 모드 — 컴포넌트가 `UAnimGraphAsset` 같은 새 에셋 클래스의 경로 직렬화를 보유하고(`AnimToPlayPath`와 유사한 새 필드), 모드 결정 시 그래프 로드 후 적절한 AnimInstance 파생을 선택.
- 어느 형태로 가든 외부에서 `AnimInstance`에 접근하는 통로(컴포넌트의 public API)가 필요하다는 점은 공통.

---

## 작업 분할 제안

> 구현하지 않음. A·B·C를 어떤 순서로 쪼갤지, 각 단계의 선행 의존성만 제시.
> 권장안 형태로 적되, 의존성 근거에 file:line이나 위 절 참조를 둔다. 각 단계는 별도 진단/구현 문서로 다시 쪼개진다.

### 의존성 그래프 (사실관계)

- **에디터(B)가 만들어내는 in-memory 산출물 = 노드 트리.** 그 트리의 메모리 표현은 이미 정의돼 있다(축 A의 Q1·Q2). 에디터는 이 트리를 만들기만 하면 되므로, **B는 A·C 어느 쪽이 먼저 와도 진행 가능**하다. 단, 에디터가 의미를 가지려면 산출물을 (i) 디스크에 저장하거나(A), (ii) 런타임에 주입(C)할 수 있어야 한다.
- **C(런타임 주입)는 A(직렬화)와 독립적으로 가능.** 에디터가 만든 in-memory root를 컴포넌트의 새 API에 그대로 주입하면 동작 검증이 가능하다 — 저장/로드를 거치지 않아도 됨. 즉 C는 A를 선행 의존하지 않는다.
- **A(직렬화)는 Q3(타입 식별 메커니즘) 도입을 선결로 한다.** 노드별 `Serialize`를 적기 전에 타입 태그/팩토리가 자리잡아야 디스패치가 성립한다.
- **A는 C와 독립적으로 가능.** 단, 사람이 손으로 만든 dummy 트리를 저장/로드 라운드트립하는 단위 테스트만으로도 A의 검증은 닫힌다.

### 권장 분할 (의존성 근거 명시; 단일 정답으로 단정하지 않음)

권장 진행 순서는 **C → B → A** 이다 — 이유:
1. **C 먼저**: 현재 코드에서 가장 작은 변화로 가장 큰 검증을 얻는다. 컴포넌트에 root graph를 코드로 만들어 주입하는 진입점이 추가되면, 이미 완성돼 있는 `FAnimGraphNode_Blend2/BlendN/StateMachine` 평가 경로가 전체 회로로 한 번 닫힌다 — 픽스처 액터로 동작 검증이 즉시 가능. (근거: Q5에서 `SetStateMachineGraph`가 미호출 상태이고, Q6에서 컴포넌트 측 진입점 부재.)
2. **그 다음 B**: 에디터가 만들어내는 in-memory 결과를 C의 진입점에 흘려넣어 "에디터로 만든 그래프가 실제로 평가된다"는 회로를 닫는다. 라이브러리 도입 정책 결정이 이 단계의 선결.
3. **마지막에 A**: 직렬화는 "지금까지 잘 돌던 in-memory 그래프"를 디스크 라운드트립 가능하게 보존하는 작업. 이 단계가 마지막인 이유는 타입 태그/팩토리 메커니즘 도입이라는 정책 결정 무게가 크고(Q3), B의 노드 메타데이터(에디터 노드 좌표/주석 등)가 A의 저장 포맷에 영향을 줄 수 있어 — B가 어느 정도 형태를 갖춘 뒤 결정하는 편이 손실이 적기 때문이다.

**대안 순서** — A를 먼저(작은 dummy 트리부터 라운드트립 단위테스트 형태로), 이후 C, 마지막에 B로 가는 안도 유효하다. 결정은 사람.

각 단계는 독립 진단/구현 문서로 다시 쪼개진다 — 본 문서는 분할 권장까지로 한정한다.

---

## 확인 필요 / 정책 결정 항목

- **노드 에디터 라이브러리 도입 여부** — 외부 라이브러리(`imgui-node-editor` / `imnodes`) vs 직접 구현 vs 비-노드 UI. 빌드/의존성 정책 사안. (축 B)
- **노드 타입 식별 메커니즘 형태** — TypeTag prefix + 함수 테이블 vs enum + 가상함수 + 팩토리 vs 등록 매크로 도입. (축 A · Q3)
- **`UAnimStateMachineInstance::SetStateMachineGraph`를 일반화할지, 새 진입점을 만들지** — StateMachine 외 root 타입을 받을 통로가 필요하다. SubLengthHint 자동 도출 로직의 위치도 함께 결정. (축 C · Q5)
- **`SkeletalMeshComponent`의 외부 진입점 형태** — `SetAnimGraphAsset` 신설 vs 기존 모드 재해석 vs 일반화된 새 모드. `AnimationMode` enum 확장 여부 포함. (축 C · Q6)
- **`FAnimState::SubLengthHint` 직렬화 정책** — 캐시성 값이므로 저장하지 않고 로드 후 재계산할지, 그대로 저장할지. (축 A · Q4 표 비고)
- **신규 `EAssetType::AnimGraph` enumerant 추가 시점** — A 작업 진입 시 함께. `.asset` 파이프라인 자체는 손대지 않음(범위 외 원칙). (축 A 요약)

---

## 범위 외 관찰 사항 (기록만, 진단하지 않음)

- `UAnimInstance::Update`가 Notify trigger 판정만 하고 dispatch는 안 함 — 본 진단의 관심사 아님 ([AnimInstance.cpp:73-84](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:73)).
- `UAnimSingleNodeInstance::EvaluateGraph`에 임시 진단 로그 `[DIAG][root_rotation_coordsys_verification]` 잔존 — 본 작업 무관 ([AnimSingleNodeInstance.cpp:52-67](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:52)).
- `UAnimDataModel::Serialize`에 `CurveData` 포함 — 본 진단 범위 외 ([AnimSequence.cpp:25](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:25)).
- `.asset` 파이프라인(`FAssetFileHeader` / `FWindowsBinReader/Writer`) 자체 — 재사용만 확인했고 손대지 않음.
- ContentBrowser에서 AnimGraph asset이 어떻게 분류·아이콘 표시될지 — 별도 사안.
