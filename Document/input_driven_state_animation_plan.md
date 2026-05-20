# Input-Driven State Machine Animation — Infrastructure Scan & Plan

본 문서는 "임의 키 입력 → 상태 전이 → 애니메이션 전환(블렌딩 포함)"을 구현하기 전에 수행한 **scan + plan**의 결과물이다. 실제 구현 코드 추가는 별도 작업으로 분리한다.

문서 전체에 다음 규칙을 적용한다: 설명/추론은 한국어로, 식별자·파일/클래스/함수/변수명·코드 스니펫·경로는 영어 원문을 유지한다.

---

## 0. Scope & Constraints

- **End-to-end 대상 레이어**: input capture → state variable 노출 → StateMachine transition 평가 → animation blending.
- **본 작업은 planning only**. 모든 사실 주장은 직접 열어본 파일 경로 + 라인 번호로 뒷받침한다.
- 스캔 도중 input/state/transition/blend 4개 레이어를 벗어나는 변경 필요성은 8절(Out-of-Scope Findings)에 기록.

---

## 1. Scan Summary — 코드 사실(Facts)

### 1.1 StateMachine 노드 (`AnimGraph_StateMachine.h/.cpp`)

- `FAnimGraphNode_StateMachine`은 `FAnimGraphNode_Base`를 상속하는 단일 노드(`KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:61-81`).
- 데이터:
  - `States: TArray<FAnimState>` — 각 `FAnimState`는 `Name`, `Sub`(sub-graph; 보통 `FAnimGraphNode_SequencePlayer`), `bResetTimeOnEnter`, `bLooping`, `SubLengthHint`을 보유 (`:23-30`).
  - `Transitions: TArray<FAnimTransition>` — `FromStateIndex`, `ToStateIndex`, `BlendDuration` (기본 `0.2f`), `Conditions` AND 결합 (`:49-55`).
  - `InitialStateIndex`, 런타임 상태: `ActiveStateIndex`, `ActiveTransitionIndex`, `TransitionElapsed`, `StateLocalTimes`, scratch 버퍼들 (`:65-80`).
- `EAnimTransitionConditionKind` (`:32-38`): `TimeElapsed`, `BoolVariable`, `OnNotify`, `Custom`. 단, **현재 구현은 `TimeElapsed`와 `BoolVariable`만 평가하며, `OnNotify`/`Custom`은 `false`를 반환**(`AnimGraph_StateMachine.cpp:37-39`).
- `Evaluate`의 핵심 흐름(`AnimGraph_StateMachine.cpp:45-154`):
  1. 첫 평가 시 `InitialStateIndex`로 진입하고 `StateLocalTimes`를 0으로 초기화 (`:55-60`).
  2. `Ctx.DeltaTime`을 `ActiveStateIndex`(전이 중이면 `ToStateIndex`도)에 누적 + loop wrap(`ApplyLoopOrClamp`)(`:63-71`).
  3. `ActiveTransitionIndex >= 0` 이고 `TransitionElapsed >= BlendDuration`이면 전이 완료 — 활성 상태를 `ToStateIndex`로 교체하고 `bResetTimeOnEnter` 적용 (`:74-85`).
  4. 활성 전이가 없을 때만, `From==Active`인 transition들을 순회하며 `EvaluateConditions` 결과 첫 true에서 발화 (`:88-106`).
  5. 출력 단계:
     - 활성 전이 없음 → 단일 상태의 `Sub->Evaluate` 실행 (`:115-127`).
     - 활성 전이 있음 → `ScratchFrom`/`ScratchTo`에 각각 평가 후 `AnimPoseUtils::BlendTransform(...,Alpha)`로 본별 합성. `Alpha = clamp(TransitionElapsed / BlendDuration, 0..1)`이고, `BlendDuration <= 0`이면 `Alpha = 1.0f`로 즉시 To 포즈 채택 (`:128-153`).

### 1.2 AnimInstance / AnimGraph 통합

- `UAnimInstance`(`AnimInstance.h:22-101`)는 `AnimGraphPtr`(`unique_ptr<AnimGraph>`)와 `OutputLocalPose: TArray<FTransform>`를 보유. `EvaluateGraph`(`AnimInstance.cpp:87-108`)는 `FAnimEvalContext { Skeleton, TimeSeconds=CurrentTime, DeltaTime=LastDeltaTime, OwningInstance=this }`를 만들어 `AnimGraphPtr->Evaluate(Ctx, OutputLocalPose)` 호출.
- 변수 노출 훅: `virtual bool UAnimInstance::GetBoolVariable(const FName &Name, bool Default) const` (`AnimInstance.h:71`)이 베이스에 존재. 기본은 `Default` 반환. `FAnimGraphNode_StateMachine::EvaluateConditions`는 `Ctx.OwningInstance->GetBoolVariable(C.VarName, false)`로 조회(`AnimGraph_StateMachine.cpp:35`).
- `UAnimStateMachineInstance`(`AnimStateMachineInstance.h:18-39`)가 그 훅을 override:
  - `BoolVariables: unordered_map<FName, bool, FName::Hash>` 멤버 보유(`:38`).
  - `SetBoolVariable(Name, Value)`(`:33`), `GetBoolVariable(Name, Default)` override(`:34`, 구현 `:cpp:38-42`).
  - `SetStateMachineGraph(unique_ptr<FAnimGraphNode_StateMachine>)`(`:31`, 구현 `:cpp:11-36`): 각 `FAnimState::Sub`가 `SequencePlayer`면 `SubLengthHint`를 `Sequence->GetPlayLength()`로 자동 도출(`:22-32`).
- `UAnimGraphInstance`(`AnimGraphInstance.h:16-30`)는 `SetRootGraph(unique_ptr<FAnimGraphNode_Base>)`로 임의 노드를 root에 set(`AnimGraphInstance.cpp:10-17`). **이 경로는 `SubLengthHint` 자동 도출 부수효과가 없음** — StateMachine을 여기에 꽂으려면 호출자가 hint를 직접 채워야 한다.

### 1.3 SkeletalMeshComponent 주입과 Tick chain

- `EAnimationMode`(`SkeletalMeshComponent.h:18-23`): `AnimationSingleNode`, `AnimationStateMachine`, `AnimationGraph`. **enum 정수값은 serialize되므로 순서 변경 금지**(`:17 주석`).
- `EnsureAnimInstance`(`SkeletalMeshComponent.cpp:133-154`): 모드별로 `UAnimSingleNodeInstance` / `UAnimStateMachineInstance` / `UAnimGraphInstance`를 lazy 생성하고 `InitializeAnimation(skeleton)` + Looping/Speed/Paused/EvaluationTime 동기화.
- `SetRootGraph(unique_ptr<FAnimGraphNode_Base>)`(`:339-359`)는 **`AnimationMode == AnimationGraph`일 때만 동작**. AnimationStateMachine 모드에서 호출하면 `UE_LOG` 후 ignored. 즉 StateMachine 그래프를 컴포넌트에 주입하는 정식 경로는 두 가지:
  1. (권장) 모드를 `AnimationStateMachine`으로 set → `Cast<UAnimStateMachineInstance>(SkelComp->GetAnimInstance())->SetStateMachineGraph(...)`. SubLengthHint 자동 도출이 적용된다.
  2. 모드를 `AnimationGraph`로 set → `SkelComp->SetRootGraph(state_machine_node)`. 이 경로는 SubLengthHint 자동 도출이 없으므로 호출자가 채워야 한다.
