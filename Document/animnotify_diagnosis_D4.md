# AnimNotify 진단 D4 — GetActiveNotifies override 인프라 진단

D3([Document/animnotify_diagnosis_D3.md](animnotify_diagnosis_D3.md)) §5.1 옵션 1 ("`GetActiveNotifies` override 신설") 사전 진단. 선행 문서: D1, D2, D3, P-Fix5.

조사 방법: code view + grep only. 코드 수정 금지. D3 결론(V2 root cause) 재검증 없음. 모든 라인 인용은 working tree에서 재확인됨.

전제: ① From→To 전환 시점 기준 1개 sequence만 활성, blend 중에는 From이 활성 / ② 옵션 1만 / ③ D3 결론 수용.

---

## 1. 요약

- **[확정] StateMachine 활성 state query 메커니즘**: `UAnimInstance::AnimGraphPtr->GetRoot()` → `dynamic_cast<FAnimGraphNode_StateMachine*>` → `States[ActiveStateIndex].Sub`. `ActiveStateIndex`는 blend 완료까지 From을 가리키므로 **전제 1 자연 호환**. 단 `ActiveStateIndex`/`ActiveTransitionIndex`가 private이라 getter 신설 필요.
- **[확정] AnimGraph 활성 sequence query 메커니즘**: 현 working tree에서 `EAnimationMode::AnimationGraph`는 console debug 외 production 사용 0건. 본 진단은 SequencePlayer 단일 root 케이스만 다루고 Blend2/BlendN은 **장래 대비**로 보류 (전제 1 하에 선택 정책 미정).
- **[확정] 시그니처 유지 가능 여부**: **Y**. 단일 포인터 반환 유지 가능. 단 G6의 PrevTime mismatch 해결이 동반되어야 함. 또한 **`GetEffectivePlayLength()`도 동시에 override 필요** (Update의 line 35-39 Length≤0 early return이 GetActiveNotifies보다 먼저 차단).
- **[확정] PrevTime mismatch(G6.3) 처리 방안**: **후보 b 권장** — `UAnimStateMachineInstance`가 ① `Update`를 override해 state-local time 기반으로 notify firing 수행, ② state 전환 감지 시 자체 PrevStateLocalTime을 0으로 reset. 후보 a/c는 G6.3 표 참조.
- **[전제 충돌] 1건 발견**: `UAnimInstance::Update` line 34-39의 `GetEffectivePlayLength() ≤ 0` early return 이 D3 결론(V2 root cause)보다 더 상류 차단. D3는 GetActiveNotifies만 root cause로 지목했으나, 실제로는 GetEffectivePlayLength도 동일하게 override 필요. **D3 결론을 부정하지는 않으나 작업 범위가 GetActiveNotifies + GetEffectivePlayLength 2개로 확장**.
- **[확인 불가] 없음** — G1~G7 모두 매핑 완료.
- **[구현 prompt로 위임]**: ① getter 신설 시그니처 확정 (FAnimGraphNode_StateMachine::GetActiveStateIndex 등), ② AnimGraph mode의 Blend 노드 처리 정책 (장래), ③ override Update 시 `LastDeltaTime` 처리, ④ G7의 `TriggeredNotifiesThisFrame` clear 시점 변경 가능성.

---

## 2. G1 — StateMachine 활성 state query

### G1.1 `UAnimStateMachineInstance` 멤버 전수 조사

파일: [AnimStateMachineInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h)

| 멤버명 | 타입 | 접근자 | 위치 | 용도 |
|---|---|---|---|---|
| `BoolVariables` | `std::unordered_map<FName, bool, FName::Hash>` | private | [AnimStateMachineInstance.h:38](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h:38) | StateMachine BoolVariable 보관. transition 조건 평가용. |

derived 멤버는 `BoolVariables` 단일. graph 자체는 base `UAnimInstance::AnimGraphPtr` (protected, [AnimInstance.h:106](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:106)) 에 보관됨.

### G1.2 `FAnimGraphNode_StateMachine` 구조 조사

파일: [AnimGraph_StateMachine.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h)

| 멤버명 | 타입 | 접근자 | 위치 | 용도 |
|---|---|---|---|---|
| `States` | `TArray<FAnimState>` | public | [AnimGraph_StateMachine.h:63](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:63) | state 정의 배열 |
| `Transitions` | `TArray<FAnimTransition>` | public | [AnimGraph_StateMachine.h:64](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:64) | transition 정의 배열 |
| `InitialStateIndex` | `int32` | public | [AnimGraph_StateMachine.h:65](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:65) | 초기 state index |
| `ActiveStateIndex` | `int32 = -1` | **private** | [AnimGraph_StateMachine.h:75](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:75) | 현재 활성 state index. blend 중에는 From을 유지. |
| `ActiveTransitionIndex` | `int32 = -1` | **private** | [AnimGraph_StateMachine.h:76](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:76) | 진행 중 transition index. -1이면 transition 없음. |
| `TransitionElapsed` | `float = 0.0f` | private | [AnimGraph_StateMachine.h:77](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:77) | blend 누적 시간 |
| `StateLocalTimes` | `TArray<float>` | private | [AnimGraph_StateMachine.h:78](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:78) | state별 local time. size == States.size(). |
| `ScratchFrom`/`ScratchTo` | `TArray<FTransform>` | private | [AnimGraph_StateMachine.h:79-80](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:79) | blend 임시 버퍼 |

**Transition 표현 방식**:
- `ActiveTransitionIndex >= 0` 동안 blend 진행 중. `Transitions[ActiveTransitionIndex].FromStateIndex` / `.ToStateIndex` 가 From/To.
- blend 완료 시점 ([AnimGraph_StateMachine.cpp:74-85](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp:74)): `TransitionElapsed >= BlendDuration` → `ActiveStateIndex = T.ToStateIndex`, `ActiveTransitionIndex = -1`.
- **중요**: `ActiveStateIndex`는 blend가 완료될 때까지 From 그대로 유지. To로 flip되는 시점이 blend 완료 시점.

