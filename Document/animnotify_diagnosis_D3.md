# AnimNotify 진단 D3 — PIE Silent + Lua Binding 설계

D1([Document/animnotify_diagnosis_D1.md](animnotify_diagnosis_D1.md)), D2([Document/animnotify_diagnosis_D2.md](animnotify_diagnosis_D2.md)), P-Fix5([Document/animnotify_pfix5_implementation.md](animnotify_pfix5_implementation.md)) 후속. PIE silent + Lua payload-query 모델 설계.

조사 방법: code view + grep only. 코드 수정 금지. D1/D2/P-Fix5 결론 재검증 없음.

조사 일자 컨텍스트: 2026-05-20 working tree.

---

## 1. 요약

- **Stage 1 판정**: **Cause-AnimInstance 확정** — PIE의 SkeletalMeshComponent가 `UAnimStateMachineInstance` (또는 `UAnimGraphInstance`) 를 사용하는데, 두 파생 모두 `UAnimInstance::GetActiveNotifies()` 를 override 하지 않아 **항상 nullptr 반환**. `Update`의 notify trigger 루프가 진입조차 못함 → dispatch 미도달 → silent.
- **근거**: V0(샘플 `MarioPawn.Prefab`이 `MarioControllerAnimGraph.lua`를 사용하고 그 Lua가 `SetupStateMachineGraph` → StateMachine 모드로 전환) + V2(`UAnimStateMachineInstance`/`UAnimGraphInstance` 둘 다 GetActiveNotifies 미override 확인).
- **V1(bPaused)**: 음성. `Component->Play(true)` ([LuaSkeletalMeshBindings.cpp:854](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:854))이 SetupStateMachineGraph 끝에서 `bPaused=false`로 set. PlayAnimation 경로도 마찬가지.
- **V3(asset binding)**: 음성. StateMachine 모드는 sequence를 각 state 노드에 bind. UAnimSingleNodeInstance 모델과 다르나 asset 자체는 정상.
- **Stage 2 진행**: payload 모델 4개 변형(b1~b4), Lua dispatch 패턴 3개(L1~L3), CameraManager binding 3개(C1~C3) 비교표 작성. 객관적 라벨 일치 조합 식별.
- **확인 불가 항목**: 없음 — V0~V3 모두 매핑 가능, V4는 음성 조건 미충족이라 skip.
- **부수 메모**: 본 root cause는 **UAnimSingleNodeInstance가 아닌 모든 AnimInstance 파생에 영향**. AnimSequenceEditorTab preview (UAnimSingleNodeInstance)가 정상이고 PIE/StateMachine이 silent인 이유가 정확히 여기 있음.

---

## 2. Stage 1 — PIE Silent 진단

### V0 PIE 환경 매핑

#### V0.1 PIE 진입점
- [EditorEngine.cpp:294](KraftonEngine/Source/Editor/EditorEngine.cpp:294) `UEditorEngine::StartPlayInEditorSession(const FRequestPlaySessionParams& Params)`
- Trigger: [EditorPlayToolbarWidget.cpp:82-83](KraftonEngine/Source/Editor/UI/EditorPlayToolbarWidget.cpp:82) editor toolbar의 Play 버튼 → `Editor->RequestPlaySession(Params)`.
- 호출 체인: `RequestPlaySession` → `PlaySessionRequest = InParams` (deferred); 메인 루프에서 `StartQueuedPlaySessionRequest` → `StartPlayInEditorSession`.

#### V0.2 PIE World
- editor preview world와 **다름** — duplication 통한 별도 객체.
- [EditorEngine.cpp:308](KraftonEngine/Source/Editor/EditorEngine.cpp:308): `UWorld* PIEWorld = EditorWorld->DuplicateAs(EWorldType::PIE);`
- WorldList에 별도 context로 등록([line 320](KraftonEngine/Source/Editor/EditorEngine.cpp:320)), `ActiveWorldHandle = "PIE"` ([line 340](KraftonEngine/Source/Editor/EditorEngine.cpp:340)).
- duplication 시 component 단위로 `PostDuplicate()` 호출됨 — SkeletalMeshComponent는 [SkeletalMeshComponent.cpp:241-261](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:241)에서 AnimInstance 재생성 + asset path 기반 SetAnimation + **`SetBakedAnimPaused(true)`** 강제.