- `TickComponent`(`SkeletalMeshComponent.cpp:390-410`):
  1. `AnimInstance->Update(DeltaTime)` — base `UAnimInstance::Update`(`AnimInstance.cpp:23-85`)는 진입 시 `LastDeltaTime = bPaused ? 0.0f : DeltaTime`로 저장(`:25`)한 뒤, `GetEffectivePlayLength() <= 0`이면 early return. 즉 **StateMachine 인스턴스는 `GetEffectivePlayLength`가 base 디폴트 0이므로 early return하지만 `LastDeltaTime`은 이미 set돼 있다**. 단, `bPaused`가 true면 `LastDeltaTime = 0`이 되어 StateMachine 평가에서 시간이 흐르지 않는다.
  2. `AnimInstance->EvaluateGraph()` — 위에서 set된 `LastDeltaTime`이 `Ctx.DeltaTime`으로 노드까지 전달.
  3. `ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose())` — `SkinnedMeshComponent.cpp:691-732` 안에서 `LocalBonePoseMatrices` 갱신 → `RebuildMeshSpaceBoneMatrices` → (GPU 스킨 우선 시도, 실패 시) `SkinVerticesToReferencePose` → `EnsureRuntimeResources` → `MarkWorldBoundsDirty`. **baseline 문서 1.B-2의 끊김은 해소됨**(아래 1.6 참조).

### 1.4 Input system

- `InputSystem`(singleton, `Engine/Input/InputSystem.h:60-176`): Win32 키보드/마우스/스크롤/포커스 폴링. `GetKey(VK)`, `GetKeyDown(VK)`(transition press), `GetKeyUp(VK)`를 노출.
- 한 프레임 뷰: `FInputFrame`(`Engine/Input/InputFrame.h:36-298`) — `IsDown(VK)`, `WasPressed(VK)`, `WasReleased(VK)`. **각 함수는 consumption(`IsKeyConsumed`)를 함께 검사**해, GUI 캡처/Lua/Viewport가 먼저 소비한 키는 후속 consumer에게 보이지 않는다.
- 디스패처: `FGameplayInputRouter::Route`(`Engine/Input/GameplayInputRouter.cpp:15-60`).
  - 순서: (a) GUI capture 적용, (b) `FLuaScriptSubsystem::Get().CallInput(World, DeltaTime)` — 모든 Lua 스크립트에 `OnInput` 디스패치, (c) `ViewportClient->Tick(DeltaTime, InputFrame)` — viewport-level 처리.
- Lua 바인딩(`Engine/Scripting/LuaInputLibrary.cpp:108-251`): `Input.GetKey/GetKeyDown/GetKeyUp("NAME")` — 문자열 키명(`A-Z`, `0-9`, `SPACE`, `ENTER`, `SHIFT`, `LSHIFT`, ..., `F1-F12`, `MOUSE1-5`)을 VK로 매핑(`:14-90`). consumption API도 노출(`Input.ConsumeKey`, `Input.ConsumeKeyboard`, ...).
- **StateMachine 변수 set용 Lua 바인딩이 이미 존재**(`Engine/Scripting/LuaSkeletalMeshBindings.cpp:478-508`):
  - `SkeletalMeshComponent:SetStateBool(name, value)` — `Cast<UAnimStateMachineInstance>(...)`에 성공한 경우만 `SetBoolVariable` 호출, 실패 시 `UE_LOG`와 false 반환.
  - `SkeletalMeshComponent:GetStateBool(name, default)` — 동일 캐스트, 실패 시 default 반환.
- Lua 스크립트에서 입력→스테이트 set은 **추가 C++ 코드 없이 즉시 가능**.

### 1.5 Animation blending 능력

- 기본 블렌딩 유틸: `AnimPoseUtils::BlendTransform(A, B, alpha)`(`AnimPoseUtils.h:34-41`) — `FVector::Lerp` 위치/스케일, `FQuat::Slerp` 회전. `BindPoseToTransform`, `FillBindPoseTransforms`도 동일 헤더.
- 노드:
  - `FAnimGraphNode_SequencePlayer::Evaluate`(`AnimGraph.cpp:44-127`) — 단일 시퀀스 TRS 키 보간(Option B). 동작 확인됨(baseline 문서 1.B-1 끊김은 해소된 것으로 보임 — 아래 1.6).
  - `FAnimGraphNode_Blend2::Evaluate`(`AnimGraph.cpp:129-156`) — 두 자식 + alpha.
  - `FAnimGraphNode_BlendN::Evaluate`(`AnimGraph.cpp:158-217`) — N개 자식 가중치 합성.
  - `FAnimGraphNode_StateMachine::Evaluate`(`AnimGraph_StateMachine.cpp:128-153`) — **per-transition crossfade를 이미 구현**. `BlendDuration > 0`이면 알파 진행, `BlendDuration <= 0`이면 즉시 To 채택(hard switch).
- 결론: **오늘 코드로 두 애니메이션 사이 crossfade와 hard switch 모두 가능**. 추가 노드 신설 불필요.

### 1.6 Baseline 교차 — `Document/animation_pipeline_validation_plan.md`

- baseline 1.B-2 "매 프레임 스킨 트리거 누락 `[끊김]`"은 **현재 해소됨**: `SkinVerticesToReferencePose()` + `EnsureRuntimeResources()`이 `USkinnedMeshComponent::ApplyEvaluatedPose`(`SkinnedMeshComponent.cpp:725-730`)로 옮겨졌고, `SkeletalMeshComponent::TickComponent`(`:406`)와 `RefreshAnimationPose`(`:424`) 모두 이 경로를 통과한다.
- baseline 1.B-1 "TRS 분리 키 미충전"은 사용자가 `SequencePlayer::Evaluate`가 동작한다고 명시 — importer 측이든 fallback이든 해결된 것으로 가정한다. **그러나 본 작업의 plan에서는 이를 verify 단계(7절)에 1차 항목으로 포함**해, 새 시퀀스가 실제로 움직이는지를 먼저 확인한다.
- baseline 1.B-3 "Notify 적재 미구현"은 transition condition `EAnimTransitionConditionKind::OnNotify` 사용을 막는다. **본 plan은 OnNotify를 사용하지 않는다**(`BoolVariable` + `TimeElapsed`만 사용).

---

## 2. End-to-End 경로 — 입력에서 포즈까지

본 plan이 다루는 데이터 흐름:

```
Win32 keyboard
  └─ InputSystem::Tick (CurrentStates[256])
       └─ FInputSystemSnapshot
            └─ FInputFrame (per-frame view, consumption)
                 └─ FGameplayInputRouter::Route
                      ├─ FLuaScriptSubsystem::CallInput  ← (A) 권장 진입점
                      │     └─ Lua: Input.GetKeyDown("X")
                      │          └─ Lua: comp:SetStateBool("Var", true)
                      │               └─ UAnimStateMachineInstance::SetBoolVariable
                      │                    └─ BoolVariables[FName("Var")] = true
                      └─ ViewportClient::Tick (대안 진입점)

  [Tick 시작]
  └─ USkeletalMeshComponent::TickComponent
       ├─ UAnimInstance::Update (LastDeltaTime 갱신)
       └─ UAnimInstance::EvaluateGraph
            └─ AnimGraph::Evaluate
                 └─ FAnimGraphNode_StateMachine::Evaluate
                      ├─ EvaluateConditions(OwningInstance, ...)
                      │     └─ OwningInstance->GetBoolVariable("Var", false) == bExpectedValue?
                      ├─ (조건 발화 시) ActiveTransitionIndex set
                      ├─ TransitionElapsed += Ctx.DeltaTime
                      └─ if 전이 중: From·To 두 sub-graph 평가 후 BlendTransform 합성
                                else: 단일 상태 sub-graph 평가
            └─ OutputLocalPose: TArray<FTransform>
       └─ ApplyEvaluatedPose → 스킨 → 화면
```

---

## 3. 결정 사항(스캐너 위임 항목)

각 항목에 대해 **코드 사실 → 옵션 → 추천 → 사용자 확인 필요 여부** 순으로 정리한다.

### 3.A Key Input → State Trigger 위치

**코드 사실**
- 키 폴링/디스패치 인프라는 완비(`InputSystem`, `FInputFrame`, `FGameplayInputRouter`).
- Lua 측에 `Input.GetKey*`와 `SkeletalMeshComponent:SetStateBool` 바인딩이 **둘 다 이미 존재**(추가 C++ 코드 0).
- C++ 측에 input → StateMachine을 잇는 기존 컴포넌트/시스템은 발견되지 않았다(`grep`으로 `UAnimStateMachineInstance` 참조는 `SkeletalMeshComponent`, `LuaSkeletalMeshBindings`, 본 클래스 정의 파일들만).
- Editor 측에는 `EditorConsoleWidget.cpp:1428-1444`이 `SequencePlayer` 단일 노드를 graph 모드로 주입하는 콘솔 명령 예시가 있을 뿐, StateMachine 조립/주입 예시는 없음.

**Viable Options**

1. **Lua 스크립트가 input 폴링 + `SetStateBool` 호출.**
   - 장점: 추가 코드 0. Lua만으로 입력 매핑 변경 가능(런타임 핫리로드). 게임 액터/씬에 자연스럽게 부착.
   - 단점: Lua 의존. 디버깅 시 두 언어 사이를 오감.
   - 적용 예: 액터에 부착된 Lua 스크립트의 `OnInput(deltaTime)` 안에서 `if Input.GetKeyDown("J") then comp:SetStateBool("Jump", true) end`.

2. **C++ 측에 새로운 `UAnimInputDriverComponent`(가칭) 추가.** ActorComponent로 input action ID ↔ FName 변수명 매핑 테이블을 보유하고, Tick에서 `InputSystem::Get().GetKey*`를 직접 폴링.
   - 장점: Lua 비의존. 에디터 프로퍼티로 매핑을 노출 가능.
   - 단점: 새 클래스/생성팩토리/시리얼라이즈/에디터 프로퍼티 패널 모두 신설 필요. scope 확장.

3. **Viewport/Viewer 레벨의 임시 키 핸들러**(예: 메뉴 단축키처럼) → `Cast<UAnimStateMachineInstance>` 후 직접 set.
   - 장점: Lua 미관여, 클래스 신설도 적음.
   - 단점: viewport가 게임 도메인 변수를 알게 되어 추상 누수. 단일 컴포넌트만 다루기 어렵고 여러 actor에 일반화하기 까다로움.

**추천**: **옵션 1 (Lua 진입점)**. 근거: 모든 binding이 이미 존재해 추가 C++ 비용 0이고, baseline에서 다른 Lua 게임 액터들(예: `LuaScripts/Game/Player.lua`)이 동일 패턴(`Input.GetKey` → 액션)을 이미 사용 중이다. C++ 진입점이 필요해지면 옵션 2를 별도 작업으로 도입.

**사용자 확인 필요**: 예 — Lua를 본 작업의 트리거 채널로 확정해도 되는지 사용자 승인 필요.

### 3.B Transition 메커니즘 (instant vs crossfade)

**코드 사실**
- `FAnimTransition::BlendDuration`이 per-transition 값(`AnimGraph_StateMachine.h:53`, 기본 `0.2f`).
- `FAnimGraphNode_StateMachine::Evaluate`이 이미 두 가지 모두 지원(`AnimGraph_StateMachine.cpp:145-152`):
  - `BlendDuration > 0` → `Alpha = clamp(TransitionElapsed / BlendDuration, 0, 1)`로 본별 `BlendTransform`.
  - `BlendDuration <= 0` → `Alpha = 1.0f`로 즉시 To 채택(hard switch). 그리고 다음 Evaluate에서 `TransitionElapsed >= 0`이 즉시 참이 되어 전이 완료.
- **전이 진행 중 신규 트리거 거부**: `Evaluate`가 `ActiveTransitionIndex < 0`일 때만 조건 검사(`:88`). 즉 0.2s 진행 중에 다른 키가 눌려도 무시된다 — UX 결정 사항.

**Viable Options**

1. **현재 코드 그대로 — per-transition `BlendDuration` 사용.** instant가 필요한 전이는 `0.0f`, 부드러운 전이는 `0.2~0.3f` 등 transition마다 선택.
2. **글로벌 default + per-transition override.** 사실상 현재가 그것(default `0.2f`이고 외부에서 set 가능). 별도 변경 없음.
3. **전이 중 신규 트리거 큐잉/취소** 정책 추가.
   - 옵션 3a: 큐잉 — 현재 전이 종료 후 즉시 다음 전이 실행. 코드 변경 필요(작은 큐).
   - 옵션 3b: 취소 — 현재 전이를 끊고 진행 중 alpha 상태에서 새 전이로 재합성. 코드 변경 큼(중첩 블렌딩 필요).
   - **본 plan 범위 밖**으로 분류 — 8절 Out-of-Scope에 기록.

**추천**: **옵션 1**. 사유: 이미 동작하는 메커니즘에 0-cost. UX는 "전이 중 입력 무시"가 디폴트가 되며, 데모 시 빠른 연타에서는 자연스럽게 끝난 뒤 반응한다. 이 동작이 부적절하다고 판단되면 별도 작업으로 옵션 3 검토.

**사용자 확인 필요**: 일부 — "전이 진행 중 입력 무시" 동작이 데모 요구사항에 부합하는지만 확인.

### 3.C State Variable 모델

**코드 사실**
- `UAnimStateMachineInstance::BoolVariables: unordered_map<FName, bool>` (`AnimStateMachineInstance.h:38`).
- `EAnimTransitionConditionKind`은 `TimeElapsed`, `BoolVariable`, `OnNotify`, `Custom`(`AnimGraph_StateMachine.h:32-38`)이지만 **실제 평가는 `TimeElapsed`와 `BoolVariable`만**(`AnimGraph_StateMachine.cpp:30-39`).
- enum 변수, float 변수, blackboard-style key/value(다형) 등은 **현재 없음**.

**Viable Options**

