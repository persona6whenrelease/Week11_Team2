# C(런타임 주입) 구현 계획

> 계획 전용. 코드 변경 없음. 확정 설계((B,B) 조합, 수동 매크로,
> mode swap UX = StateMachine과 동일, 검증 = 콘솔 커맨드)는
> 전제이며 재논의하지 않는다. file:line은 "계획 전 최소 검증"
> 항목만 갱신.

---

## TL;DR — 작업 단위와 순서 요약

**계획 전 최소 검증 결과**: 선행 진단([animinstance_injection_diagnosis.md](Document/animinstance_injection_diagnosis.md))과 일치. `UAnimStateMachineInstance`는 `DECLARE_CLASS`/`IMPLEMENT_CLASS` 수동 매크로 사용([AnimStateMachineInstance.h:21](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h:21), [AnimStateMachineInstance.cpp:7](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:7)); `EAnimationMode`는 2개 case + `EnsureAnimInstance` switch는 StateMachine 명시 case + default가 SingleNode를 흡수([SkeletalMeshComponent.h:14-18](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:14), [SkeletalMeshComponent.cpp:101-110](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:101)); `PostEditProperty` "Animation Mode"의 비-SingleNode else 분기가 `AnimToPlay = nullptr; EnsureAnimInstance();`로 graph 모드를 그대로 흡수 가능([SkeletalMeshComponent.cpp:269-277](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:269)); `PostDuplicate`는 destroy+SetAnimation 패턴([SkeletalMeshComponent.cpp:203-223](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:203)); `GAnimationModeNames[]` 라벨 배열([SkeletalMeshComponent.cpp:23-26](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:23))이 enum과 동기 필수. 콘솔 커맨드 인프라는 `FEditorConsoleWidget` 형태로 존재([EditorConsoleWidget.h:64-94](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.h:64)) — `RegisterCommand` + 카테고리별 핸들러. 단위테스트 프레임워크는 부재(0건) → 검증은 콘솔 커맨드 단일 형태로 확정.

**작업 단위 / 순서**:

| 순서 | 단위 | 한 줄 요약 |
|---|---|---|
| 1 | C-1 | 새 파생 `UAnimGraphInstance` 헤더/소스 (수동 매크로) |
| 2 | C-2 | `EAnimationMode::AnimationGraph` 추가 + `GAnimationModeNames` 라벨 추가 + `EnsureAnimInstance` switch case 추가 |
| 3 | C-3 | `PostEditProperty` / `PostDuplicate` 검토 — 정책 "허용한다"(빈 root 진입) 하에서 현행 분기 그대로 사용 가능 여부 확인 후 필요 시만 미세 조정 |
| 4 | C-4 | `USkeletalMeshComponent::SetRootGraph` 위임 메서드 신설 |
| 5 | C-5 | `FEditorConsoleWidget`에 검증 커맨드 추가 (`anim.graph.test` 또는 그 자리) |

---

## 1. 작업 단위 분할              [P1]

선행 진단의 (B,B) 조합 구현을 다음 5개 단위로 분할한다. 각 단위는 하나의 구현 프롬프트가 다룰 수 있는 크기.

### C-1: 새 파생 `UAnimGraphInstance` 신설

**책임**: graph 전용 AnimInstance 파생을 한 쌍의 헤더/소스로 추가. `EvaluateGraph` override 없음(베이스가 `AnimGraphPtr->Evaluate`를 그대로 호출하게 둠). 등록 매크로는 수동(`DECLARE_CLASS` + `IMPLEMENT_CLASS`).

**산출 파일** (신규):
- `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.h`
- `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.cpp`

**선행 의존성**: 없음(독립).

### C-2: `EAnimationMode` 확장 + 라벨 + `EnsureAnimInstance` switch

**책임**: 새 모드 `AnimationGraph` 추가, `GAnimationModeNames` 배열에 라벨 한 항목 추가(인덱스 동기 필수 — 라벨 표시는 [GetEditableProperties](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:225)가 enum 정수 인덱스로 참조), `EnsureAnimInstance` switch에 `case AnimationGraph: CreateObject<UAnimGraphInstance>(this);` 추가.