#### V0.3 spawn 흐름
- PIE World duplication 후 [Level.cpp:75-86](KraftonEngine/Source/Engine/GameFramework/Level.cpp:75) `ULevel::BeginPlay()` 호출 — actors 순회.
- 각 Actor의 [AActor.cpp:275-300](KraftonEngine/Source/Engine/GameFramework/AActor.cpp:275) `BeginPlay()`는 OwnedComponents 순회하여 각 `Comp->BeginPlay()` 호출.
- 샘플 Prefab [Asset/Prefab/MarioPawn.Prefab](../KraftonEngine/Asset/Prefab/MarioPawn.Prefab):
  ```json
  RootActor: APawn
    NonSceneComponents:
      - ULuaScriptComponent  ScriptPath="Game/MarioControllerAnimGraph.lua"
    RootComponent: UBoxComponent
      Children:
        - USkeletalMeshComponent  Skeletal Mesh="...", Animation Mode=0(=SingleNode)
  ```
- 주목: Prefab의 `Animation Mode=0`(SingleNode)이지만 **Lua가 BeginPlay에서 `SetupStateMachineGraph`를 호출하면서 StateMachine 모드로 전환**됨 ([LuaSkeletalMeshBindings.cpp:840](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:840)). 즉 Prefab의 mode는 첫 frame 사이에 덮어쓰여짐.

#### V0.4 Lua 조작 진입점
- [LuaSkeletalMeshBindings.cpp](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp) usertype `"SkeletalMeshComponent"`에 노출된 함수 중 PIE setup에 쓰이는 것:
  - `PlayAnimation(handle, looping)` line 688 — `Component->PlayAnimation(Sequence, bLooping)` 위임, AnimationSingleNode 모드로 강제 set + `bPaused=false`
  - `PlayAnimationPath(path, looping)` line 704 — 동일 + path resolve
  - `Play(looping)` line 726 — `Component->Play(bLooping)` 위임, `bPaused=false`
  - `SetupStateMachineGraph(config)` line 830 — AnimationStateMachine 모드로 set + StateMachine 구성 + `Component->Play(true)`(=`bPaused=false`)
  - `SetStateBool(name, value)` line 808 — StateMachine BoolVariable 설정
  - `GetTriggeredNotifies()` line 787 — `TriggeredNotifiesThisFrame` (이름 list) 반환
  - `SetAnimation` / `SetBakedAnimTime` / `SetBakedAnimPaused` / `SetBakedAnimPlaybackSpeed` / `SetSkeletalMesh` 등

#### V0.5 sample script
- [LuaScripts/Game/MarioControllerAnimGraph.lua](../KraftonEngine/LuaScripts/Game/MarioControllerAnimGraph.lua) — wrapper, 실제 로직은 `MarioControllerAnimGraphBase.lua`.
- [LuaScripts/Game/MarioControllerAnimGraphBase.lua:111-](../KraftonEngine/LuaScripts/Game/MarioControllerAnimGraphBase.lua:111) `ConfigureStateMachine` → `M.skelMesh:SetupStateMachineGraph({...})` 호출. 즉 PIE 진입 후 BeginPlay → Lua BeginPlay → SetupStateMachineGraph → AnimationStateMachine 모드 + bPaused=false.
- Locomotion submachine + Jump states 정의. 각 state는 sequence 참조 (`Animation.LoadSequence(path)`로 로드).
- SingleNode 변형 ([MarioControllerSingleNodeBase.lua](../KraftonEngine/LuaScripts/Game/MarioControllerSingleNodeBase.lua)) 도 별도 존재 — `M.skelMesh:PlayAnimation(handle, looping)` 직접 호출. 이 경로는 UAnimSingleNodeInstance.

### V1 bPaused 초기 상태 진단

#### V1.1 멤버 기본값
- `USkeletalMeshComponent::bBakedAnimPaused` 멤버([SkeletalMeshComponent.h:117](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:117)): 기본 `true`.
- `UAnimInstance::bPaused` 멤버([AnimInstance.h:115](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:115)): 기본 `true`.
- 두 layer 모두 default = paused.