1. **bool flags only (현재 코드).** "Jumping=true", "Running=true", "Crouched=false" 같은 binary flag로 모든 키 입력을 표현.
2. **bool + enum 추가**(예: `EAnimStateMode` 값을 transition condition이 비교). enum 비교 condition kind 신설, `UAnimStateMachineInstance`에 `EnumVariables: unordered_map<FName, int32>` 추가, `EvaluateConditions` 분기 추가.
3. **Blackboard 모델**(키→variant, `std::variant<bool,int,float>` 등). 가장 표현력 높음, 변경량 큼.

**추천**: **옵션 1 (bool flags)**. 사유: 키 입력은 본질적으로 binary on/off이고, 현재 디자인이 가장 단순하다. 멀티 토글이 필요한 케이스(예: 4방향 이동)는 4개의 bool(`MoveN`, `MoveE`, `MoveS`, `MoveW`) 또는 같은 변수에 대한 진입 transition 분기로 표현 가능.

**사용자 확인 필요**: 일부 — 데모 시나리오에 enum이 꼭 필요한지(예: 단일 변수로 상태 다중 분기). 필요하면 별도 작업으로 옵션 2 도입.

---

## 4. Plan — 단계별 작업과 검증 기준

본 절은 **구현 단계의 명세만 적는다**. 실제 코드 추가는 별도 prompt로 분리한다.

### 4.1 단계 0: Verify Baseline (구현 전 필수 사전 확인)

- 목적: 1.6에서 가정한 baseline 끊김 해소 상태를 실제로 확인.
- 항목:
  1. `EditorConsoleWidget.cpp:1428-1444`의 SequencePlayer 단일 노드 주입 경로로 메시가 실제로 움직이는지 — 화면 시각 확인.
  2. `bPaused = false` 상태에서 Tick이 `LastDeltaTime > 0`을 유지하는지 — 임시 로그 추가 또는 디버거 확인.
- **통과 기준 (수정됨, Patch B C.4)**: 단일 시퀀스 재생 시각 확인 **AND** `bPaused = false` 상태에서 매 tick `LastDeltaTime > 0` 유지를 임시 로그로 확인. 두 항목 모두 통과해야 단계 0 완료로 본다.

### 4.2 단계 1: StateMachine 구성 헬퍼

- 목적: 호출자가 한 번에 (a) state machine 그래프 조립 → (b) SkeletalMeshComponent에 주입까지 할 수 있도록 helper API 마련.
- 코드 단위: C++ 헬퍼 함수 또는 builder struct (위치 후보: `Engine/Asset/Animation/Core/` 신규 파일 또는 기존 `AnimStateMachineInstance.cpp`의 helper). **본 plan은 위치/이름 확정 X — 구현 prompt에서 결정.**
- 의무 동작:
  1. 입력: `USkeleton *Skeleton`, sequence 핸들 또는 path 배열, 전이 규칙 (`{ from, to, BlendDuration, condition list }`).
  2. 출력: `unique_ptr<FAnimGraphNode_StateMachine>` (호출자가 `UAnimStateMachineInstance::SetStateMachineGraph`로 주입하거나, 컴포넌트 측 통합 API 호출).
  3. 각 상태의 `Sub`는 `FAnimGraphNode_SequencePlayer` 단일 노드를 default로 구성하고 `SetSequence(Skeleton, sequence)` 호출.
  4. `bLooping = true`, `bResetTimeOnEnter = true`가 합리적 default.
- 통과 기준: 헬퍼 호출 → 컴포넌트 주입까지가 단일 콘솔 명령/Lua 호출로 끝남.

### 4.3 단계 2: Lua 측 StateMachine 구성 바인딩 (선택)

- 목적: Lua 게임 액터에서 그래프 자체를 조립할 수 있게 하면 변경 비용이 더 낮아진다.
- 현재 사실: 변수 set은 가능(`SetStateBool`)하지만 **그래프 자체를 Lua로 조립하는 바인딩은 없다**.
- 옵션:
  - 옵션 A: Lua에 `SkeletalMeshComponent:SetupStateMachine({...})` 단일 entry 추가. 인자는 `states`, `transitions` 테이블.
  - 옵션 B: 우선 C++에서만 조립 가능하게 두고, Lua는 변수 set만 담당. 데모 범위가 작으면 충분.
- **본 plan 추천**: **옵션 B**(데모 단계). 그래프 자체가 자주 바뀌지 않는다면 C++ side에서 한 번 set하고 Lua는 키 → bool만 담당. 옵션 A는 후속 작업.
- **사용자 확인 필요**: 데모 시 그래프 자체를 Lua에서 바꾸고 싶은지.

### 4.4 단계 3: 입력 → 변수 연결

- Lua 진입(추천 시나리오):
  - 액터에 부착된 Lua 스크립트의 `OnInput(deltaTime)` 안에서 `Input.GetKeyDown/IsKey` 폴링 후 `comp:SetStateBool(name, value)` 호출.
  - 입력이 일회성 키(점프) → `GetKeyDown`(에지) + 일정 시간 후 자동 해제 또는 별도 키로 해제.
  - 입력이 지속 키(달리기) → `GetKey`/`WasReleased` 페어로 누르고 있을 때 true, 떼면 false.
- **본 plan 범위 안의 산출물**: 데모 액터용 Lua 스크립트(`LuaScripts/...`) 1~2개. 코드 변경은 없거나 매우 적다.

### 4.5 단계 4: Transition 정의 (값만 결정)

- 권장 default:
  - `BlendDuration`: 빠른 응답이 필요한 전이(공격 ↔ idle) `0.1f`, 사이클 전이(walk ↔ run) `0.2~0.3f`, 큰 포즈 차이(stand ↔ crouch) `0.4~0.5f`.
  - `Conditions`: `BoolVariable(VarName, bExpectedValue=true)` 단독이 기본. `TimeElapsed`로 최소 체류 시간(예: idle에서 0.05s 이내 빠른 토글 방지)을 추가하는 것도 옵션.
- **현재 코드 한계 reminder**: 전이 중 신규 트리거 무시(3.B 참조). 사용자가 `BlendDuration`을 짧게 잡을수록 이 한계가 덜 보인다.

### 4.6 단계 5: 데모 시나리오 — Mario64 모델 (Patch B C.1 교체)

- 목적: end-to-end가 작동함을 시각적으로 검증하기 위한 최소 시나리오.
- 시나리오: **`Idle ↔ Walk ↔ Run` 3상태 + `Jump` 1회성 상태** (Mario64 단순화 모델).
- 변수 모델: bool flags only — `bMoving`, `bWantRun`, `bJump` (3.C 결정).
- **Transition 표 (fixture에 그대로 박힐 값. 11.3 참조)**:

  | # | From | To | Conditions (AND) | BlendDuration | 등록 순서 메모 |
  |---|---|---|---|---|---|
  | 1 | Idle | Run | `bMoving == true`, `bWantRun == true` | 0.1s | Walk보다 먼저 (스캔 순서 우선순위) |
  | 2 | Idle | Walk | `bMoving == true`, `bWantRun == false` | 0.1s | Run 다음 |
  | 3 | Walk | Run | `bWantRun == true` | 0.1s | |
  | 4 | Run | Walk | `bWantRun == false` | 0.1s | |
  | 5 | Walk | Idle | `bMoving == false` | 0.15s | |
  | 6 | Run | Idle | `bMoving == false` | 0.15s | |
  | 7 | Idle | Jump | `bJump == true` | 0.05s | |
  | 8 | Walk | Jump | `bJump == true` | 0.05s | |
  | 9 | Run | Jump | `bJump == true` | 0.05s | |
  | 10 | Jump | Idle | `TimeElapsed >= 0.5s` | 0.15s | J1 단순화 — 시간 기반 자동 복귀 |

  총 10개 transition.