### G1.3 활성 state 접근 경로

```
UAnimStateMachineInstance(derived)::GetActiveNotifies() override
  → AnimGraphPtr (protected base member, accessible to derived)
  → AnimGraphPtr->GetRoot()  [public, returns FAnimGraphNode_Base*]
  → dynamic_cast<FAnimGraphNode_StateMachine*>(root)
  → SM->ActiveStateIndex     [PRIVATE - getter 필요]
  → SM->States[idx].Sub      [States 공개, Sub 공개]
```

**접근 가능성**: 일부 private — getter 신설 필요.

**신설 필요 getter 목록** (구현 prompt에서 추가):

| 클래스 | 함수 시그니처 | 용도 |
|---|---|---|
| `FAnimGraphNode_StateMachine` | `int32 GetActiveStateIndex() const` | 활성 state idx |
| `FAnimGraphNode_StateMachine` | `int32 GetActiveTransitionIndex() const` | 진행 중 transition idx (-1 if none). 부수 진단용 — 본 override 자체는 사용 안 함 (전제 1 하에 ActiveStateIndex만으로 충분). |
| `FAnimGraphNode_StateMachine` | `float GetStateLocalTime(int32 StateIdx) const` 또는 `const TArray<float>& GetStateLocalTimes() const` | G6.3 후보 b 구현에 필수 — state local time 직접 query |

### G1.4 전환 시점(transition trigger) 검출

| 후보 | 정의 | 코드 위치 |
|---|---|---|
| (a) | transition 조건 평가 시점 (조건 first-true, blend 시작 같은 frame) | [AnimGraph_StateMachine.cpp:88-105](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp:88) — `ActiveTransitionIndex = i` set |
| (b) | blend 시작 시점 (transition state 진입) | 같은 코드와 동일 — (a)와 같은 frame |
| (c) | blend 완료 시점 (To state 단독 활성) | [AnimGraph_StateMachine.cpp:74-85](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp:74) — `ActiveStateIndex = NewActive`, `ActiveTransitionIndex = -1` |

**전제 1의 "From→To 전환 시점" 확정**: **(c) blend 완료 시점**.

근거: 전제 1이 "blending 구간에는 From 활성, 전환 시점에 To로 전환" 으로 정의 → blend 진행 중은 From 유지 → blend 완료 시 To로 flip. 코드의 `ActiveStateIndex` 동작과 정확히 일치.

**자연 호환의 의미**: 본 override가 `States[ActiveStateIndex].Sub`를 query하면 전제 1의 정책이 추가 분기 없이 자동 보장됨. 별도 `IsTransitioning()` 체크 불필요.

### G1.5 음성 검증 — 의도적 정보 손실

전제 1을 따를 때 발생 가능한 손실:
- **blend 중간에 To state의 notify 발화 누락**: 예) blend duration=0.5s, blend 중 0.3s 시점에 To의 TriggerTime=0.05 notify가 도달해야 하는데, 전제 1 하에서는 blend 완료 시점까지 From이 활성이라 발화 안 됨.
- **blend 시간이 0인 경우**: `BlendDuration=0`이면 [AnimGraph_StateMachine.cpp:75](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp:75)에서 `TransitionElapsed >= 0` 이 같은 frame에 true → 같은 frame에 blend 완료 → ActiveStateIndex flip. 즉 즉시 전환. 손실 없음.

**손실 빈도/심각도 평가**: blend duration이 길수록(>0.3s) 손실 큼. 게임플레이 sound notify는 보통 state 시작 직후(예: punch sound at frame 1)에 배치되므로 영향 큼.

**완화 옵션 (전제 1 해제 시 — 본 D4 scope 외)**:
- transition 중에 To의 (StateLocalTime range) 안의 notify도 발화하는 dual-stream 모델 필요. 본 진단에서는 향후 prompt로 위임.

---

## 3. G2 — State → Sequence 추출

### G2.1 State 노드 구조 조사

파일: [AnimGraph_StateMachine.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h)

| 멤버 | 타입 | 접근자 | 위치 |
|---|---|---|---|
| `Name` | `FName` | public | [AnimGraph_StateMachine.h:25](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:25) |
| `Sub` | `std::unique_ptr<FAnimGraphNode_Base>` | public | [AnimGraph_StateMachine.h:26](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:26) |
| `bResetTimeOnEnter` | `bool = true` | public | [AnimGraph_StateMachine.h:27](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:27) |
| `bLooping` | `bool = true` | public | [AnimGraph_StateMachine.h:28](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:28) |
| `SubLengthHint` | `float = 0.0f` | public | [AnimGraph_StateMachine.h:29](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:29) |

**Sub 보유 방식**: **(c) generic base pointer** — `unique_ptr<FAnimGraphNode_Base>`. 실제 타입은 runtime에 SequencePlayer 또는 StateMachine 또는 Blend* 중 하나.

### G2.2 `FAnimGraphNode_SequencePlayer` 구조 조사

파일: [AnimGraph.h:49-63](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:49)

| 멤버 | 타입 | 접근자 | 위치 | nullptr 조건 |
|---|---|---|---|---|
| `Sequence` | `const UAnimSequence*` | public | [AnimGraph.h:60](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:60) | `SetSequence(_, nullptr)` 호출 시. 또는 `SetSequence` 미호출 시(default nullptr). |
| `DataModel` | `const UAnimDataModel*` | public | [AnimGraph.h:61](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:61) | cache — Sequence와 동기 |
| `TrackToBoneIndex` | `TArray<int32>` | public | [AnimGraph.h:62](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:62) | cache |