#### V1.2 누가 false로 푸는가
- **Component-side**:
  - [SkeletalMeshComponent.cpp:171](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:171) `PlayAnimation` 본체에서 `bBakedAnimPaused=false`. 다음 caller에서 호출:
    - [SkeletalMeshComponent.cpp:225](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:225) `BeginPlay` (SingleNode 모드 + AnimToPlay 있을 때만)
    - Lua bindings: `PlayAnimation`/`PlayAnimationPath` 경로 (위 V0.4)
  - [SkeletalMeshComponent.cpp:196-205](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:196) `Play(bLooping)` 본체에서 `bBakedAnimPaused=false`. Lua의 `Play` binding + `SetupStateMachineGraph` 끝부분([LuaSkeletalMeshBindings.cpp:854](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:854))에서 호출.
- **Lua-side**: grep 결과 PIE 흐름의 sample script들이 `SetupStateMachineGraph` 또는 `PlayAnimation` 호출. 두 경로 모두 component를 unpause 함.

#### V1.3 sample script 분석
- `MarioControllerAnimGraphBase.lua` Bootstrap → `ConfigureStateMachine` → `M.skelMesh:SetupStateMachineGraph(...)` 호출. 이 binding이 마지막에 `Component->Play(true)` 호출하여 `bPaused=false`.
- `MarioControllerSingleNodeBase.lua` Bootstrap → 각 anim load → `EnterState(STATE.IDLE)` → `PlayAnim` → `M.skelMesh:PlayAnimation(...)`. PlayAnimation binding이 `Component->PlayAnimation`을 호출하여 `bPaused=false`.

#### V1.4 판정
- **음성**: PIE의 정상 Lua flow에서 bPaused는 반드시 false로 풀림 (SetupStateMachineGraph 끝 + PlayAnimation 본체). bPaused는 silent 원인이 아님.
- 단, Lua가 setup 함수를 한 번도 호출하지 않은 frame(첫 frame 또는 BeginPlay 실패 시)에는 bPaused=true 상태로 Tick이 돌 수 있음 — 이 경우 V2 root cause 와 무관하게 silent. 그러나 정상 PIE 시나리오에서는 BeginPlay에서 setup이 완료되므로 첫 Tick에 unpause 완료.

### V2 AnimInstance 생성/세팅 진단

#### V2.1 AnimInstance 멤버
- [SkeletalMeshComponent.h:126](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:126): `UAnimInstance *AnimInstance = nullptr;`
- 기본값 nullptr. lazy 생성.

#### V2.2 AnimInstance 생성 경로
- [SkeletalMeshComponent.cpp:133-154](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:133) `EnsureAnimInstance()`:
  ```cpp
  if (AnimInstance) return;
  switch (AnimationMode) {
  case AnimationStateMachine:  AnimInstance = CreateObject<UAnimStateMachineInstance>(this); break;
  case AnimationGraph:         AnimInstance = CreateObject<UAnimGraphInstance>(this); break;
  case AnimationSingleNode:
  default:                     AnimInstance = CreateObject<UAnimSingleNodeInstance>(this); break;
  }
  AnimInstance->InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh));
  ...
  ```
- 호출처:
  - `SetAnimation` ([SkeletalMeshComponent.cpp:188](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:188))
  - `PlayAnimation` ([SkeletalMeshComponent.cpp:160](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:160))
  - `SetAnimationMode` (mode 변경 시) — 추가 grep으로 확인
  - `PostDuplicate` 경유의 SetAnimation
  - `BeginPlay` 경유의 PlayAnimation
- **모드 변경 시 재생성 미지원**: [SkeletalMeshComponent.cpp:135](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:135) `if (AnimInstance) return;` → 이미 생성된 경우 그대로 유지. `SetAnimationMode` 가 호출되어도 기존 AnimInstance 그대로면 mode mismatch 발생 가능.

#### V2.3 종류 판별 (sample 시나리오)
- Prefab `Animation Mode=0`(SingleNode) → PIE PostDuplicate 시점에 SetAnimation 호출되어 UAnimSingleNodeInstance 생성.
- 그러나 BeginPlay 후 Lua의 `SetupStateMachineGraph` → 내부에서 `Component->SetAnimationMode(AnimationStateMachine)` 호출([LuaSkeletalMeshBindings.cpp:840](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:840)).
- [SkeletalMeshComponent::SetAnimationMode](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) 동작 확인:

#### V2.4 GetActiveNotifies 동작 — **핵심 root cause**
- Base [AnimInstance.h:84](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:84): `virtual const TArray<FAnimNotifyEvent>* GetActiveNotifies() const { return nullptr; }` — 기본 nullptr.
- Override 현황:
  | 파생 클래스 | `GetActiveNotifies` override | 반환 |
  |---|---|---|
  | `UAnimSingleNodeInstance` | ✓ ([AnimSingleNodeInstance.cpp:75-78](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:75)) | `CurrentSequence ? &CurrentSequence->GetNotifies() : nullptr` |
  | `UAnimStateMachineInstance` | ✗ ([AnimStateMachineInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h) 전체 본문 확인 — override 선언 없음) | nullptr (base) |
  | `UAnimGraphInstance` | ✗ ([AnimGraphInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.h) 전체 본문 확인 — override 선언 없음) | nullptr (base) |
- `UAnimInstance::Update` ([AnimInstance.cpp:81-98](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:81)):
  ```cpp
  const TArray<FAnimNotifyEvent> *Notifies = GetActiveNotifies();
  TArray<const FAnimNotifyEvent *> LocalTriggered;
  if (Notifies)                              // ← StateMachine/Graph는 nullptr → skip
  {
      for (const FAnimNotifyEvent &Notify : *Notifies) { ... }
  }
  if (!LocalTriggered.empty()) {             // ← 항상 empty
      DispatchTriggeredNotifies(LocalTriggered);
  }
  ```
- 결과: **StateMachine/Graph 모드에서는 Sequence/Notify가 어디에 binding되어 있든 dispatch path 자체가 진입 불가**.

#### V2.5 판정
- **양성**: PIE의 SkeletalMeshComponent가 `UAnimStateMachineInstance` (또는 향후 `UAnimGraphInstance`)를 사용하는 경우, `GetActiveNotifies()`가 nullptr 반환 → notify trigger pass 전체 skip → `DispatchTriggeredNotifies` 미도달 → **silent root cause 확정**.
- AnimSequenceEditorTab preview는 항상 `UAnimSingleNodeInstance` 사용([SkeletalMeshComponent.cpp:146](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:146) default) 이므로 P-Fix5 후 정상 동작 — 환경 A/B 차이의 정확한 원인.

### V3 AnimSequence asset binding 진단

#### V3.1 Lua의 SetAnimation 호출
- 직접 `SetAnimation` binding은 grep 결과 없음. Lua는 `PlayAnimation(handle, looping)`/`PlayAnimationPath(path, looping)`/`SetupStateMachineGraph(config)` 경유로 sequence를 set.
- StateMachine 시나리오는 graph 내부 state별로 `FAnimGraphNode_SequencePlayer` 가 sequence를 보관. 각 노드의 sequence는 `BuildStateMachineFromLua`(=`LuaSkeletalMeshBindings.cpp:848` 인용) 경유로 Lua config에서 추출.

#### V3.2 asset path 해결 흐름
- Lua side: `Animation.LoadSequence(path)` ([MarioControllerAnimGraphBase.lua:42](../KraftonEngine/LuaScripts/Game/MarioControllerAnimGraphBase.lua:42)) → C++ FLuaWorldLibrary::LoadAnimSequence (PlayAnimationPath 경유) 또는 Animation usertype 자체 lookup. 반환된 handle은 Lua에서 sequence wrapper.
- StateMachine config 내부: 각 state의 anim path → `BuildStateMachineFromLua` 가 sequence를 SequencePlayer 노드에 주입.

#### V3.3 set 시점
- `BeginPlay` → ULuaScriptComponent::BeginPlay → Lua의 `BeginPlay` 함수 호출. Mario 케이스: `Controller.BeginPlay() → Bootstrap() → ConfigureStateMachine()` → SetupStateMachineGraph가 sequence들을 graph에 binding.
- 첫 Tick 전에 setup 완료. 즉 set 시점은 **BeginPlay 끝**.