- State별 `bResetTimeOnEnter` default:
  - **Idle, Jump**: `true`.
  - **Walk, Run**: `true` (default). 시각 검증 후 phase mismatch가 보이면 `false`로 전환 옵션 — fixture 멤버로 노출하거나 `#ifdef`로 토글 가능하게 둔다.

- 키 매핑(Lua):
  - `W`(누르고 있을 때): `bMoving = true`, 떼면 `false`.
  - `Shift`(누르고 있을 때): `bWantRun = true`, 떼면 `false`. (단독으로는 동작 안 함 — `bMoving`과 AND 결합으로 Run.)
  - `Space`(에지 down): `bJump = true` → 0.05s 뒤(또는 다음 tick) Lua가 `bJump = false`로 자동 해제. Jump → Idle은 `TimeElapsed >= 0.5s`로 시간 기반 자동 복귀(J1, 결정 사항 참조).

- 필요한 시퀀스: idle, walk, run, jump 각각 1개. **이 시퀀스 자산이 baseline에서 import 가능한지는 단계 0에서 확인**.

- Mario64 모델 한계 reminder:
  - **J2(공중 상태) 미포함** — `IsAirborne` 변수, ground detection, Z velocity는 모두 후속 작업. Jump는 단순 시간 기반.
  - **방향 전환(yaw rotation) 미포함** — 데모는 제자리 이동 애니메이션만 검증.

### 4.7 단계 6: 검증 체크포인트

| ID | 체크포인트 | 확인 방법 | 통과 기준 |
|---|---|---|---|
| VP-1 | `bPaused = false` 일 때 매 tick `Ctx.DeltaTime > 0` | StateMachine 평가 진입에서 임시 로그 | 비-zero 값 |
| VP-2 | `SetBoolVariable` 결과가 `EvaluateConditions`에 즉시 반영 | 키 누른 다음 프레임 안에 transition fire | 한 프레임 내 전이 발화 |
| VP-3 | crossfade 진행 중 `BlendTransform`이 실제로 본을 섞는다 | 전이 중간 시점 캡처: scratchFrom != scratchTo, output이 둘의 보간 | 시각적으로 부드러운 보간 |
| VP-4 | hard switch(`BlendDuration = 0`) 시 즉시 To 포즈 | 키 입력 다음 프레임 캡처 | scratchFrom 무관, output == To |
| VP-5 | `bResetTimeOnEnter = true` 상태 진입 시 `StateLocalTimes[idx] == 0` | 디버거/로그 | 0.0 |
| VP-6 | 진행 중 전이가 다른 키 입력을 무시 | 전이 중 새 키 → 변화 없음 | 진행 중인 전이만 끝까지 진행 |
| VP-7 | `OutputLocalPose.size() == Skeleton bone count` | EvaluateGraph 직후 | 동일 |
| VP-8 | 화면상 메시가 키 입력에 반응 | 수동 조작 | 시각적 일치 |
| VP-9 | 전이 완료 후 `ActiveTransitionIndex == -1`로 reset되고 후속 transition fire 가능 | A→B 완료 직후 즉시 B→C 트리거 조건 set → 같은 또는 다음 tick에 새 transition 발화 | 두 번째 transition도 정상적으로 시작·완료 |
| VP-10 | 같은 from에서 여러 transition 후보가 있을 때 등록 순서대로 첫 true가 fire | `bMoving=true, bWantRun=true`로 set 후 첫 Evaluate 결과의 `ActiveTransitionIndex`가 표의 #1 (Idle→Run) | `Transitions[ActiveTransitionIndex].ToStateIndex` == Run의 인덱스. Walk가 fire되지 않음 |

---

## 5. Inputs / Outputs / Owners

| 단계 | 입력 | 출력 | 담당 레이어 |
|---|---|---|---|
| 0 baseline verify | 기존 메시/시퀀스 | 단일 시퀀스 재생 확인 | 사용자(시각 확인) |
| 1 헬퍼 작성 | skeleton + 시퀀스 + 룰 | `unique_ptr<FAnimGraphNode_StateMachine>` | C++(`Engine/Asset/Animation/Core/`) |
| 2 Lua 그래프 바인딩(선택) | Lua 테이블 | StateMachine 주입 | C++(`Engine/Scripting/`) |
| 3 입력→변수 | 키 입력 | `SetBoolVariable(name, value)` | Lua(`LuaScripts/...`) |
| 4 transition 값 | 데모 요구 | `FAnimTransition` 값들 | C++(헬퍼 호출자) 또는 Lua |
| 5 데모 시나리오 | 시퀀스 자산 4개 | 키로 조작 가능한 캐릭터 | Lua + 액터 setup |
| 6 검증 | 위 모든 단계 | VP-1~VP-8 통과 보고 | 사용자 + 임시 계측 |

---

## 6. Risks & Open Questions

1. **`bPaused` 초기값**. `UAnimInstance::bPaused`의 default가 `true`(`AnimInstance.h:100`)이고, `EnsureAnimInstance`가 `SetPaused(bBakedAnimPaused)`로 동기화하는데 `bBakedAnimPaused`도 default true(`SkeletalMeshComponent.h:117`). StateMachine 모드를 set한 직후 누군가가 명시적으로 `SetBakedAnimPaused(false)` 또는 `Play()`를 부르지 않으면 **`LastDeltaTime`이 0이 되어 StateMachine 시간이 진행하지 않는다**. 헬퍼/setup 단계에서 명시적 `Play` 또는 `SetPaused(false)`를 포함해야 한다.

2. **`EAnimTransitionConditionKind::OnNotify` 미구현**. 시퀀스 끝 notify로 전이를 끊는 패턴(예: jump 애니메이션의 land 키프레임 notify)은 **현재 불가**(`AnimGraph_StateMachine.cpp:37-39`이 false 반환). 본 plan은 `TimeElapsed`로 대체.

3. **전이 중 신규 트리거 거부**(3.B). 빠른 연타 시 입력이 묵살된다. 데모 시나리오에서 받아들일 만한 동작인지 사용자 확인 필요.

4. **시퀀스 길이 wrap**. `FAnimState::bLooping = true` + `SubLengthHint > 0`이어야 `ApplyLoopOrClamp`에서 wrap이 발생(`AnimGraph_StateMachine.cpp:11-18`). `UAnimStateMachineInstance::SetStateMachineGraph` 경로는 hint를 자동 도출하지만 `UAnimGraphInstance::SetRootGraph` 경로는 자동 도출이 없음(1.2 참조).

5. **Single-actor가 같은 키를 다른 의도로 쓰는 경우**. `Input.ConsumeKey`를 호출하는 다른 Lua 스크립트가 먼저 실행되면 본 액터 스크립트에서 키가 보이지 않을 수 있다. 데모에서는 다른 액터가 그 키를 소비하지 않게 setup.