**Sequence nullptr 발생 조건**:
- 초기 (SetSequence 미호출)
- BuildSequenceNode 가 호출되지 않음 — Lua config에서 `sequence`/`anim` 필드 누락 + submachine도 없음 → `BuildStateMachineNodeFromLua` 실패 (정상 path에서는 발생 안 함, 발생 시 그래프 자체 build 실패).
- asset load 실패 — `Animation.LoadSequence(path)` 반환이 nil → Lua handle invalid → ResolveSequenceFromObject가 nullptr → BuildSequenceNode가 nullptr 반환 → State.Sub가 nullptr → build 실패.

### G2.3 State에 SequencePlayer가 여러 개일 수 있는지

- **단일 SequencePlayer 보장**: 현재 working tree의 Lua build 경로([LuaSkeletalMeshBindings.cpp:220-230](../KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:220) `BuildSequenceNode`)는 항상 단일 SequencePlayer를 생성. State.Sub는 SequencePlayer 또는 nested StateMachine 둘 중 하나.
- **샘플 확인** ([MarioControllerAnimGraphBase.lua](../KraftonEngine/LuaScripts/Game/MarioControllerAnimGraphBase.lua)): LOCOMOTION state는 nested submachine, JUMP1/JUMP2/JUMP3 state는 단일 sequence. 검증됨.
- **Blend2/BlendN을 State.Sub로 두는 경로**: 현 Lua build에는 없음. 향후 추가될 가능성 있으나 전제 1 하에서 별도 선택 정책 필요 — 본 진단 범위 외.

### G2.4 `BuildStateMachineFromLua` 경로

코드: [LuaSkeletalMeshBindings.cpp:287-345](../KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:287)

흐름:
1. `MaybeStates = Config["states"]` 읽기
2. 각 state entry에서:
   - `sequence` 또는 `anim` 필드 → `ResolveSequenceFromObject` → `UAnimSequence*` 획득
   - `BuildSequenceNode(Skeleton, Sequence)` → `FAnimGraphNode_SequencePlayer` 단일 노드 생성 후 base pointer 반환
   - State.Sub에 대입
3. sequence가 없으면 `submachine` 필드 → 재귀적 `BuildStateMachineNodeFromLua` → nested StateMachine
4. 둘 다 없으면 build 실패

**주입 시점 재확인 (D3 §V3.3)**: BeginPlay 끝 시점. 변경 없음.

`BuildSequenceNode` 본체 ([LuaSkeletalMeshBindings.cpp:220-230](../KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:220)):
```cpp
std::unique_ptr<FAnimGraphNode_Base> BuildSequenceNode(USkeleton* Skeleton, UAnimSequence* Sequence)
{
    if (!Skeleton || !Sequence) return nullptr;
    auto Node = std::make_unique<FAnimGraphNode_SequencePlayer>();
    Node->SetSequence(Skeleton, Sequence);
    return Node;
}
```

---

## 4. G3 — AnimGraph root 노드 traversal

### G3.1 `UAnimGraphInstance` 멤버 전수 조사

파일: [AnimGraphInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.h)

derived 멤버 없음. 모든 데이터는 base `UAnimInstance::AnimGraphPtr` 에 저장. method:
- `SetRootGraph(unique_ptr<FAnimGraphNode_Base>)` — 임의 root 노드 주입 ([AnimGraphInstance.h:29](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.h:29))

root 접근: `AnimGraphPtr->GetRoot()` (public, [AnimGraph.h:110](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:110)).

### G3.2 Graph 노드 base 클래스 visit 인터페이스

파일: [AnimGraph.h:38-42](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:38)

```cpp
struct FAnimGraphNode_Base {
    virtual ~FAnimGraphNode_Base() = default;
    virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) = 0;
};
```

- **visit / traverse / GetActiveChild 메서드**: ✗ 없음.
- **활성 leaf SequencePlayer 찾기**: **(b) visit 없음 — dynamic_cast 분기 필요**.

판정:
- `dynamic_cast<FAnimGraphNode_SequencePlayer*>(root)` → 성공 시 그 sequence의 notifies 반환.
- `dynamic_cast<FAnimGraphNode_StateMachine*>(root)` → StateMachine 재귀.
- `dynamic_cast<FAnimGraphNode_Blend2/BlendN*>(root)` → 활성 child 1개 선택 정책 필요 (전제 1 하에 미정).

### G3.3 Blend 노드 처리

- Blend2 ([AnimGraph.h:71-82](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:71)): ChildA, ChildB, Alpha. 전제 1 하에 1개 선택 정책 미정 (예: Alpha < 0.5 → ChildA, else ChildB; 또는 항상 ChildA "from-like" 의미).
- BlendN ([AnimGraph.h:92-101](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:92)): Children[], Weights[]. 최고 weight 자식 선택 정책 가능. 미정.
- **현재 사용처**: Lua build path에서 Blend 노드를 State.Sub로 두는 경로 없음. 본 진단에서 단순화 — Blend 노드 처리는 장래 대비.

### G3.4 AnimGraph mode 사용 시나리오

`SetAnimationMode(EAnimationMode::AnimationGraph)` 호출처:
- [EditorConsoleWidget.cpp:1434](../KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1434) — console 명령으로 단일 SequencePlayer를 root로 주입하는 debug 흐름.
- Prefab/Lua: AnimationGraph 명시 0건.

**판정**: AnimationGraph mode는 현재 production gameplay에서 사용 안 됨. **G3 관련 override는 장래 대비 성격**. 구현 prompt에서 우선순위 낮춰도 silent 해소에 영향 없음. 단순 케이스(root가 SequencePlayer) 만 지원하고 Blend/StateMachine 노드는 후속 작업으로 분리 가능.