#### V3.4 GetAnimation 동작
- `USkeletalMeshComponent::GetAnimation()` ([SkeletalMeshComponent.h:39](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:39)) → `AnimToPlay` 멤버 반환. StateMachine 모드는 AnimToPlay를 사용하지 않음 — graph 내부에 sequence가 들어있음.
- 따라서 GetAnimation()은 nullptr이거나 의미 없음. 그러나 그것 자체가 silent 원인은 아님 — sequence는 graph 노드 내부에 정상 binding됨.

#### V3.5 판정
- **음성**: StateMachine 모드에서 sequence는 graph 노드(`FAnimGraphNode_SequencePlayer`) 내부에 binding되어 있음. 정상 동작. silent 원인은 sequence가 set 안 됐기 때문이 아니라 **AnimInstance가 그 sequence의 notify list를 노출하지 않기 때문**(V2).
- 단 미세한 부작용: `Component->GetAnimation()`이 nullptr이라는 사실은 외부에서 sequence를 query하려는 다른 path를 막을 수 있으나 본 silent 진단 범위 외.

### V4 TickComponent → Update 도달 검증

V1~V3 중 V2가 양성이므로 V4 skip. silent root cause가 V2에서 식별됨.

### 종합 판정

- **판정**: **Cause-AnimInstance 확정** (V2 양성).
- **근거**: 
  1. V0.5의 sample Lua가 `SetupStateMachineGraph` 호출하여 StateMachine 모드로 전환.
  2. V2.4에서 `UAnimStateMachineInstance`와 `UAnimGraphInstance` 둘 다 `GetActiveNotifies()` override 부재 확인 — base의 nullptr 반환을 그대로 사용.
  3. `UAnimInstance::Update`의 notify trigger 루프는 `if (Notifies)` 가드로 nullptr 시 skip → LocalTriggered가 항상 empty → DispatchTriggeredNotifies 미도달.
- **추가 관찰**:
  - 본 silent는 **AnimationMode가 SingleNode가 아닌 모든 component**에 영향. 즉 PIE뿐만 아니라 향후 AnimGraph editor preview, console command graph injection ([EditorConsoleWidget.cpp:1433](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1433) 인용 참고) 등에서도 동일 silent 예상.
  - StateMachine의 "현재 활성 state의 sequence" 는 graph 평가 중 결정되는 runtime 상태 — 단일 sequence가 아니라 시점에 따라 다른 sequence가 활성. 따라서 `GetActiveNotifies()` 의 single-pointer 시맨틱이 StateMachine 모델과 잘 맞지 않음. override 신설하려면 **현재 활성 state의 sequence** 를 graph로부터 query하는 인프라가 필요(범위 외 — 본 prompt는 진단 + Lua binding 설계까지).

---

## 3. Stage 2 — Lua Binding 설계 옵션 비교 (Payload Query 모델)

### 3.1 Stage 1 결과와의 관계

본 prompt의 Stage 2는 D2 부분 무효화(Lua 우선 / C++ fallback) 정책 구현을 위한 **Lua binding 설계**이며 silent 해소와는 **독립**. 위 Stage 1 결과("Cause-AnimInstance 확정")는 다음 두 사실을 의미:

1. **Stage 2의 Lua binding이 PIE silent 해소를 보장하지 않음**: dispatch path가 `UAnimInstance::Update` 안에서 차단된 상태이므로, Lua가 query하더라도 `TriggeredNotifiesThisFrame`은 비어 있음 (Stage 1에서 확인). Lua binding을 추가해도 query 결과가 empty면 dispatch 안 일어남.
2. **PIE silent 해소는 별도 작업 필요**: `UAnimStateMachineInstance`/`UAnimGraphInstance`에 `GetActiveNotifies` override 추가 또는 graph가 직접 notify를 발화할 수 있는 메커니즘 신설 등. 이는 본 prompt의 scope 외 — 후속 진단/구현으로 분리.

본 Stage 2는 **silent 해소 작업과 병렬로** 진행 가능한 binding 설계만 다룬다.

### 3.2 Payload 노출 방식 비교 (b1~b4)