6. **`UAnimInstance::Update`의 early return** — **Patch A.2로 검증 완료, 블로커 아님**. `LastDeltaTime` 할당(`AnimInstance.cpp:25`)이 `GetEffectivePlayLength() <= 0` early return(`:29-33`) **전**에 발생함을 직접 확인했다. 따라서 StateMachine 인스턴스(base `GetEffectivePlayLength()`가 0)도 `LastDeltaTime`이 정상 set되어 `Ctx.DeltaTime`이 노드까지 도달한다. 단, early return 이후 `PreviousTime`/`CurrentTime`이 0으로 reset된다는 점은 동일 — `CurrentTime` 기반 외부 로직(notify dispatch 등)을 기대하면 미스매치. **본 plan 범위 안의 risk: 낮음**(notify 미사용).

---

## 7. Out-of-Scope Findings

스캔 도중 input/state/transition/blend 4개 레이어 외부 변경이 필요해 보였지만, **본 작업에서는 진행하지 않고 사용자에게 보고만 한다.**

1. **`UAnimStateMachineInstance` 미구현 영역**: `GetEffectivePlayLength`이 base 기본값 0을 그대로 두는 점. State별 길이 합산/현재 활성 상태 길이 노출이 필요해질 수 있음(예: 외부에서 진행률 UI). 본 작업 범위 밖.

2. **`EAnimTransitionConditionKind::OnNotify` / `Custom` 구현**. baseline 문서 1.B-3의 notify 적재 미구현과 맞물려 작동하지 않음. 본 작업 범위 밖.

3. **전이 중 입력 큐잉/취소 정책**(3.B 옵션 3). UX 개선 시 별도 작업.

4. **Editor UI**: state machine을 GUI로 조립/편집하는 패널은 없음(`EditorConsoleWidget.cpp`의 단일 시퀀스 콘솔 명령만 존재). 본 작업 범위 밖.

5. **Lua 그래프 조립 바인딩**(4.3 옵션 A). 데모 단계에서는 옵션 B로 충분하다는 판단이지만, 후속 작업 후보.

6. **Notify dispatch 파이프라인**. baseline 1.B-3의 적재가 빈 상태이므로 notify 기반 트리거 로직은 모두 무력화된다. 본 작업 범위 밖.

7. **방향 전환 (yaw rotation, Patch B C.2)**. 데모는 제자리 이동 애니메이션만 검증. 캐릭터가 좌우로 돌거나 이동 방향이 키와 일치하도록 회전시키는 로직은 본 작업 범위 밖.

8. **J2 — 진짜 공중 상태 (Patch B C.2)**. ground detection, Z velocity, jump apex 판정, 착지 시 `IsAirborne` 변수 전이 등은 모두 후속 작업. 본 plan의 J1은 `TimeElapsed >= 0.5s` 시간 기반 자동 복귀만 다룬다.

---

## 8. Summary — 추천 한 줄

**현 코드베이스는 input layer / state variable / StateMachine transition / blending 4단계가 모두 갖춰져 있고, `SkeletalMeshComponent:SetStateBool` Lua binding을 통해 키→불변수 연결까지 0-cost로 가능**하다. 본 작업의 구현은 (a) C++ 측 StateMachine 조립/주입 헬퍼 1개, (b) 데모 Lua 스크립트 1~2개, (c) `bPaused = false` 보장과 4상태 데모 시나리오 setup만으로 완결된다.

---

## 9. (구 결정 질문 목록 — 12절 Resolved Decisions로 이동, Patch B C.5)

본 절은 빈 자리로 둔다. 결정 확정 내역은 **12. Resolved Decisions**를 참조.

---

## 10. Patch A — Fact Re-verification

본 절은 plan의 기존 주장 3개를 코드로 다시 확인한 결과를 기록한다(Patch A 수행 결과).

### 10.1 — `UAnimGraphInstance::SetRootGraph` 경로의 SubLengthHint 미도출 (plan 1.2 / 6.4 주장)

- **결과: 확인됨**.
- 근거:
  - `AnimGraphInstance.cpp:10-17` 전문:
    ```cpp
    void UAnimGraphInstance::SetRootGraph(std::unique_ptr<FAnimGraphNode_Base> InRoot)
    {
        if (!AnimGraphPtr)
        {
            AnimGraphPtr = std::make_unique<AnimGraph>();
        }
        AnimGraphPtr->SetRoot(std::move(InRoot));
    }
    ```
    `States` 순회나 `SubLengthHint` 기록 코드가 전혀 없다.
  - `AnimStateMachineInstance.cpp:22-32`만이 `States`를 순회해 `dynamic_cast<FAnimGraphNode_SequencePlayer *>(S.Sub.get())` 후 `S.SubLengthHint = SeqPlayer->Sequence->GetPlayLength()`을 수행한다.
- 영향: plan 1.2와 6.4의 주장 그대로 유효. `UAnimGraphInstance::SetRootGraph` 경로로 StateMachine을 주입하려면 호출자가 hint를 직접 채워야 한다.

### 10.2 — `UAnimInstance::Update`의 `LastDeltaTime` 할당 시점 (plan Risk #6 검증)

- **결과: 전(before)**. `LastDeltaTime` 할당이 `GetEffectivePlayLength() <= 0` early return **전**에 발생한다.
- 근거: `AnimInstance.cpp:23-33` 전문:
  ```cpp
  void UAnimInstance::Update(float DeltaTime)
  {
      LastDeltaTime = bPaused ? 0.0f : DeltaTime;   // line 25 — 할당
      TriggeredNotifiesThisFrame.clear();

      const float Length = GetEffectivePlayLength();
      if (Length <= 0.0f)                           // line 29 — early return 게이트
      {
          PreviousTime = CurrentTime = 0.0f;
          return;
      }
  ```
  라인 25(할당)가 라인 29(게이트)보다 위에 있다.
- 영향: **Risk #6는 블로커가 아님**. StateMachine 인스턴스(base `GetEffectivePlayLength() == 0`)도 `LastDeltaTime`이 정상 set되어 `Ctx.DeltaTime`이 노드까지 전달된다. plan Risk #6 본문은 이 사실을 반영하도록 갱신됨.

### 10.3 — Lua 액터의 input 사용 패턴 (plan 3.A 추천 근거 검증)

- **결과: 확인됨**. plan 3.A의 근거 "다른 Lua 게임 액터들이 동일 패턴을 이미 사용 중"이 사실로 확인된다. 폴링 패턴은 3개 액터에 걸쳐 존재한다.

| 파일:라인 | 패턴 |
|---|---|
| `LuaScripts/Game/Player.lua:347-371` | `Input.GetKey`/`Input.GetKeyDown`를 `pcall` + nil-check 방어로 wrap한 로컬 함수 정의 |
| `LuaScripts/Game/Player.lua:430-466` | `Tick(dt)`마다 `W/A/S/D`, `SHIFT`/`LSHIFT`, `MOUSE1`/`MOUSE2`를 폴링해 state 테이블 구성 + edge 판정 |
| `LuaScripts/Game/GameState.lua:890-893` | `Input.GetKeyDown("R") or Input.GetKeyDown("ENTER")`로 GameOver 시 재시작 트리거 |
| `LuaScripts/Game/IntroCameraController.lua:636-637` | `Input.GetKeyDown("ENTER") or Input.GetKeyDown("SPACE")`로 인트로 진행 |

- 영향: Mario64 데모 Lua 스크립트는 위 패턴(특히 Player.lua의 wrap 스타일)을 그대로 따르면 된다. 추가 코드 비용 없음.