**산출 파일** (수정):
- [KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h) — enum case 추가.
- [KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) — `GAnimationModeNames` 배열 + `EnsureAnimInstance` switch + `AnimGraphInstance.h` include.

**선행 의존성**: C-1 (switch case 본문이 `UAnimGraphInstance::StaticClass()`를 참조). C-1 없이 C-2만 적용하면 컴파일 실패.

### C-3: `PostEditProperty` / `PostDuplicate` mode swap 경로 검토·반영

**책임**: 현행 `PostEditProperty` "Animation Mode" 분기([SkeletalMeshComponent.cpp:257-282](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:257))의 else 가지가 `AnimToPlay = nullptr; EnsureAnimInstance();`로 끝나므로, 신규 graph 모드는 **현행 else 가지로 자동 흡수된다**. UX 정책이 "graph 모드 진입 시 root가 빈 상태로 들어가는 것을 허용"이므로 **추가 분기·가드 불필요**. `PostDuplicate`도 `ResolveCompatibleAnimSequence` → `SetAnimation(...)` 경로가 graph 모드에서 `nullptr` 분기로 들어가 `SetAnimation(nullptr)` → 내부 `EnsureAnimInstance` 호출로 마무리되므로 **추가 변경 불필요**.

따라서 C-3는 **검토 단위**다. 다음을 확인하고, 어긋남이 있으면 그때만 미세 분기 추가:
- [SkeletalMeshComponent.cpp:269-277](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:269) else 가지가 신규 graph 모드에서도 동일하게 동작하는가(기대: 그대로 사용).
- [SkeletalMeshComponent.cpp:182](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:182) `BeginPlay`의 early return(`AnimationMode != AnimationSingleNode || !AnimToPlay`)이 graph 모드를 자연스럽게 skip하는가(기대: 그대로 skip — root는 별도 주입 경로로 채우므로 `BeginPlay`에서 할 일 없음).
- [SkeletalMeshComponent.cpp:195](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:195) `Serialize`가 enum의 새 정수값을 안전히 round-trip하는가(기대: 그대로 — int 캐스팅 경로이므로 신규 값은 그냥 저장/복원).

**산출 파일**: 기대상 **변경 없음**. 만약 위 검토에서 어긋남 발견 시에만 [SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)에 명시 분기 추가.

**선행 의존성**: C-2 (새 enum case가 있어야 검토 의미가 있음).

### C-4: `USkeletalMeshComponent::SetRootGraph` 위임 메서드

**책임**: 컴포넌트 public 인터페이스로 단일 진입점 `SetRootGraph(std::unique_ptr<FAnimGraphNode_Base>)` 신설. 본문은 (a) 모드 확인 → (b) `EnsureAnimInstance()` 선호출 → (c) `Cast<UAnimGraphInstance>` → (d) cast 성공 시 파생의 root 주입 메서드 호출.

**산출 파일** (수정):
- [KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h) — public 선언 + `FAnimGraphNode_Base` 사용을 위해 `AnimGraph.h` include(현 헤더는 `AnimInstance.h`만 include).
- [KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) — 본문 정의 + `AnimGraphInstance.h` include (C-2에서 이미 추가 가능).

**선행 의존성**: C-1 (cast 대상 타입) + C-2 (graph 모드 case가 `EnsureAnimInstance`에서 적절한 파생을 생성하지 않으면 cast 실패). C-3는 의존성 아님(C-3는 검토 단위라 코드 변경이 없을 가능성이 큼).

### C-5: 검증 콘솔 커맨드

**책임**: `FEditorConsoleWidget`에 단일 커맨드(예: `anim.graph.test`) 등록. 커맨드는 현재 viewport/level에서 한 `USkeletalMeshComponent`를 찾아, 코드로 `make_unique`한 root graph(SequencePlayer 단일 노드 또는 Blend2)를 만들어 `SetRootGraph` 호출, 한 tick 강제 평가 후 출력 포즈가 bind pose와 다른지 단언/로그.

**산출 파일** (수정):
- [KraftonEngine/Source/Editor/UI/EditorConsoleWidget.h](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.h) — 신규 핸들러 선언(예: `HandleAnimGraphTest`) + 카테고리 등록 메서드 추가 또는 기존 `RegisterDiagnosticsCommands`에 한 줄 등록.
- [KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp) — 핸들러 본문 + `RegisterCommand("anim.graph.test", ...)` 등록.