---

## 5. G4 — Transition 처리 정책 검증 (전제 1 하)

### G4.1 전제 1의 정확한 시점 정의

G1.4에서 전환 시점 = (c) blend 완료 시점 으로 확정.

| 시점 | ActiveStateIndex | ActiveTransitionIndex | 활성 sequence (전제 1) | 비고 |
|---|---|---|---|---|
| Frame N-1 (blend 시작 직전) | A | -1 | A | 일반 state |
| Frame N (blend 시작 frame) | A | i (just set) | A | `ActiveTransitionIndex = i`로 set되었지만 ActiveStateIndex 아직 A |
| Frame N+1 ... N+k (blending) | A | i | A | TransitionElapsed 누적, ActiveStateIndex 미변경 |
| Frame N+k+1 (blend 완료 frame) | B (flipped at line 78) | -1 (reset at line 83) | B | `ActiveStateIndex = NewActive` 후 To 활성 |
| Frame N+k+2 이후 | B | -1 | B | 일반 |

**결론**: `States[ActiveStateIndex].Sub`를 매 frame query하면 정확히 전제 1을 만족. 추가 분기 불필요.

### G4.2 전환 시점에 발화되어야 하는 notify

#### Case 1 — From의 마지막 notify 누락 가능성

시나리오:
- From state seq length=2.0s. From의 마지막 notify TriggerTime=1.95.
- Blend 시작 시점 (frame N): From's StateLocalTime=1.90.
- Frame N에서 transition 조건 발화 → ActiveTransitionIndex=i. From의 StateLocalTime은 line 63에서 += DeltaTime 진행 (예: 1.90 → 1.92).
- Blend 진행 중에도 From의 StateLocalTime은 line 63에서 계속 진행.
- 만약 frame N+2에 From's StateLocalTime이 1.95를 통과하면 정상 발화.

**조건**: From의 마지막 notify는 blend 진행 중에도 StateLocalTime 진행 덕분에 정상 발화. 누락 없음 (단, G6 PrevTime mismatch 해결 전제).

#### Case 2 — To의 첫 notify 발화 가능성

시나리오:
- Blend 완료 시점(frame N+k+1): ActiveStateIndex=B로 flip. To의 StateLocalTime은 blend 진행 중 이미 누적 (line 69: `StateLocalTimes[T.ToStateIndex] += Ctx.DeltaTime`). 즉 N+k+1 frame 진입 시 To의 StateLocalTime = (k+1)*DT (또는 bResetTimeOnEnter=true이면 line 79-82에서 reset).
- Default: `bResetTimeOnEnter=true` ([AnimGraph_StateMachine.h:27](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:27)) — flip 시점에 0으로 reset.

**조건**:
- `bResetTimeOnEnter=true` 케이스: Frame N+k+1에 ActiveStateIndex=B + To.StateLocalTime=0. To의 TriggerTime>0인 첫 notify는 N+k+1+DT 이상 누적 후 발화.
- `bResetTimeOnEnter=false` 케이스: Frame N+k+1 진입 시 To.StateLocalTime 이미 (k+1)*DT 누적. To.TriggerTime ≤ 이 누적값인 notify는 이미 지나간 상태 → 누락 가능.

**중요 문제 — PrevTime mismatch**: From → To flip 시점에 AnimInstance::PreviousTime / CurrentTime 이 어떻게 동기화되는가? 본 D4의 G6.3에서 깊이 분석.

### G4.3 P-Fix5 시간 모델과의 조합

P-Fix5 시간 모델 ([P-Fix5 implementation doc](animnotify_pfix5_implementation.md)):
- AnimInstance가 CurrentTime/PreviousTime 소유, 매 Tick에서 `Update(DeltaTime)`가 `PreviousTime = CurrentTime`, `CurrentTime += DeltaTime * PlaybackSpeed` 갱신.
- `SetEvaluationTime(t)` → `PrevTime = CurTime = t` (scrub-suppress 안전망).

StateMachine 모드와의 충돌점:
- AnimInstance::CurrentTime/PreviousTime은 **AnimInstance level** 시간 (단일 누적자).
- StateMachine은 별도 **state-local time** 보유 (StateLocalTimes[]).
- **두 시간이 서로 다른 의미** — AnimInstance::CurrentTime은 StateMachine evaluation에 사용되지 않음.
- `FAnimGraphNode_StateMachine::Evaluate`가 `Ctx.TimeSeconds`를 무시하고 자체 StateLocalTimes를 사용 ([AnimGraph_StateMachine.cpp:109-113](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp:109)).

**결론**: AnimInstance::Update의 notify trigger loop가 사용하는 PrevTime/CurrTime은 StateMachine의 활성 sequence와 무관 → G6.3의 mismatch가 발생. 자세한 분석 G6 참조.

---

## 6. G5 — 시그니처 유지 검증

### G5.1 현재 시그니처 재확인

[AnimInstance.h:84](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:84) (working tree 재확인):
```cpp
virtual const TArray<FAnimNotifyEvent> *GetActiveNotifies() const { return nullptr; }
```

**변경 여부**: D3에서 인용된 line 84와 동일. 변경 없음.

### G5.2 단일 포인터 반환의 충분성 검증

전제 1 하에서 매 frame "활성 sequence 1개" 보장:

| 케이스 | 활성 sequence | 반환 처리 |
|---|---|---|
| 일반 (transition 없음) | `States[ActiveStateIndex].Sub` 안의 SequencePlayer | sequence의 notifies pointer |
| Transition 진행 중 (전제 1) | 같음 (ActiveStateIndex가 From 유지) | 같음 |
| Transition 완료 frame | 같음 (ActiveStateIndex가 To로 flip된 상태) | To sequence의 notifies pointer |
| BeginPlay 직후 첫 frame | StateMachine::Evaluate 미실행 → `ActiveStateIndex == -1` ([AnimGraph_StateMachine.h:75](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:75)) | nullptr 반환 안전 |
| Graph 미구성 | `AnimGraphPtr` null 또는 `GetRoot()` null | nullptr 반환 안전 |
| Cast 실패 (root가 StateMachine 아님) | dynamic_cast 실패 | nullptr 반환 안전 |
| State.Sub가 nested StateMachine | 재귀 처리 필요 — 본 진단은 한 단계 재귀 권장 | nested의 active state로 한 단계 더 들어감 |
| State.Sub가 nullptr | (BuildStateMachineFromLua 실패 케이스 — 정상 path에서는 발생 안 함) | nullptr 반환 안전 |

**nullptr 반환 안전성**: `UAnimInstance::Update` [AnimInstance.cpp:83](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:83) `if (Notifies)` 가드가 nullptr 처리. 안전 보장.

**판정**: ✓ 단일 포인터 반환 시그니처 유지 가능.

### G5.3 `UAnimInstance::Update`의 notify trigger loop 호환성

[AnimInstance.cpp:81-98](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:81) 재인용:
```cpp
const TArray<FAnimNotifyEvent> *Notifies = GetActiveNotifies();
TArray<const FAnimNotifyEvent *> LocalTriggered;
if (Notifies)
{
    for (const FAnimNotifyEvent &Notify : *Notifies)
    {
        if (Notify.IsTriggeredBetween(PreviousTime, CurrentTime, Length))
        {
            TriggeredNotifiesThisFrame.push_back(Notify.NotifyName);
            LocalTriggered.push_back(&Notify);
        }
    }
}
if (!LocalTriggered.empty()) { DispatchTriggeredNotifies(LocalTriggered); }
```

- **LocalTriggered**: function-local 변수 — frame 간 reset 자동. ✓
- **frame 간 다른 sequence 반환**: 가능. 단 PrevTime/CurrentTime이 이전 frame의 활성 sequence(=From)와 현재 frame의 활성 sequence(=To)에 모두 의미를 가질 수 없음 — **G6.3의 핵심 mismatch**.

**판정**: loop 구조 자체는 호환. 시간 동기화는 G6에서 해결 필요.

### G5.3.1 **전제 충돌 발견 — `GetEffectivePlayLength` 동시 override 필요**

본 진단 진행 중 발견: `UAnimInstance::Update`의 line 34-39 ([AnimInstance.cpp:34-39](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:34))
```cpp
const float Length = GetEffectivePlayLength();
if (Length <= 0.0f)
{
    PreviousTime = CurrentTime = 0.0f;
    return;
}
```

- `GetEffectivePlayLength`는 [AnimInstance.h:79](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:79) 정의: base는 `return 0.0f`.
- `UAnimSingleNodeInstance`만 override ([AnimSingleNodeInstance.cpp:70-73](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:70)).
- `UAnimStateMachineInstance` / `UAnimGraphInstance` 둘 다 override 없음 → 0 반환 → **GetActiveNotifies 호출 전에 line 38에서 early return**.

**즉 D3가 지목한 root cause (`GetActiveNotifies` 미override) 보다 더 상류에 `GetEffectivePlayLength` 미override가 차단막으로 작동**. D3 결론은 부정되지 않음 — `GetActiveNotifies`도 override 필요한 것은 사실 — 그러나 `GetEffectivePlayLength`도 **반드시 동시에 override** 되어야 dispatch path에 도달 가능.

**전제 충돌 보고**: D3 결론을 부정하지는 않으나 작업 범위가 단일 override → 이중 override로 확장. 사용자 결정 필요한 항목은 없으나 본 사실을 D4 §1 요약 + 후속 구현 prompt에 반영해야 함.

**구현 시 override 방향**:
- `UAnimStateMachineInstance::GetEffectivePlayLength()`: `States[ActiveStateIndex].SubLengthHint` 반환. SubLengthHint > 0 이면 OK, 0이면 일시 0 반환 → Update가 early return → 그 frame은 notify 없음. ActiveStateIndex가 -1 (initial 이전)인 경우도 0 반환. 안전.

---

## 7. G6 — P-Fix5 시간 모델 호환 (⚠ 최대 위험)

### G6.1 P-Fix5 시간 모델 재인용

P-Fix5 ([animnotify_pfix5_implementation.md](animnotify_pfix5_implementation.md) §4 Before/After):
- AnimInstance::Update 매 Tick:
  - `PreviousTime = CurrentTime`
  - `CurrentTime += DeltaTime * PlaybackSpeed`
  - `IsTriggeredBetween(PreviousTime, CurrentTime, Length)` 평가
- `SetEvaluationTime(t)`: `PrevTime = CurTime = t` (scrub 시 사이 notify suppress 안전망).

notify trigger 조건 ([AnimNotify.h:78-89](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:78)):
- forward 케이스: `PrevTime < TriggerTime && TriggerTime <= CurTime`
- 루프 wrap 케이스: `TriggerTime > PrevTime || TriggerTime <= CurTime`

### G6.2 StateMachine의 state별 time 추적 방식

- 각 state의 time: `FAnimGraphNode_StateMachine::StateLocalTimes` (private array, [AnimGraph_StateMachine.h:78](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:78))
- time 진행: `StateMachine::Evaluate` 내부 line 63 + 69에서 `StateLocalTimes[idx] += Ctx.DeltaTime`
- state 전환 reset: line 79-82 (blend 완료 시 `bResetTimeOnEnter=true`이면 `StateLocalTimes[NewActive] = 0`). 또한 line 99-102 (blend 시작 시점에 To state도 같은 처리).