---

## 11. Mario64 Demo Fixture — Design

사용자 요청에 따라, Mario64 시나리오의 모든 transition을 사전 정의한 **임시 fixture**를 plan에 추가한다. 본 절은 fixture의 형태/위치/책임/호출 방식/검증 기준만 명세한다 — 구현 코드는 후속 prompt로 분리.

### 11.1 형태 / 위치 결정

#### 형태 후보 비교

1. **`UMario64DemoAnimInstance : UAnimStateMachineInstance` 상속 클래스.**
   - 장점: UObject 생명주기에 통합, `REGISTER_FACTORY` 패턴(`AnimSingleNodeInstance.cpp:8`) 적용 가능.
   - 단점: `USkeletalMeshComponent::EnsureAnimInstance`(`SkeletalMeshComponent.cpp:133-154`)가 `EAnimationMode` enum→class 매핑을 switch로 고정해 둠 — 새 클래스를 자동 생성할 길이 없음. 외부에서 컴포넌트의 `AnimInstance`를 destroy → manual create → swap 필요. 복잡도가 높고 enum 변경 없이는 lifecycle 통합 효과가 약하다.

2. **자유 함수 `BuildMario64StateMachine(USkeleton*, sequences...)` + 호출자 측 직접 주입.**
   - 장점: 단순, 의존성 명확. 데모 helper로서 응집성은 떨어지지만 호출자가 명시적으로 단계를 본다.
   - 단점: 4개 sequence path를 보유하는 자리가 없음(사용자 요구: "path는 멤버 default 또는 생성자 인자"). stateless로는 fixture라 부르기 어렵다.

3. **`struct FMario64DemoFixture` — instance 멤버로 4개 path 보유 + `Apply(USkeletalMeshComponent*)` 단일 진입점.**
   - 장점: 4개 path를 멤버로 보유 가능. `Apply` 한 번에 (a) sequence 로드 → (b) state/transition 조립 → (c) `AnimationStateMachine` 모드 set → (d) `SetStateMachineGraph` 주입 → (e) `SetPaused(false)` 보장까지 완결. 기존 `F`-prefix struct 패턴(`FDrawCommandBuilder`(`Engine/Render/Command/DrawCommandBuilder.h:19`), `FMeshManager`)과 일관.
   - 단점: 데모 fixture가 production 도메인 코드와 한 디렉토리에 섞일 위험 → 디렉토리 분리로 해소.

- **추천**: **옵션 3 — `struct FMario64DemoFixture`**.
- **사용자 확인 필요**: 부분 — 옵션 1(상속) 대신 옵션 3(struct)을 선택해도 되는지. 데모 코드라는 점을 감안하면 옵션 3이 자연스럽지만, 사용자가 instance lifecycle을 UObject로 다루고 싶다고 하면 옵션 1로 전환 가능.

#### 위치 후보 비교

- 옵션 (i) `Engine/Asset/Animation/Core/Mario64DemoFixture.h/.cpp` — Animation Core 도메인 안.
  - 장점: 기존 animation 헤더들과 같은 디렉토리(`AnimPoseUtils.h`처럼). include 경로가 짧다.
  - 단점: production code와 demo code의 경계가 흐려진다.
- 옵션 (ii) `Engine/Asset/Animation/Demo/Mario64DemoFixture.h/.cpp` — 새 `Demo/` 서브 디렉토리.
  - 장점: 데모성 코드임을 디렉토리명이 명시. `Engine/Asset/Animation/Notify/`처럼 sub-domain 디렉토리 패턴이 이미 있다(`Engine/Asset/Animation/Notify/AnimNotify.h`).
  - 단점: 새 디렉토리 신설 비용(빌드 시스템 갱신).

- **추천**: **옵션 (ii) — `Engine/Asset/Animation/Demo/Mario64DemoFixture.h/.cpp`**. 사유: 데모 전용임을 디렉토리명이 명확히 표시. 후속에 다른 데모 fixture가 늘 가능성에 대비.
- **사용자 확인 필요**: 예 — Demo 디렉토리 신설 vs Core 안에 두기.

### 11.2 책임 명세 — fixture 포함/미포함

#### Fixture 포함 (책임)

1. **멤버 데이터**: 4개 sequence asset path를 보유.
   - `FString IdlePath, WalkPath, RunPath, JumpPath` — 생성자 인자로 받거나 멤버 default 후 외부에서 set.
2. **로드**: `Apply` 시점에 path → `UAnimSequence*` resolve. (참조 패턴: `LuaSkeletalMeshBindings.cpp:379`의 `FLuaWorldLibrary::LoadAnimSequence` 또는 `SkeletalMeshComponent.cpp:69-89`의 `ResolveCompatibleAnimSequence` 헬퍼.)
3. **그래프 조립**: 11.3 transition 표를 *등록 순서까지 포함하여* 박아둔 `unique_ptr<FAnimGraphNode_StateMachine>` 생성.
   - 각 state의 `Sub`는 `FAnimGraphNode_SequencePlayer` 단일 노드. `SetSequence(Skeleton, sequence)` 호출.
   - `bResetTimeOnEnter`/`bLooping` default는 11.3 하단의 표 적용.
4. **모드 set + 주입**: `SkelComp->SetAnimationMode(EAnimationMode::AnimationStateMachine)` → `SkelComp->PostEditProperty("Animation Mode")` (필요 시) → `Cast<UAnimStateMachineInstance>(SkelComp->GetAnimInstance())->SetStateMachineGraph(std::move(Root))`.
5. **`SetPaused(false)` 보장**: `SkelComp->SetBakedAnimPaused(false)` 또는 `SkelComp->Play(true)`. plan Risk #1 대응.
6. **(선택) `bResetTimeOnEnter` 토글 멤버**: Walk/Run의 phase mismatch 옵트인 노출. `bool bResetWalkRunOnEnter = true;`.

#### Fixture 미포함 (책임 외)

- 입력 폴링, `SetBoolVariable("bMoving"/...)` 호출 — **Lua 스크립트 책임**.
- 액터 spawn/destroy — fixture는 기존 `SkelComp`에 적용만 한다.
- 시퀀스 import — 자산이 이미 디스크/메모리에 있어야 한다(단계 0에서 확인).
- 그래프 자체의 GUI 편집 — Editor UI는 out-of-scope(7.4).

### 11.3 Transition 표 (fixture에 그대로 박힐 값)

본 표는 4.6 데모 시나리오와 동일하며, **fixture는 이 순서 그대로 `FAnimGraphNode_StateMachine::Transitions`에 push_back**한다. 순서가 평가 우선순위에 직접 영향한다(plan VP-10 검증 대상).

| # | From | To | Conditions (AND) | BlendDuration | 등록 순서 메모 |
|---|---|---|---|---|---|
| 1 | Idle | Run | `bMoving == true`, `bWantRun == true` | 0.1s | Walk보다 먼저 |
| 2 | Idle | Walk | `bMoving == true`, `bWantRun == false` | 0.1s | Run 다음 |
| 3 | Walk | Run | `bWantRun == true` | 0.1s | |
| 4 | Run | Walk | `bWantRun == false` | 0.1s | |
| 5 | Walk | Idle | `bMoving == false` | 0.15s | |
| 6 | Run | Idle | `bMoving == false` | 0.15s | |
| 7 | Idle | Jump | `bJump == true` | 0.05s | |
| 8 | Walk | Jump | `bJump == true` | 0.05s | |
| 9 | Run | Jump | `bJump == true` | 0.05s | |
| 10 | Jump | Idle | `TimeElapsed >= 0.5s` | 0.15s | J1 단순화 — 시간 기반 자동 복귀 |