**선행 의존성**: C-1, C-2, C-4 모두 필요(검증이 만지는 통로 전체).

### 합치거나 더 쪼갤 단위 판단

- **C-2와 C-3 합치기**: 가능. C-3는 검토 단위로 코드 변경이 0건일 가능성이 높음. 단, 분리 유지가 더 명확(코드 변경 vs 검토 책임 구분). **분리 유지 권장**.
- **C-1과 C-2 합치기**: 비권장. C-1만 적용해도 컴파일 안정(미사용 신규 클래스). C-2와 함께 묶으면 단일 프롬프트 변경 면적이 두 디렉토리(Asset/Animation/Core + Component/)에 걸침. 분리 시 각 프롬프트가 한 디렉토리만 본다 — 리뷰 편의 ↑.
- **C-4를 C-2와 합치기**: 비권장. C-4는 신규 public API 신설로, 인터페이스 결정·정책(모드 가드)이 별도 검토 가치를 가짐.
- **C-5 분리 유지**: 필수. 검증 자체가 한 프롬프트의 책임이고, 검증이 실패하면 C-1~C-4 중 어디가 깨졌는지 진단하는 단위 역할.

---

## 2. 작업 단위 의존성 순서       [P2]

```
C-1 → C-2 → C-3 → C-4 → C-5
```

각 화살표 근거:

- **C-1 → C-2**: C-2의 `EnsureAnimInstance` switch case 본문이 `UObjectManager::Get().CreateObject<UAnimGraphInstance>(this)`를 호출. `CreateObject<T>`는 `T::StaticClass()`만 요구하므로(선행 진단 §4), C-1이 RTTI(`DECLARE_CLASS`/`IMPLEMENT_CLASS`)를 완료한 뒤여야 컴파일·링크가 통과. **C-1 단독 적용 시점에서 트리는 컴파일 안정**(미사용 신규 클래스).
- **C-2 → C-3**: C-3는 신규 enum case가 존재하는 상태에서 기존 `PostEditProperty`/`PostDuplicate`/`BeginPlay`/`Serialize` 분기를 검토·검증하는 단위. C-2 없이는 검토 대상 자체가 없음.
- **C-3 → C-4**: 약한 의존(코드상 강제는 아님). C-4가 호출하는 `EnsureAnimInstance`가 graph 모드에서 올바른 파생을 만들어내는지의 정합성은 C-2가 보장(C-3는 검토 단위). 그러나 *흐름상* C-3 검토를 마치고 C-4 진행이 자연스러움. C-3 코드 변경이 0건이면 C-4와 동일 프롬프트에 묶어도 무방(파일 면적도 동일 → SkeletalMeshComponent).
- **C-4 → C-5**: C-5의 콘솔 커맨드 본문이 `SkelMeshComp->SetRootGraph(...)`을 호출. C-4 없이는 호출할 통로 자체가 없음.

**컴파일 안정성**: 각 단계 적용 직후 트리가 빌드 가능해야 한다. 위 순서는 그 조건을 충족한다. 특히:
- C-1만 적용 → 신규 클래스는 어디서도 참조되지 않음 → 빌드 OK.
- C-2만 적용 → 신규 enum case + switch case + 라벨 배열 추가. C-1 산출물이 있어야 `CreateObject<UAnimGraphInstance>` 컴파일 가능. → C-1 후행 적용이라야 OK.
- C-3는 검토 단위 — 적용 후 코드 변경 0건이면 빌드 무변화.
- C-4만 적용 → `SetRootGraph` public API 추가. 호출자 0건이라도 빌드 OK.
- C-5 적용 → 모두 의존하지만, 모두 이미 들어왔으면 OK.

---

## 3. `UAnimGraphInstance` 파일 구성 [P3]

### 레퍼런스

[AnimStateMachineInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h) / [AnimStateMachineInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp). 동일 패턴 적용 + 두 가지 의도적 차이.

### 헤더 `AnimGraphInstance.h`

**Include**:
- `"Asset/Animation/Core/AnimInstance.h"` (베이스)
- `"Asset/Animation/Core/AnimGraph.h"` (`FAnimGraphNode_Base` 베이스 타입을 인자로 받는 메서드 시그니처에 필요)
- `<memory>` (`std::unique_ptr`)