**time 관리 위치**: StateMachine 노드 내부. **AnimInstance 멤버 PrevTime/CurrTime과 무관**.

### G6.3 활성 sequence 변경 시 PrevTime 처리 — ⚠ 핵심 위험

#### Mismatch 발생 코드 추적

Frame N+k+1 (blend 완료 frame):
1. `USkeletalMeshComponent::TickComponent(DT)` ([SkeletalMeshComponent.cpp:390-410](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:390))
2. → `AnimInstance->Update(DT)` ([AnimInstance.cpp:29](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:29))
   - line 34: `Length = GetEffectivePlayLength()` (override되면 ActiveSeq=A의 length 반환 — **이 시점 ActiveStateIndex 아직 A**)
   - line 49: `PreviousTime = CurrentTime` (= 이전 frame A 기준 누적값)
   - line 50: `NewTime = CurrentTime + DT * PlaybackSpeed`
   - line 52-77: wrap on A's Length
   - line 81: `Notifies = GetActiveNotifies()` → **이 시점에도 ActiveStateIndex는 아직 A** → A의 notifies 반환. `IsTriggeredBetween(PrevTime, CurrTime, A.Length)` 평가. **A 기준 정상 동작**.
3. → `AnimInstance->EvaluateGraph()` ([SkeletalMeshComponent.cpp:400](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:400))
   - → `StateMachine::Evaluate` 호출
   - line 65-71: `ActiveTransitionIndex >= 0` → TransitionElapsed += DT
   - line 74-85: TransitionElapsed >= BlendDuration 만족 → **여기서 `ActiveStateIndex = To` flip**
   - line 109-127: 새 ActiveStateIndex(=To) 기준 pose 평가

**즉 Frame N+k+1의 Update에서는 ActiveStateIndex=A 기준으로 notify 평가됨**. ActiveStateIndex flip은 EvaluateGraph 내부 — Update보다 늦게.