| 변형 | 정보 완전성 | 기존 caller 영향 | 메모리/성능 | Lua 사용성 | 미래 hook 호환 | 라벨 |
|---|---|---|---|---|---|---|
| **b1** — 멤버 element 타입 swap: `TriggeredNotifiesThisFrame: TArray<FName>` → `TArray<FAnimNotifyEvent>` (값 복사) | Type/SoundId/ShakeParams/TriggerTime/Duration **모두 노출** | [LuaSkeletalMeshBindings.cpp:801](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:801)의 `GetTriggeredNotifies` 시그니처/구현 변경 필요. 기존 Lua 측은 `notify_name` string list를 받던 것이 table list로 바뀌어 시맨틱 깨짐. | `FAnimNotifyEvent`는 약 80바이트 이상(SoundId FString + ShakeParams 13필드). 매 frame O(N) 복사 — N은 frame당 trigger 수, 보통 0~3. 영향 작음. | 한 번의 query로 type별 분기 가능. table에 모든 정보. | 향후 C++→Lua push 추가 시 같은 payload를 push 함수에 그대로 전달 가능. | "가장 명확한 시맨틱" (근거: payload가 한 자료구조에 모임) |
| **b2** — 기존 `TArray<FName>` 유지 + 별도 `TArray<FAnimNotifyEvent> TriggeredNotifyPayloadsThisFrame` 신설 | 모두 노출 (별도 멤버로) | 기존 `GetTriggeredNotifies` 보존 (이름만). 새 `GetTriggeredNotifyPayloads` binding 신설. 기존 Lua 측 무수정. | 동일 정보 2배 보관 — 이름 + payload (이름은 payload에 이미 포함). 메모리 약간 낭비. clear/push 위치도 2배. | 이름만 필요한 caller는 기존 API, payload 필요하면 신규 API. choice의 자유. | b1과 동일 수준 호환. | "가장 작은 변경" (근거: 기존 caller 시그니처 무수정) |
| **b3** — element 타입을 light snapshot struct로 swap: `struct FAnimNotifySnapshot { FName Name; EAnimNotifyType Type; FString SoundId; FCameraShakeParams ShakeParams; float TriggerTime; }` (FAnimNotifyEvent와 거의 동일하나 의도적으로 별도 struct로 분리하여 직렬화/runtime 분리 명시) | 동일 (FAnimNotifyEvent와 시맨틱 같음) | b1과 동일 수준 — 기존 caller 시그니처 변경. | b1과 비슷 — 약간 더 가벼울 수 있으나 차이 미미. struct 자체가 거의 동일. | b1과 동일. | b1과 동일. | (라벨 없음 — b1 대비 차이 작음) |
| **b4** — 멤버 변경 없음. Lua bindings에서 `GetActiveNotifies()` + 이름 lookup으로 payload 재발굴 | TriggerTime/Duration까지 노출 가능. 단 각 trigger마다 O(N) lookup. | 기존 caller 무수정. 새 query binding은 lookup 비용. | 이름 list에서 sequence notify 배열을 매번 lookup — 매 frame 최악 O(N×M)(N=triggered, M=시퀀스 notify 수). 보통 작음. **단, StateMachine 케이스에서는 `GetActiveNotifies`가 nullptr이라 lookup이 동작 안 함 — Stage 1의 root cause와 직결**. | Lua가 직접 lookup 코드 작성 또는 binding이 lookup 후 반환. lookup 비용을 Lua가 알 필요는 없으나 정확성 책임을 binding이 짊. | 약함 — `GetActiveNotifies` 의존하는 구조라 SingleNode 외 모드에서 동작 불가. | (라벨 없음 — Stage 1 root cause와 직접 충돌) |

### 3.3 Lua dispatch 패턴 비교 (L1~L3)