**클래스 선언 책임**:
- `class UAnimGraphInstance : public UAnimInstance`
- `DECLARE_CLASS(UAnimGraphInstance, UAnimInstance)` — `UAnimStateMachineInstance`와 형식 일치(수동 매크로). `UCLASS+GENERATED_BODY` codegen 사용 안 함.
- 기본 ctor / `~UAnimGraphInstance() override = default;`
- public 메서드 한 개: `void SetRootGraph(std::unique_ptr<FAnimGraphNode_Base> InRoot);` — root를 받아 베이스의 `AnimGraphPtr`(protected 멤버)에 lazy 생성 + `SetRoot(std::move(InRoot))`.

**의도적으로 두지 않는 것** (대조점):
- `EvaluateGraph()` override **없음**. 베이스 `UAnimInstance::EvaluateGraph`([AnimInstance.cpp:87-108](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:87))가 `AnimGraphPtr->Evaluate`로 위임하므로 그대로 사용. `UAnimStateMachineInstance`와 동일 패턴 — silent dead injection 구조적 차단.
- concrete-typed 주입 메서드 **없음** (`UAnimStateMachineInstance::SetStateMachineGraph`처럼 `FAnimGraphNode_StateMachine` 한정 시그니처를 두지 않음). graph 모드는 root 노드 타입을 가리지 않음이 핵심.
- `SetStateMachineGraph`의 `SubLengthHint` 자동 도출 부수효과([AnimStateMachineInstance.cpp:22-33](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:22))는 **이전하지 않음**(선행 진단의 범위 외 관찰 사항). graph 모드 사용자는 root에 StateMachine이 들어 있어도 SubLengthHint를 직접 설정하거나 별도 helper 사용.
- 추가 변수/맵(`BoolVariables` 류) **없음**. 베이스의 변수 훅이 필요해지면 이후 단계에서 확장.

### 소스 `AnimGraphInstance.cpp`

**Include**:
- `"Asset/Animation/Core/AnimGraphInstance.h"`
- `"Asset/Animation/Core/AnimGraph.h"`
- `"Object/ObjectFactory.h"` (`IMPLEMENT_CLASS` 매크로)

**책임**:
- 첫 줄에 `IMPLEMENT_CLASS(UAnimGraphInstance, UAnimInstance)` — `DEFINE_CLASS` + `REGISTER_FACTORY` 일괄 처리(선행 진단 §4). 형식상 `UAnimStateMachineInstance`와 일치.
- `UAnimGraphInstance::UAnimGraphInstance() = default;`
- `SetRootGraph` 본문: `if (!AnimGraphPtr) AnimGraphPtr = std::make_unique<AnimGraph>(); AnimGraphPtr->SetRoot(std::move(InRoot));` — `UAnimStateMachineInstance::SetStateMachineGraph`의 lazy 패턴([AnimStateMachineInstance.cpp:11-36](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:11))과 동일하되 `SubLengthHint` 도출 루프는 없음.

### root 주입에 필요한 것은 무엇뿐인가

선행 진단 §1·§6에서 확인된 대로: 베이스의 `AnimGraphPtr` 슬롯([AnimInstance.h:91](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:91)) + `AnimGraph::SetRoot(unique_ptr<FAnimGraphNode_Base>)`([AnimGraph.h:109](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:109))만으로 충분. `UAnimGraphInstance`는 두 기존 자원을 묶어 lazy 생성하는 한 메서드만 더한다.

---

## 4. `SetRootGraph` 위임 메서드   [P4]

### 본문 단계 (순서 명시)