Frame N+k+2 (To 단독 활성 첫 frame):
1. → `AnimInstance->Update(DT)`
   - line 34: `Length = GetEffectivePlayLength()` (이제 ActiveStateIndex=B → **B의 length 반환**)
   - line 49: `PreviousTime = CurrentTime` (= 이전 frame의 누적값, **A's domain**)
   - line 50: `NewTime = CurrentTime + DT * PlaybackSpeed`
   - line 52-60 (looping): `fmod(NewTime, B.Length)` → 새 wrap이 B의 length 기준
   - line 81: `Notifies = GetActiveNotifies()` → B의 notifies
   - line 87: `IsTriggeredBetween(PreviousTime, CurrentTime, B.Length)` 평가:
     - PrevTime = (A의 누적값, 예 1.95)
     - CurrTime = fmod(1.95 + DT, B.Length=1.0) = ... 일관성 없음
     - B 기준에서 의미 없는 PrevTime → 거의 모든 B의 notify가 잘못된 wrap 케이스 판정.

**mismatch 확정**:
- PrevTime은 A의 시간 누적값 (의미 없음 in B's domain).
- CurrTime은 B의 length로 wrap된 값.
- B의 notify에 대한 IsTriggeredBetween 판정이 wrong domain → 임의 결과.

#### 회피 방안 비교

| 후보 | 방안 | 코드 변경 범위 | P-Fix5 호환 | D7 호환 |
|---|---|---|---|---|
| **a** | 전환 시 PrevTime을 0으로 강제 reset | `UAnimInstance::Update` 또는 `EnterState` 1곳. State flip 감지 로직 신설 필요 (이전 frame의 ActiveStateIndex 기억). | 부분 호환 — `SetEvaluationTime`의 PrevTime=CurTime 의도와 미세 차이 (scrub용 안전망과 flip-reset이 둘 다 PrevTime을 손댐). 충돌 없음. | OK — D7은 scrub/reverse 범위 외이고 flip-reset은 정상 forward 재생에서만 발생. |
| **b** | UAnimStateMachineInstance가 `Update` 자체 override — state-local time을 직접 notify firing에 사용. AnimInstance::PrevTime/CurrTime은 무시. | `UAnimStateMachineInstance.h/cpp` 에 Update override + PrevStateLocalTime 멤버 추가. 약 30~50줄. | 호환. AnimInstance::CurrentTime은 mirror용으로 SkeletalMeshComponent::BakedAnimTime이 읽지만 ([SkeletalMeshComponent.cpp:409](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:409)) override 안에서 적절히 동기화 가능. | OK — D7 범위 외. SetEvaluationTime을 StateMachine에서 어떻게 해석할지는 별도 결정 (state-local time을 어떻게 set? — 보통 외부에서 호출 안 함). |
| **c** | notify trigger loop를 sequence 단위로 호출 (전환 frame에 From의 잔여 + To의 시작 둘 다 처리) | `Update` 내부에 sequence-list 기반 loop. 또는 StateMachine override Update가 직접 두 sequence 처리. 큰 변경. | 호환 가능. | OK. blend 중 dual-active를 의도적으로 처리하는 시맨틱이 전제 1과 약간 다름 (전제 1은 단일 활성 — c는 boundary frame에 한해 dual). |

#### 권고안 — 후보 b

**근거**:
- AnimInstance::PrevTime/CurrTime은 SingleNode 모델에 fitted된 design — StateMachine에는 의미 부합 안 함. 강제 reset(a)은 그 부합 안 함을 가린 채 우회하는 hack.
- StateMachine은 이미 state-local time을 보유 ([StateLocalTimes](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:78)). 같은 time을 notify firing에도 사용하는 것이 자연.
- Override Update는 base 의 Length-check / pause 처리 / TriggeredNotifiesThisFrame.clear 등을 일부 재구현해야 하나, 분리된 책임으로 가독성/유지보수성 더 좋음.
- (c)는 dual-active 처리로 정확도 더 높으나 전제 1 (단일 활성)과 시맨틱 차이. 향후 prompt에서 전제 1 해제 시 (c) 채택 검토.

**후보 b의 구현 sketch** (구현 prompt에서 정밀화):
```cpp
// UAnimStateMachineInstance.h 에 추가
private:
    int32 PrevActiveStateIndex = -1;
    float PrevStateLocalTime   = 0.0f;
// .cpp에서 Update override
void UAnimStateMachineInstance::Update(float DeltaTime) {
    LastDeltaTime = bPaused ? 0.0f : DeltaTime;
    TriggeredNotifiesThisFrame.clear();
    if (bPaused) return;

    // StateMachine 자체의 시간 진행은 Evaluate 내부에서 처리됨.
    // 이 override는 EvaluateGraph 호출 후가 아니라 호출 전이라
    // 직전 frame 의 state time을 사용. 즉 trigger 판정은 "직전 frame 평가 종료 시점 → 이전 frame 시작 시점" 의 변화 분.
    // 더 정확한 구현은 EvaluateGraph 안에서 notify trigger를 수행하도록 책임 이동 (옵션 2 영역, 본 prompt scope 외).

    // 활성 state + state-local time query (getter 신설 후)
    FAnimGraphNode_StateMachine* SM = ResolveStateMachineRoot();
    if (!SM || SM->GetActiveStateIndex() < 0) return;

    const int32 ActiveIdx = SM->GetActiveStateIndex();
    const float CurrStateTime = SM->GetStateLocalTime(ActiveIdx);

    // state 전환 감지
    if (ActiveIdx != PrevActiveStateIndex) {
        PrevStateLocalTime = 0.0f;     // 새 state는 from-zero 시작 (bResetTimeOnEnter=true 가정)
        PrevActiveStateIndex = ActiveIdx;
    }

    // notify firing — state-local time 기반
    const TArray<FAnimNotifyEvent>* Notifies = GetActiveNotifies();
    const float Length = SM->GetStateSubLengthHint(ActiveIdx);
    if (Notifies && Length > 0.0f) {
        TArray<const FAnimNotifyEvent*> LocalTriggered;
        for (auto& N : *Notifies) {
            if (N.IsTriggeredBetween(PrevStateLocalTime, CurrStateTime, Length)) {
                TriggeredNotifiesThisFrame.push_back(N.NotifyName);
                LocalTriggered.push_back(&N);
            }
        }
        if (!LocalTriggered.empty()) DispatchTriggeredNotifies(LocalTriggered);
    }
    PrevStateLocalTime = CurrStateTime;
}
```

**주의**: 위 sketch는 한 가지 미해결 항목 — `CurrStateTime`은 직전 frame `Evaluate` 종료 시점 값. Update가 Evaluate보다 먼저 호출됨 ([SkeletalMeshComponent.cpp:398-400](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:398))이므로 본 frame 의 state time advance를 반영하지 못함. **본 진단의 sketch는 정밀도가 1 frame 지연**. 더 정확한 구현은 Evaluate 안에서 notify trigger를 수행하는 옵션 2와 합쳐야 함. 본 D4의 sketch는 구현 prompt에서 위 frame 지연을 어떻게 다룰지 결정해야 함.

### G6.4 권고 회피안 선택

**선택**: 후보 b (Update override + PrevStateLocalTime per-state tracking).

**근거**:
- StateMachine의 자연스러운 시간 모델 (state-local time) 활용.
- 전제 1 호환 — 단일 활성 sequence 가정 그대로.
- 변경 범위 적당 (한 파일 = `UAnimStateMachineInstance.h/cpp`).
- 후보 a 대비 의미 명확. 후보 c 대비 단순.

**전제 1과의 호환성 재검증**:
- 전제 1: blend 중 From, 완료 시 To flip.
- 후보 b: ActiveStateIndex (= From during blend, To after flip) 기반 query → 자연 호환. PrevStateLocalTime은 ActiveStateIndex가 바뀌는 시점에만 reset → blend 중에는 reset 없음 → From 의 state-local time 정상 누적. 호환 ✓.

---

## 8. G7 — D7 정책과의 호환

### G7.1 D7 dispatch 정책 재인용

[animnotify_integration_design.md:21](animnotify_integration_design.md) (D7):
> Reverse 재생 / `SetEvaluationTime` seek 후 발화 정확성은 본 작업 범위 외. forward 정상 재생만 검증.

D7은 dispatch 정책이 아니라 **scope 제한 정책** (reverse/seek 정확성 미보장). dispatch path 자체는 D2 부분 무효화 정책 (Lua 우선 / C++ fallback) 이 다룸 — 본 진단은 그 dispatch path를 silent 영역에서 활성화 시키는 작업.

`TriggeredNotifiesThisFrame`의 역할 ([AnimInstance.h:108](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:108)): `TArray<FName>` — Lua 측이 매 frame query하여 dispatch 정책 적용 가능 (D3 §3.3 b4 옵션 영역).

### G7.2 override 반환값이 D7 dispatch path에 어떻게 흘러가는지

path 추적 (옵션 1 + 후보 b 채택 시):
```
SkelComp::TickComponent
  → UAnimStateMachineInstance::Update (override)
       → GetActiveNotifies (override) → 활성 state's sequence notifies
       → IsTriggeredBetween(PrevStateLocalTime, CurrStateTime, StateLength) 평가
       → TriggeredNotifiesThisFrame.push_back(name)
       → LocalTriggered.push_back(&notify)
       → DispatchTriggeredNotifies(LocalTriggered)
           → switch on Notify.Type → Sound: PlayEffect | CameraShake: StartCameraShake
  → AnimInstance::EvaluateGraph → StateMachine evaluate (pose)
```

sequence 변경 시 동작:
- ActiveStateIndex flip 후 첫 frame: PrevStateLocalTime이 reset되어 To의 notify들이 정상 IsTriggeredBetween 평가.
- 후속 frame: 같은 To 기준으로 일관 평가.

Lua 우선 / C++ fallback 분기 (D2 부분 무효화):
- 본 작업은 `DispatchTriggeredNotifies` 진입까지 도달시키는 작업. dispatch 함수 내부의 Lua 우선 분기는 별도 prompt 영역.
- sequence 변경에 영향받지 않음 — dispatch는 한 단계 아래 layer.

### G7.3 음성 검증

D7 정책이 sequence-agnostic 여부:
- D7 정책은 dispatch 동작 자체에 sequence-aware 분기를 포함하지 않음 — sequence 누구든 동일하게 trigger 후 dispatch.
- grep 결과 `DispatchTriggeredNotifies` 내부에 sequence별 분기 0건. type별 (Sound/CameraShake) 분기만 존재.
- ✓ sequence-agnostic.

---

## 9. 종합 판정

§1 양식 기준:

```
- [확정] StateMachine 활성 state query 메커니즘:
  UAnimStateMachineInstance::GetActiveNotifies override
    → AnimGraphPtr->GetRoot()
    → dynamic_cast<FAnimGraphNode_StateMachine*>
    → States[GetActiveStateIndex()].Sub  (getter 신설 필요)
    → dynamic_cast<FAnimGraphNode_SequencePlayer*> 또는 nested StateMachine 재귀
    → Sequence->GetNotifies()
  근거: G1.1~G1.4 (private 멤버 + 전제 1 자연 호환), G2.4 (Lua build path가 SequencePlayer 또는 nested StateMachine만 생성)

- [확정] AnimGraph 활성 sequence query 메커니즘:
  UAnimGraphInstance::GetActiveNotifies override
    → AnimGraphPtr->GetRoot()
    → dynamic_cast<FAnimGraphNode_SequencePlayer*> (단순 케이스)
    → Sequence->GetNotifies()
  Blend2/BlendN/nested StateMachine 처리는 장래 대비 (G3.3, G3.4)
  근거: G3.1~G3.4 (현재 production 사용 없음 — 단순 root만 지원)

- [확정] 시그니처 유지 가능 여부: Y
  단일 포인터 반환 시그니처 유지. nullptr 반환 안전 (Update의 if (Notifies) 가드).
  단 GetEffectivePlayLength 동시 override 필요 (G5.3.1 전제 충돌).
  근거: G5.1~G5.3

- [확정] PrevTime mismatch(G6.3) 처리 방안: 후보 b — UAnimStateMachineInstance가 Update를 override하고 state-local time + PrevStateLocalTime 자체 멤버로 notify firing.
  근거: G6.2, G6.3 추적 + 후보 a/b/c 비교

- [전제 충돌] GetEffectivePlayLength도 동시 override 필요. D3 결론 부정 안 됨.
  근거: G5.3.1

- [확인 불가] 없음

- [구현 prompt로 위임]:
  ① FAnimGraphNode_StateMachine 의 getter 신설 (GetActiveStateIndex/GetStateLocalTime 등)
  ② UAnimGraphInstance 의 Blend 노드 처리 정책 (장래)
  ③ Update override 안 LastDeltaTime/bPaused/TriggeredNotifiesThisFrame.clear 등 base 책임 재현
  ④ Frame 지연 문제 — Update가 Evaluate 전 실행이라 1-frame 지연 가능 (정밀화 시 옵션 2 영역으로 확장)
  ⑤ SetEvaluationTime을 StateMachine에서 어떻게 해석할지 (state-local time을 set? 또는 무시?)
  ⑥ UAnimGraphInstance::GetEffectivePlayLength 의 반환 정책 (root가 SequencePlayer면 그 sequence length, Blend이면 ?)
```

---

## 10. 확인 불가 / scope 외 항목

### 본 D4에서 구현 단계로 위임한 항목
- FAnimGraphNode_StateMachine 의 정확한 getter 시그니처 — friend 선언 회피 vs public getter 선택은 코딩 스타일 결정.
- `Update` override 시 base 의 어떤 책임을 재현할지 (LastDeltaTime, bPaused, TriggeredNotifiesThisFrame.clear) — sketch 수준만 제시, 정밀 구현은 prompt에서.
- 1-frame 지연 문제의 정밀화 — Evaluate 안에서 notify 발화 책임을 분리하는 옵션 2 영역 침범 가능 → 구현 prompt에서 결정.

### 향후 별도 prompt 영역
- **전제 1 해제 시 dual-active 시그니처**: blend ratio 기반 동시 활성 — 시그니처가 `TArray<...>` 또는 callback 형태로 변경 필요. 본 D4 scope 외.
- **AnimGraph mode의 production 사용 시작 시 G3 정밀화**: Blend2/BlendN 활성 child 선택 정책, nested 그래프 traversal. 본 D4 scope 외.
- **옵션 2 (dispatch path를 graph 노드 내부로 이동)**: 본 D4의 1-frame 지연 + StateMachine time 모델 정합성을 더 깊게 풀려면 옵션 2가 자연 — 그러나 본 D4의 옵션 1 scope 외. 별도 prompt 필요.

### 본 진단으로 답이 안 나오는 항목
- 없음. G1~G7 모두 매핑 완료. 본 D4의 모든 결론은 view+grep으로 입증됨.