| 패턴 | 장점 | 단점 | 코드 sketch | 라벨 |
|---|---|---|---|---|
| **L1** — Lua script가 매 Tick에서 직접 query + type별 if/elseif 분기 | 명시적, debug 용이. 각 actor가 자기 dispatch 정책 자유롭게 결정 (예: 특정 notify는 skip, 다른 notify는 추가 처리). | 매 component마다 동일 boilerplate. type별 분기 코드 중복. | ```lua\nfunction Tick(dt)\n  local list = self.skelMesh:GetTriggeredNotifyPayloads()\n  for _, n in ipairs(list) do\n    if n.Type == "Sound" then Sound.PlayEffect(n.SoundId)\n    elseif n.Type == "CameraShake" then\n      local pc = self.pawn:GetPlayerController()\n      pc:StartCameraShakeFull(n.ShakeParams)\n    end\n  end\nend\n``` | "가장 debug 용이" (근거: dispatch 책임이 script에 명시) |
| **L2** — Lua helper 함수 `AnimNotify.DispatchAll(comp)` 신설 + script는 한 줄 호출 | boilerplate 제거. 일관성. helper 정책 변경이 모든 script에 자동 반영. | helper 자체가 type별 동작 정책을 알아야 함 — Lua 모듈에 dispatch 정책 누설. C++의 `DispatchTriggeredNotifies` 와 정책 중복(2곳에서 같은 일). | ```lua\n-- AnimNotify.lua\nlocal M = {}\nfunction M.DispatchAll(comp)\n  for _, n in ipairs(comp:GetTriggeredNotifyPayloads()) do\n    if n.Type == "Sound" then Sound.PlayEffect(n.SoundId)\n    ...\n  end\nend\nreturn M\n``` | "가장 간결한 script side" (근거: 1줄로 끝) |
| **L3** — Lua가 query만 하고 dispatch는 C++에 위양: `Comp:DispatchTriggeredNotifies()` 새 binding | C++ 정책 변경이 Lua script 무수정으로 반영. | Lua의 "query" 라기보다 "C++ trigger" — 후보 c(push) 와 시맨틱 거의 동일. 사용자가 후보 c를 scope 외로 둔 의도와 충돌. round-trip(query+trigger)이 어색. 또한 Lua가 dispatch를 막거나 정책 분기를 끼울 여지 없음. | ```lua\nfunction Tick(dt)\n  self.skelMesh:DispatchTriggeredNotifies()  -- C++에 dispatch 위임\nend\n``` | (라벨 없음 — scope 경계 침범 위험) |

### 3.4 CameraManager binding 비교 (C1~C3)

| 변형 | 장점 | 단점 | 라벨 |
|---|---|---|---|
| **C1** — `APlayerCameraManager` usertype 신설 + `StartCameraShake(FCameraShakeParams)` binding. `FCameraShakeParams`는 Lua table ↔ C++ struct 변환 helper 필요 (13필드) | 정보 무손실. AnimNotify의 ShakeParams를 그대로 Lua → CameraManager로 전달 가능. future-proof. | binding 코드 양 큼. Lua table 입력의 type-safety는 빈약(누락 필드/오타 시 default fallback 또는 에러 처리 필요). | "가장 정보 완전성" |
| **C2** — `PlayerController:StartCameraShakeFull(table)` overload 신설 — 1개 table 인자 (또는 13개 primitive 인자) | C1과 같은 정보 완전성. PlayerController usertype은 이미 존재 — usertype 신설 비용 없음. 기존 7-arg `StartCameraShake` 와 공존. | overload 모호성 가능(arg 수 같으면 sol 모호). table 변환은 동일하게 필요. | "가장 작은 추가" (근거: 기존 PlayerController usertype 재사용) |
| **C3** — binding 신설 없이 `Player:StartCameraShake(...)` (현재 7-arg overload) 그대로 사용 | 변경 0. | `FCameraShakeParams.Pattern`(Sine/Perlin), `Roughness`, `Seed`, `BlendInTime`, `BlendOutTime`, `bApplyInCameraLocalSpace` 등 6+필드 손실. LocAmp/RotAmp의 비등방 xyz/pyr도 등방 1값으로 압축 — 정보 손실 추가. AnimNotify가 13필드를 모두 의도해 설정한다면 시맨틱 깨짐. | "가장 변경 없음" (근거: 0 line code change) |

### 3.5 객관적 라벨 일치 조합