1. **모드 확인**: `if (AnimationMode != EAnimationMode::AnimationGraph) ...` — 처리는 아래 정책 옵션 중 하나.
2. **`EnsureAnimInstance()` 호출**: 컴포넌트 멤버 함수이므로 protected 접근 OK ([SkeletalMeshComponent.h:103](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:103)). idempotent이므로 다중 호출 안전([SkeletalMeshComponent.cpp:100](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:100)).
3. **cast**: `auto* Graph = Cast<UAnimGraphInstance>(AnimInstance);` — `Cast<>`는 엔진 RTTI 기반.
4. **cast 실패 처리**: `if (!Graph) { /* 로그 또는 무시 */ return; }`. 정상 흐름에서는 모드 확인(1단계)이 통과하면 cast는 성공해야 함. 실패는 모드 강제 swap을 안 했거나 외부에서 `AnimInstance`를 강제로 다른 타입으로 갈아끼웠을 때 등 비정상.
5. **root 주입**: `Graph->SetRootGraph(std::move(InRoot));` — C-1의 파생 메서드 호출. `unique_ptr` move 한 번이면 소유권 이전 완료(선행 진단 §6의 소유 사슬 그대로).

### 1단계 모드 처리 정책 (정책 결정 항목 — 5절 끝에서 다시)

호출 시점에 `AnimationMode != AnimationGraph`인 경우 세 옵션:

| 옵션 | 거동 | 트레이드오프 |
|---|---|---|
| (i) 강제 swap | `AnimationMode = AnimationGraph; if (AnimInstance) { DestroyObject; null; } EnsureAnimInstance();` | 호출자 편의 최대. 단, 모드를 무성으로 바꿔 외부 UI(`GetEditableProperties`의 enum 콤보)와 silent 불일치 가능. |
| (ii) 경고 후 early return | 로그 한 줄 + `return` — `InRoot`는 소멸. | 외부가 명시적으로 모드를 먼저 set해야 함. 의도 표명 강제. 안전. |
| (iii) 자동 ensure 후 cast 실패 의존 | 모드 체크 없이 `EnsureAnimInstance` → cast → null이면 fail. | 옵션 (ii)의 약한 버전. 실패 사유가 cast 단계로 미뤄짐 — 진단 메시지가 모호. |

**권장**: 옵션 (ii). 이유: graph 모드는 외부(에디터/Lua/콘솔)가 의도적으로 선택해야 의미가 있는 모드. silent swap은 mode 변경 트리거가 어디서 발생했는지 추적을 어렵게 함. 단, 검증 콘솔 커맨드(C-5)는 사용 편의를 위해 옵션 (i)의 강제 swap을 커맨드 본문에서 명시적으로 수행(컴포넌트 API 자체는 (ii)를 유지하고, 커맨드가 외부에서 mode set → SetRootGraph 두 줄로 호출).

이 정책은 5절·"확인 필요" 절에서 사람 결정으로 다시 표기.

### 헤더 변경

- [SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h) public 섹션에 한 줄 선언 추가:
  ```
  void SetRootGraph(std::unique_ptr<FAnimGraphNode_Base> InRoot);
  ```
- include 추가: `"Asset/Animation/Core/AnimGraph.h"` — `FAnimGraphNode_Base` 완전 타입이 `std::unique_ptr` 시그니처에 필요(헤더에서 `std::unique_ptr<T>` 파라미터의 move/소멸 처리상 완전 타입 요구). 현 헤더는 `"Asset/Animation/Core/AnimInstance.h"`만 include([SkeletalMeshComponent.h:4](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:4)).

### cast 실패 시 거동

- 정책 (ii) 채택 시: 모드 체크에서 이미 걸러져 거의 발생 안 함. cast 실패가 발생할 수 있는 잔여 경로는 외부가 `EnsureAnimInstance`를 우회하는 시나리오 — 그런 경로는 현 트리에 없음(선행 진단 §3).
- 본문에서는 `if (!Graph) return;` 한 줄로 안전 종료. `InRoot`는 자동 소멸(누수 없음 — `unique_ptr`).

---

## 5. 검증 작업 단위 (C-5)         [P5]

### 형태 확정

콘솔 커맨드 단일 형태. `FEditorConsoleWidget`이 이미 ImGui 기반 커맨드 디스패치를 제공([EditorConsoleWidget.h:79](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.h:79), [EditorConsoleWidget.h:82](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.h:82)) — 신규 핸들러를 `RegisterDiagnosticsCommands` 또는 새 카테고리 메서드(예: `RegisterAnimCommands`)에 등록.

### 등록 위치 옵션

- **옵션 A — 기존 `RegisterDiagnosticsCommands`에 한 줄 추가**: 가벼움. 카테고리는 "Diagnostics"로 묶임.
- **옵션 B — 새 `RegisterAnimCommands` 카테고리 신설**: 향후 anim 관련 커맨드가 더 늘어날 가능성에 대비. 현재로선 과한 분할.