**총 10개**. State 인덱스는 fixture 내부에서 `enum { Idle=0, Walk=1, Run=2, Jump=3 }`로 고정.

State별 `bResetTimeOnEnter` default:
- **Idle, Jump**: `true`.
- **Walk, Run**: `true` (default). 시각 검증 후 phase mismatch가 보이면 `false`로 전환 옵션 — `bResetWalkRunOnEnter` 멤버로 노출하거나 `#ifdef MARIO64_PRESERVE_WALK_RUN_PHASE`로 토글 가능하게 둔다.

`InitialStateIndex = 0` (Idle).

### 11.4 호출 방식

#### 옵션 비교

- **(C1) 콘솔 명령 추가 — `anim mario64 setup`.** `EditorConsoleWidget.cpp:294-295`의 `RegisterCommand("anim graph test", ...)` 패턴을 그대로 따른다. 같은 파일에 새 핸들러 `HandleMario64Setup`을 추가하고, 내부에서 `FMario64DemoFixture` 1회 생성 → `Apply(SelectedSkelComp)`.
  - 장점: 0-cost 통합. 에디터에서 즉시 트리거 가능. `anim graph test` 핸들러의 actor selection / SkelComp resolve 패턴(`EditorConsoleWidget.cpp:1366-1397`) 재사용 가능.
  - 단점: production(=non-editor) 빌드에서는 콘솔이 없을 가능성. 데모 시연 채널이 에디터에 묶임.

- **(C2) 액터 setup / Lua bootstrap.** Lua 스크립트의 `BeginPlay` 또는 `OnAttach`에서 새 Lua binding(예: `SkeletalMeshComponent:SetupMario64Demo(idle, walk, run, jump)`)을 호출.
  - 장점: 게임 빌드에도 적용. 에디터 의존 없음.
  - 단점: 새 Lua binding 1개 추가 필요(`LuaSkeletalMeshBindings.cpp`에 함수 1개). 데모 범위 초과로 갈 수 있음.

- **추천**: **(C1) 콘솔 명령 우선**. 사유: 데모 검증을 에디터에서 빠르게 시각화하는 것이 1차 목표. `anim graph test`와 동일 패턴이라 추가 코드 비용 최소. 데모가 안정되면 (C2)는 후속 작업으로.
- **사용자 확인 필요**: 일부 — (C1)만으로 데모 시연이 충분한지. 게임 빌드/Lua 진입이 필요하면 (C2)도 함께.

### 11.5 Fixture 단독 검증 체크포인트 (FCP)

VP(4.7)과는 **별개**로, fixture 자체의 통과 기준. fixture가 단독으로 의도대로 작동함을 보인다.

| ID | 체크포인트 | 확인 방법 | 통과 기준 |
|---|---|---|---|
| FCP-1 | Apply 직후 모드가 StateMachine | `SkelComp->GetAnimationMode()` | `EAnimationMode::AnimationStateMachine`과 동일 |
| FCP-2 | AnimInstance가 `UAnimStateMachineInstance`로 캐스트 가능 | `Cast<UAnimStateMachineInstance>(SkelComp->GetAnimInstance())` | 결과 nullptr 아님 |
| FCP-3 | 초기 변수 상태가 모두 false | 위 cast 결과의 `GetBoolVariable("bMoving"/"bWantRun"/"bJump", false)` | 세 값 모두 false 반환 |
| FCP-4 | Apply가 `bPaused = false` 보장 | `AnimInstance->IsPaused()` 또는 `SkelComp->IsBakedAnimPaused()` | `false` |
| FCP-5 | Transition 개수와 등록 순서 | graph root의 `Transitions.size()` 와 `Transitions[0..9].FromStateIndex/ToStateIndex` | `size() == 10`. `Transitions[0]`은 Idle→Run, `[1]`은 Idle→Walk, ..., `[9]`는 Jump→Idle (11.3 표 순서와 일치) |
| FCP-6 | 초기 활성 상태가 Idle | 첫 `Evaluate` 후 노드의 `ActiveStateIndex` | `0` (Idle) |

### 11.6 Fixture가 건드리지 않는 영역 reminder

- `EAnimationMode` enum 추가/순서 변경 금지(`SkeletalMeshComponent.h:17` 주석: serialize round-trip 대상). fixture는 기존 `AnimationStateMachine` 모드만 사용.
- baseline Notify dispatch 미구현(1.6 참조)이므로 fixture는 `EAnimTransitionConditionKind::OnNotify`/`Custom`을 사용하지 않는다. `TimeElapsed`와 `BoolVariable`만.
- 전이 진행 중 신규 트리거 무시(plan 3.B, 결정 사항)는 BlendDuration을 짧게 잡아 완화한다(이동 0.1s, 정지 0.15s, Jump 진입 0.05s).

---

## 12. Resolved Decisions

본 plan 작성 후 사용자 리뷰를 거쳐 확정된 결정을 기록한다. 후속 구현 prompt 작성 시 본 절을 참조한다.

| # | 결정 영역 | 확정 옵션 | 결정 일자 | 근거 / 메모 |
|---|---|---|---|---|
| D1 | 3.A — Key input 진입점 | **Lua script input polling** (옵션 1) | 2026-05-20 | Lua binding이 모두 존재(`Input.GetKey*`, `SkeletalMeshComponent:SetStateBool`). 10.3에서 기존 Lua 액터들의 동일 패턴 사용 확인. 추가 C++ 코드 0. |
| D2 | 3.B — Transition 중 입력 정책 | **전이 중 신규 트리거 거부 유지** | 2026-05-20 | 현재 코드(`AnimGraph_StateMachine.cpp:88`) 동작 그대로. 완화 수단으로 BlendDuration을 짧게(0.1s/0.15s/0.05s) 잡는다. 큐잉/취소는 후속 작업(7.3). |
| D3 | 3.C — State variable 모델 | **bool flags only — Mario64 모델** (`bMoving`, `bWantRun`, `bJump`) | 2026-05-20 | enum/blackboard 도입 없음. `UAnimStateMachineInstance::BoolVariables`(`AnimStateMachineInstance.h:38`) 그대로 사용. |
| D4 | Jump 모델 | **J1 — 시간 기반 자동 복귀** (`TimeElapsed >= 0.5s`) | 2026-05-20 | 물리/Z velocity/ground detection 미포함. J2(공중 상태)는 후속(7.8). |
| D5 | 방향 전환 (yaw rotation) | **데모 범위 밖** | 2026-05-20 | 제자리 이동 애니메이션만 검증. 7.7에 out-of-scope로 기록. |

추가로 본 patch에서 plan에 새로 도입된 fixture 관련 항목은 11절 참조. fixture 형태/위치/호출 방식 중 사용자 확인이 필요한 항목은 11.1, 11.4에 표시됨.

---

**이 patch 적용 후 사용자 검토 → 구현 prompt 작성.**