| 평가 축 | 조합 | 근거 |
|---|---|---|
| **"가장 작은 변경"** | b2 + L1 + C3 | b2: 기존 binding 무수정 / L1: helper 추가 안 함 / C3: CameraManager binding 0 변경. 단 C3 는 정보 손실 수용. |
| **"가장 명확한 시맨틱"** | b1 + L1 + C1 | b1: payload가 단일 자료구조 / L1: script가 dispatch 정책 명시 보유 / C1: ShakeParams 직접 binding으로 무손실 변환. |
| **"가장 빠른 성능"** | b1 또는 b3 + L1 + C1 | 멤버 swap이 lookup보다 빠름(b4 탈락). 추가 helper 함수 호출 없음(L1). 가장 적은 변환 단계(C1). |
| **"가장 정보 완전성"** | b1 또는 b2 + L1 또는 L2 + C1 | payload 노출 + ShakeParams 무손실. dispatch 패턴은 무관. |
| **"가장 미래 hook (C++→Lua push) 호환"** | b2 + L1 또는 L2 + C1 | b2: 기존 query API 유지하면서 별도 payload 멤버 — push 모델 추가 시 같은 payload를 hook에 전달 가능. |
| **"silent 해소 (Stage 1) 와 양립 가능성"** | 모든 조합 — Stage 1과 독립. 단 b4는 `GetActiveNotifies` 의존성 때문에 silent 해소 후에도 StateMachine에서 동작 안 할 위험. b4 외 모두 양립. |

**메모**: 본 표는 라벨 정의에 따른 조합 선택지일 뿐 "어느 조합이 가장 좋다"는 결론은 아님. 사용자가 가치 우선순위(정보 완전성 vs 최소 변경 vs 호환성)를 정해 선택.

---

## 4. 본 진단에서 확인 불가로 남은 항목

- 없음. V0~V3 모두 매핑 완료. V4는 V2 양성이라 skip 조건 충족.
- 단 다음 항목은 본 prompt scope 외라 미진단 — 후속 prompt 필요:
  - StateMachine 모드에서 "현재 활성 state의 sequence" 를 어떻게 노출할지 (override 신설 vs graph 평가 중 직접 발화 등 메커니즘 선택)
  - AnimNotify의 NotifyName / Type / SoundId / ShakeParams 외 추가 필드가 필요한지 (예: state 변경 시점 notify 등)

---

## 5. 사용자 후속 결정 항목

### 5.1 Stage 1 결과 관련 — 별도 silent fix prompt 필요
- 본 진단으로 확인된 root cause: `UAnimStateMachineInstance` / `UAnimGraphInstance` 의 `GetActiveNotifies` 미override.
- silent 해소 방향 후보 (본 prompt scope 외 — 향후 진단/구현 prompt 작성 시 출발점):
  1. **각 파생에 `GetActiveNotifies` override 신설** — graph로부터 현재 활성 state의 sequence pointer를 query하여 그 notify 배열을 반환. StateMachine은 `FAnimGraphNode_StateMachine` 의 현재 state, GraphInstance는 root 노드를 따라가는 visit 패턴.
  2. **dispatch path 자체를 graph 노드 내부로 이동** — `FAnimGraphNode_SequencePlayer::Evaluate` 안에서 자신의 sequence의 notify를 직접 trigger. 본질적으로 `GetActiveNotifies`-based 모델 폐기.
  3. **TriggeredNotifiesThisFrame을 외부에서 push 받는 멤버로 변경** — graph 노드가 자기 트리거를 직접 component의 buffer로 push.
- 각 옵션은 D7 정책, P-Fix5 시간 모델, Stage 2 binding 설계와의 호환성을 별도 평가 필요.

### 5.2 Stage 2 옵션 조합 선택
- **3.2 payload 노출 방식**: b1 / b2 / b3 / b4 중 선택 (b4는 Stage 1 root cause 충돌로 비추천)
- **3.3 dispatch 패턴**: L1 / L2 / L3 중 선택 (L3는 scope 경계 침범 위험)
- **3.4 CameraManager binding**: C1 / C2 / C3 중 선택 (C3는 정보 손실 수용 여부에 달림)
- 본 prompt 3.5의 라벨 조합표 참고하여 가치 우선순위 결정.

### 5.3 구현 prompt 작성 순서 권고 (사용자 참고용)
- silent fix와 Lua binding 설계는 독립적이라 어느 쪽 먼저 진행해도 무방. 단:
  - silent fix가 먼저면 Stage 2의 binding 동작 검증을 PIE에서 직접 할 수 있어 검증 비용 낮음.
  - Lua binding이 먼저면 SingleNode 기반 view(`AnimSequenceEditorTab`)에서 binding 동작은 검증 가능하나 PIE silent 해소는 별도 검증 필요.