**권장 옵션 A**. 검증 커맨드 1개부터 시작 — 추후 anim 디버그 커맨드가 늘면 옵션 B로 리팩토링.

### 커맨드 동작

커맨드명(가칭): `anim.graph.test` 또는 `test.anim.graph`. 인자 없음(또는 옵션 인자로 root 노드 타입 선택).

**본문 단계**:
1. 현재 viewport/level의 선택된 actor 또는 첫 `USkeletalMeshComponent`를 찾는다. (확인 필요: 에디터 측 선택 actor 조회 API 위치 — 본 계획은 등록 위치까지만 결정하고, 조회 경로는 구현 프롬프트가 채움.)
2. 컴포넌트의 `USkeleton`/`SkeletalMesh`가 비어 있는지 확인 — 비어 있으면 에러 로그 후 종료.
3. 검증용 root graph를 코드로 조립:
   - **권장**: `make_unique<FAnimGraphNode_SequencePlayer>()`로 단일 노드 root. 컴포넌트의 mesh에 호환 `UAnimSequence`(예: `FMeshManager::FindAnimSequenceForSkeletalMesh`를 통해 첫 시퀀스 1개)를 set. 평가 시점 `0.5 * PlayLength` 정도로 advance.
   - **대안**: `make_unique<FAnimGraphNode_Blend2>()` + 두 SequencePlayer 자식 + α=0.5. 더 graph스러운 root이지만 시퀀스 2개 확보가 필요.
   - **선택 가이드**: 컴포넌트 mesh에서 첫 시퀀스 1개를 안정적으로 뽑을 수 있다면 SequencePlayer로 충분. bind pose와의 차이만 확인하면 되므로 가장 단순한 root가 적절.
4. `AnimationMode = AnimationGraph;` 강제 set (커맨드 측에서 명시적 swap).
5. 기존 `AnimInstance`가 있으면 destroy + null (PostEditProperty의 mode swap 패턴 모방, [SkeletalMeshComponent.cpp:263-267](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:263) 동일 형태).
6. `SkelMeshComp->SetRootGraph(std::move(Root));` 호출 — 내부에서 `EnsureAnimInstance` + cast + 주입.
7. 강제 한 tick 평가: `SkelMeshComp->RefreshAnimationPose();` ([SkeletalMeshComponent.cpp:352-365](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:352)) — `AnimInstance->EvaluateGraph` + `ApplyEvaluatedPose` 경로를 한 번 회전.
8. **단언**: 출력 포즈가 bind pose와 다른가? 두 후보:
   - (a) `AnimInstance->GetOutputLocalPose()`를 가져와 `USkeleton`의 ref pose(`USkeleton::GetReferencePose()` 또는 등가 — 확인 필요)와 N개 본의 `FTransform`을 비교. `Translation`/`Rotation` 둘 다 같으면 fail.
   - (b) 더 간단히: `OutputLocalPose`의 본 1~2개에서 ref pose 대비 `FQuat::Dot < 0.999f` 등 임계로 차이를 단언.
9. 결과를 로그(`UE_LOG` 등가 또는 콘솔 출력 device `FEditorConsoleWidget::AddLog([EditorConsoleWidget.h:39](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.h:39))`)로 보고: "PASS — root graph evaluated, N bones differ" 또는 "FAIL — pose matches bind pose".

### 검증이 확인하는 것 (재명시)

- 통로의 살아 있음 (`SetRootGraph` 호출 → `EnsureAnimInstance` → cast → 주입까지 한 번이라도 닫힘)
- 평가 회로의 살아 있음 (`AnimInstance->EvaluateGraph` → 베이스 구현 → `AnimGraphPtr->Evaluate` → root 노드의 `Evaluate`까지 도달)
- 결과 포즈의 비-trivial성 (단순 bind pose가 아님)

이 셋이 모두 닫히면 C 단계의 통로 신설이 의도대로 동작한다는 증거가 된다.

---

## 6. 회귀 안전 점검             [P6]

C-1~C-5 적용이 기존 모드(`AnimationSingleNode`, `AnimationStateMachine`)의 거동을 바꾸지 않음을 보장하기 위한 점검 항목.

### 점검 항목

1. **`EnsureAnimInstance` switch의 기존 case 무영향**: 새 case `AnimationGraph`는 명시 case로 추가. 기존 `AnimationStateMachine` case와 default(SingleNode를 잡는) 분기는 그대로. → 기존 모드의 인스턴스 생성 경로 무변화.
2. **`EAnimationMode` 정수값 안정성**: 기존 enum 값 순서를 바꾸지 말 것. `AnimationGraph`는 **끝에 추가**. 이유: `Serialize`([SkeletalMeshComponent.cpp:191-201](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:191))가 enum을 정수로 round-trip하므로, 기존 저장 씬에 저장된 `AnimationSingleNode=0` / `AnimationStateMachine=1`이 그대로 복원돼야 함.
3. **`GAnimationModeNames` 배열 인덱스 동기**: enum의 끝에 새 case를 추가했다면 배열 끝에도 라벨을 추가. 라벨 갯수와 enum 갯수 불일치는 `GetEditableProperties`([SkeletalMeshComponent.cpp:233](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:233))의 `std::size(GAnimationModeNames)`로 전달되는 옵션 수와 enum 값 영역 불일치를 일으켜 UI에서 잘못된 라벨 표시 또는 out-of-range 접근.
4. **`PostEditProperty` else 가지의 의미 회귀**: 현행 else가 `AnimToPlay = nullptr; EnsureAnimInstance();`로 처리하는데(StateMachine과 새 graph 모두 흡수), StateMachine 거동이 그대로 유지되는지 확인. 변경 없음이 기대값.
5. **`PostDuplicate` 경로의 SingleNode·StateMachine 거동 불변**: `ResolveCompatibleAnimSequence` → `SetAnimation` 패턴은 SingleNode에서만 의미가 있고 StateMachine/graph는 `SetAnimation(nullptr)`으로 흡수 — 기존 StateMachine 거동(빈 root + `EnsureAnimInstance`만)이 동일.
6. **`BeginPlay` early return의 SingleNode 거동 불변**: `if (AnimationMode != AnimationSingleNode || !AnimToPlay) return;` — SingleNode + 시퀀스 있는 정상 경로는 변경 없음. graph 모드는 그대로 early return(의도된 거동).
7. **기존 씬·자산 영향**: 기존 저장 데이터의 `AnimationMode` 필드는 0/1만 가짐 → 항상 SingleNode/StateMachine로 복원. 새 enum 값(2)을 가진 데이터는 신규 작성된 것만 존재 → 회귀 없음.
8. **`UAnimStateMachineInstance::SetStateMachineGraph` 사용처 영향**: 본 메서드는 그대로 유지. C 단계는 새 통로(`UAnimGraphInstance::SetRootGraph` + 컴포넌트 위임)를 신설할 뿐 기존 StateMachine 통로를 건드리지 않음.
9. **`UAnimSingleNodeInstance`의 `EvaluateGraph` override 무관성**: SingleNode 인스턴스는 그대로 `SequencePlayer` 직접 평가 — `AnimGraphPtr` 미사용 경로([AnimSingleNodeInstance.cpp:31](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:31)) 무변화.

### 잠재 위험 (낮음, 점검만)

- `SetAnimation`(SingleNode helper)이 내부에서 `EnsureAnimInstance`를 호출하는 시점([SkeletalMeshComponent.cpp:150](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:150))에서 `AnimationMode`가 `AnimationGraph`이면 `UAnimGraphInstance`가 생성됨. 그 다음 `Cast<UAnimSingleNodeInstance>` 시도가 nullptr 반환 → `SetAnimation` 본문이 조용히 종료([SkeletalMeshComponent.cpp:152-155](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:152)). 의도된 거동(SingleNode helper는 graph 모드에서 무의미). 변경 없음.

---

## 구현 프롬프트 분할 제안

각 작업 단위를 하나의 구현 프롬프트로 처리 가능한 크기로 잡았다. 순서대로:

| 프롬프트 | 다루는 단위 | 수정/신규 파일 | 검증 신호 |
|---|---|---|---|
| P-C-1 | C-1 신규 derived | (신규) `AnimGraphInstance.h`, `AnimGraphInstance.cpp` | 빌드 통과. 신규 클래스는 미사용. |
| P-C-2 | C-2 enum + 라벨 + switch | (수정) `SkeletalMeshComponent.h`, `SkeletalMeshComponent.cpp` | 빌드 통과. 에디터 콤보에 새 모드 라벨이 보임(수동 확인). 모드 swap 시 `UAnimGraphInstance` 인스턴스가 생성됨(로그·디버거로 확인 가능). |
| P-C-3 | C-3 mode swap 경로 검토 | (기대) 변경 없음. 어긋남 발견 시에만 `SkeletalMeshComponent.cpp` 미세 수정 | 정책 "허용한다" 하에서 PostEditProperty / PostDuplicate / BeginPlay / Serialize의 graph 모드 진입이 빈 root로 안전히 들어감을 확인. |
| P-C-4 | C-4 `SetRootGraph` 위임 | (수정) `SkeletalMeshComponent.h` (include + 선언), `SkeletalMeshComponent.cpp` (본문) | 빌드 통과. public API 추가. 호출자 0건이라도 OK. |
| P-C-5 | C-5 콘솔 커맨드 | (수정) `EditorConsoleWidget.h` (핸들러 선언), `EditorConsoleWidget.cpp` (등록 + 본문) | 콘솔에 `anim.graph.test` 실행 시 "PASS — N bones differ" 로그. |

**프롬프트 작성 시 주의**:
- 각 프롬프트는 위 단위만 다루고, 다음 단위를 미리 만지지 말 것. 단위 경계가 분리되어야 회귀 발생 시 어느 단위에서 깨졌는지 추적 가능.
- P-C-1 프롬프트가 "main` 함수에 호출자도 추가"식으로 단위를 넘어가면 의존성 순서가 깨짐.
- P-C-5는 정책 결정(아래 절) 답변을 받은 뒤 시작.

---

## 확인 필요 / 정책 결정 항목

계획 단계에서 미해결로 남는 것:

1. **`SetRootGraph` 모드 체크 정책** (4절): 옵션 (i) 강제 swap / (ii) 경고+early return / (iii) cast 실패 의존. **권장 (ii)** — 커맨드 측에서 명시 swap. 사람 결정 필요.
2. **검증 커맨드의 root 조립 형태** (5절): SequencePlayer 단일 노드 / Blend2 / 둘 다 지원(서브커맨드 분기). **권장 SequencePlayer 단일 노드** — 가장 단순. 사람 결정 필요.
3. **검증 커맨드의 대상 컴포넌트 선택 경로** (5절 step 1): 현재 선택된 actor의 `USkeletalMeshComponent` / 첫 발견 / 인자로 path 받기. 에디터 측 선택 actor 조회 API의 위치는 본 계획에서 확인하지 않았음 — 구현 프롬프트가 채울 영역. (Confirmation level: 낮음.)
4. **bind pose 비교의 단언 기준** (5절 step 8): `FQuat::Dot` 임계 / `Translation` 차이 임계 / 본 인덱스 선택. 한 임계로 통일할지, 본별로 다를지. **권장**: 한 본(예: 루트가 아닌 첫 자식 본)에서 `Rotation`의 `FQuat::Dot`를 보고 `<= 0.999f`이면 PASS. 사람 결정 필요.
5. **콘솔 커맨드 등록 카테고리** (5절): 기존 `RegisterDiagnosticsCommands` 안 / 새 `RegisterAnimCommands` 신설. **권장 기존 Diagnostics 안에**. 사람 결정 필요.

---

## 범위 외 관찰 사항 (기록만)

- A 단계(직렬화) · B 단계(노드 에디터)는 본 계획의 범위가 아님 — 선행 진단의 "범위 외" 분류를 그대로 따름.
- `UAnimStateMachineInstance::SetStateMachineGraph`의 `SubLengthHint` 자동 도출 부수효과는 `UAnimGraphInstance`로 이전하지 않음 — graph 모드 사용자의 책임 또는 별도 helper로 분리(정책 결정 사항, 본 계획에서 답하지 않음).
- 단위 테스트 프레임워크 부재(0건 확인). 추후 도입 시 C-5 콘솔 커맨드를 자동화된 테스트로 재포장 가능.
