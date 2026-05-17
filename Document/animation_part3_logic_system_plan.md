# Animation Logic System (파트 3) 구현 계획서

## Context

파트 2(`Document/animation_part2_runtime_core_plan.md`)에서 `UAnimInstance` / `AnimGraph` / `FAnimGraphNode_Base` 인터페이스가 확정되어, 단일 시퀀스 재생까지 평가 트리 위에서 동작하는 골격이 잡혔다. 본 계획서는 그 위에 (a) **State Machine 노드** — "어느 시퀀스를 낼지 선택", (b) **Blend 노드** — "선택된 포즈들을 합성", (c) **Notify dispatch** — "시퀀스의 NotifyName을 외부 리스너에 전달"을 신규로 얹는다. 모두 AnimGraph의 노드로서 설계되어 파트 2가 잡아둔 확장 지점(`FAnimGraphNode_Base::Evaluate`, `AnimGraph::SetRoot`)에 그대로 끼워진다.

## 0. 파트 2 의존 전제

### 0.1 파트 2에서 그대로 가져다 쓰는 인터페이스

| 인터페이스 | 위치 (파트 2 계획) | 본 파트에서의 용도 |
|---|---|---|
| `UAnimInstance::Update(dt)` / `EvaluateGraph()` | 3.1 | tick 진입점 — 본 파트가 별도 진입점 추가하지 않음 |
| `UAnimInstance::CurrentTime` / `PreviousTime` | 3.1 | Notify 트리거 판정에 prev/curr 시간 공급, Transition 조건 중 `TimeElapsed` 평가 |
| `UAnimInstance::OutputLocalPose` | 3.1 | 최종 포즈 출력 자리 (StateMachine 노드의 결과가 여기에 들어감) |
| `AnimGraph::SetRoot` | 3.3 | Root를 `FAnimGraphNode_StateMachine`으로 교체 |
| `FAnimGraphNode_Base::Evaluate(Ctx, OutLocalPose)` | 3.3 | 신규 노드(StateMachine / BlendN / Blend2)가 동일 시그니처로 파생 |
| `FAnimGraphNode_SequencePlayer` | 3.3 | State 내부의 기본 자식 노드로 재사용 |
| `UAnimInstance::TrackToBoneIndex` | 3.1 | State 내부 시퀀스마다 본 인덱스 resolve 필요 — 단, 다중 시퀀스 환경에서 운용 방식은 OQ5 |

### 0.2 파트 2에 추가로 요구하는 인터페이스

본 파트에서 필요한데 파트 2 계획서엔 없는 항목. 파트 2에 반영하거나, 안 되면 본 파트 단계에서 패치.

1. **`FAnimEvalContext` 확장** — 다중 시퀀스/다중 노드를 동시에 평가하려면 단일 `DataModel` 필드로는 부족. 노드가 자기 시퀀스 ref를 직접 멤버로 들고 평가하는 형태가 자연스럽다(즉 `FAnimGraphNode_SequencePlayer`에 `UAnimSequence*` 멤버 추가). Ctx에는 `Skeleton` + 공용 데이터(스크래치 풀, dt)만 남기는 형태로의 정리 필요.
2. **`FAnimEvalContext`에 `DeltaTime` 필드** — StateMachine의 `TransitionElapsed`, `TimeInActiveState`를 누적하려면 노드 평가 시 dt가 필요. 대안: `Owner->CurrentTime - Owner->PreviousTime`로 산출(OQ로 보류).
3. **스크래치 포즈 버퍼 풀** — Blend / Transition 진행 중에는 자식 노드 평가 결과를 임시 보관할 `TArray<FMatrix>` 2~N개가 필요. `UAnimInstance` 측 풀에서 빌려 쓰는 형태를 권장.
4. **`UAnimInstance::OnNotify` delegate** — 매핑 doc 3절이 `TDelegate<const FAnimNotifyEvent&>` 사용을 권고. 본 파트 dispatch가 이 delegate를 broadcast하는 형태.
5. **인스턴스 변수 저장소** — Transition 조건이 `BoolVariable` 등을 평가하려면 `TMap<FName, bool> BoolVariables` 같은 자리가 `UAnimInstance`에 필요(또는 파생 클래스에).
6. **`FAnimGraphNode_SequencePlayer`에 `Reset()`** — State 진입 시 sub-graph 시간 초기화 정책에 필요(`bResetTimeOnEnter`).

위 6개를 본 계획서는 **"파트 2에 요구하는 인터페이스"**로 명시한다. 실제 반영은 파트 2 plan의 S8 또는 별도 보강 단계.

## 1. 클래스별 책임 정의

### 1.1 StateMachine 노드 (`FAnimGraphNode_StateMachine`)

**책임**: "지금 어느 포즈를 낼지 선택".

| 항목 | 내용 |
|---|---|
| 입력 | `FAnimEvalContext`(Skeleton/dt/스크래치 풀), 자기 보유 `States[]` + `Transitions[]` + 활성 상태 인덱스, `Owner = UAnimInstance*`(변수 read) |
| 출력 | `TArray<FMatrix>& OutLocalPose` (활성 State의 평가 결과, 또는 transition 진행 중이면 from/to 합성 결과) |
| 내부 상태 | `ActiveStateIndex`, `TransitionInProgress`, `TransitionElapsed`, `TimeInActiveState` |
| 하는 일 | Transition 조건 평가 → 트리거 시 진입 → 진행 중이면 from/to 양쪽 평가 후 alpha 합성 → 완료 시 active 갱신 |
| 안 하는 일 | 본별 합성 산술(=Blend의 책임), 시퀀스 키 샘플링(=SequencePlayer의 책임) |

근거 — "선택"과 "산술"을 분리하면 transition blend 로직이 그대로 BlendN에 재사용된다.

### 1.2 Blend 노드 (`FAnimGraphNode_Blend2`, `FAnimGraphNode_BlendN`)

**책임**: "선택된 포즈들을 합성".

| 항목 | 내용 |
|---|---|
| 입력 | 자식 노드 N개, 가중치 N개 (Blend2는 N=2 + Alpha 단일 스칼라) |
| 출력 | `TArray<FMatrix>& OutLocalPose` (본별 weighted blend 결과) |
| 하는 일 | 각 자식 평가 → 스크래치 포즈로 받음 → weight 정규화 → 본별 pos lerp / rot slerp / scale lerp(TRS 정본 시) 또는 행렬 element-wise weighted sum(LocalMatrix 정본 시) |
| 안 하는 일 | State 전환 / 시퀀스 키 샘플링 |

근거 — StateMachine의 transition 합성과 동일 산술이라 Blend 노드로 분리하면 재사용 가능. 단 transition마다 동적으로 노드를 생성하지 않고 인라인 합성하는 안도 있음(OQ1).

### 1.3 책임 분리 요약

```
"선택" (StateMachine 노드)      ↓ 어떤 자식의 결과를 / 어떤 가중치로
"산술" (Blend 노드 / 합성 함수) ↓ 본별 합성
"샘플링" (SequencePlayer 노드, 파트 2) ↓ 시퀀스 키 → LocalPose
```

## 2. 소유/참조 관계도

```
UAnimInstance (파트 2)
 ├─ holds ─> BoolVariables / FloatVariables (TMap<FName,...>)        ※ 파트 2에 요청
 ├─ holds ─> ScratchPosePool (TArray<TArray<FMatrix>>)               ※ 파트 2에 요청
 ├─ holds ─> TDelegate<const FAnimNotifyEvent&> OnNotify             ※ 파트 2에 요청
 │
 └─ owns ─> AnimGraph (파트 2)
        └─ owns ─> Root: FAnimGraphNode_StateMachine                 ← 본 파트 신규 (StateMachine을 root에 둠)
              │
              ├─ holds ─> TArray<FAnimState> States
              │            └─ each: FAnimState
              │                  ├─ Name (FName)
              │                  ├─ bResetTimeOnEnter (bool)
              │                  └─ owns ─> Pose: unique_ptr<FAnimGraphNode_Base>
              │                              └─ 보통 FAnimGraphNode_SequencePlayer  (파트 2 노드)
              │                                    └─ ref ─> UAnimSequence*        (자산 시스템 소유)
              │
              ├─ holds ─> TArray<FAnimTransition> Transitions
              │            └─ each: From / To 인덱스 + BlendDuration + Conditions[]
              │
              └─ ref ─> UAnimInstance* Owner  (변수/시간 조회용; not owned)

[Lua 바인딩 — 매핑 doc 3절 파트3]
   FLuaScriptSubsystem
      └─ RegisterAnimInstanceBinding / RegisterAnimSequenceBinding
            └─ Lua 측에서: AnimInstance:SetBool("Attack", true),
                         AnimInstance:OnNotify:Add(handler) 등
```

State가 `UAnimSequence*`를 어떻게 참조하는지: **각 `FAnimState`가 자기 sub-graph(`FAnimGraphNode_SequencePlayer`)를 owned로 들고, 그 노드가 `UAnimSequence*`를 ref로 가진다.** 시퀀스 자체는 자산 시스템 소유(매핑 doc 3절).

## 3. 헤더 인터페이스 골격

### 3.1 State / Transition 데이터

```cpp
// Transition 조건 — 평가 단위 1개. AND 조합은 FAnimTransition::Conditions로 묶는다.
struct FAnimTransitionCondition
{
    enum class EKind { TimeElapsed, BoolVariable, OnNotify, Custom };

    EKind Kind = EKind::TimeElapsed;

    float TimeThreshold = 0.0f;     // EKind::TimeElapsed     (TimeInActiveState >= 이 값)
    FName VariableName;             // EKind::BoolVariable    (Owner->BoolVariables[Name] == !bInvert)
    FName NotifyName;               // EKind::OnNotify        (직전 EvaluateGraph에서 트리거된 NotifyName과 일치)
    bool  bInvert = false;          // 모든 EKind에 공통 (negate)
    // EKind::Custom 평가 후크는 OQ4 — 본 계획서는 자리만 둔다.
};

// State — sub-graph 보유
struct FAnimState
{
    FName                                  Name;
    std::unique_ptr<FAnimGraphNode_Base>   Pose;                  // 보통 SequencePlayer 한 개
    bool                                   bResetTimeOnEnter = true;
};

// Transition — From -> To + 조건 AND + 진행 길이
struct FAnimTransition
{
    int32                            FromStateIndex = -1;
    int32                            ToStateIndex   = -1;
    float                            BlendDuration  = 0.2f;       // seconds
    TArray<FAnimTransitionCondition> Conditions;                  // AND
};
```

### 3.2 StateMachine 노드

```cpp
struct FAnimGraphNode_StateMachine : FAnimGraphNode_Base
{
    void Evaluate(const FAnimEvalContext& Ctx,
                  TArray<FMatrix>&        OutLocalPose) override;

    // 정적 구성 (구성 후 변경 가정 X — OQ1 참고)
    TArray<FAnimState>      States;
    TArray<FAnimTransition> Transitions;

    // 런타임 상태
    int32 ActiveStateIndex       = 0;
    int32 TransitionInProgress   = -1;        // Transitions 인덱스
    float TransitionElapsed      = 0.0f;      // [0, BlendDuration]
    float TimeInActiveState      = 0.0f;      // EKind::TimeElapsed 평가용

    // 직전 EvaluateGraph에서 트리거된 Notify 모음 (EKind::OnNotify 평가용)
    // UAnimInstance가 dispatch 직후 채워주거나, StateMachine이 직접 평가하거나 — OQ 처리는 6절 의존성.
    TArray<FName> RecentlyTriggeredNotifies;

    // Owner 변수 read용
    const UAnimInstance* Owner = nullptr;
};
```

### 3.3 Blend 노드 (2-way / N-way)

```cpp
// 2-way (StateMachine의 transition 합성에서 재사용 후보 — OQ1)
struct FAnimGraphNode_Blend2 : FAnimGraphNode_Base
{
    void Evaluate(const FAnimEvalContext& Ctx,
                  TArray<FMatrix>&        OutLocalPose) override;

    std::unique_ptr<FAnimGraphNode_Base> A;
    std::unique_ptr<FAnimGraphNode_Base> B;
    float Alpha = 0.0f;                 // 0 -> A, 1 -> B
};

// N-way (BlendSpace 류 / 멀티 입력 합성용)
struct FAnimGraphNode_BlendN : FAnimGraphNode_Base
{
    void Evaluate(const FAnimEvalContext& Ctx,
                  TArray<FMatrix>&        OutLocalPose) override;

    TArray<std::unique_ptr<FAnimGraphNode_Base>> Children;
    TArray<float>                                 Weights;     // size == Children.size()
};
```

### 3.4 `UAnimInstance`에 신설되는 멤버 (파트 2에 요청, 본 파트가 소비)

```cpp
// 변수 저장소 — Lua / 게임 코드 set, Transition 조건이 read
TMap<FName, bool>   BoolVariables;
TMap<FName, float>  FloatVariables;

// Notify dispatch
TDelegate<const FAnimNotifyEvent&> OnNotify;

// 스크래치 포즈 풀
TArray<FMatrix> ScratchPoseA;
TArray<FMatrix> ScratchPoseB;
// (N>2 합성 시) TArray<TArray<FMatrix>> ScratchPosesExtra;

// 활성 시퀀스 노출 (Notify 소스 식별)
virtual const UAnimSequence* GetCurrentSequence() const = 0;   // SingleNode 또는 StateMachine 활성 State
```

## 4. 핵심 기능별 의사코드

### 4.1 Animation Blending (N개 포즈 weighted blend)

```
fn FAnimGraphNode_BlendN::Evaluate(Ctx, OutLocalPose):
    if Children.empty() or Weights.size() != Children.size():
        FillBindPose(OutLocalPose, Ctx.Skeleton); return

    # 1) 가중치 정규화
    Sum = sum(Weights)
    if Sum <= eps:
        Children[0].Evaluate(Ctx, OutLocalPose); return
    NormW = [w / Sum for w in Weights]

    # 2) 각 자식 평가 -> 임시 포즈
    SubPoses = []
    for c in Children:
        P = Ctx.AllocScratchPose()    # 풀에서 빌리고 finally에서 반납
        c.Evaluate(Ctx, P)
        SubPoses.append(P)

    # 3) 본별 합성 — TRS 정본 시 pos/scale=lerp, rot=quaternion 평균(N=2면 slerp, N>2는 OQ3 한계)
    Bones = Ctx.Skeleton.GetBones()
    OutLocalPose.resize(Bones.size())
    for i in 0..Bones.size():
        # ===== Option A (현 LocalMatrix 정본 — 본 단위 element-wise weighted sum) =====
        Acc = ZeroMatrix
        for (P, w) in zip(SubPoses, NormW):
            Acc = Acc + P[i] * w
        OutLocalPose[i] = Acc

        # ===== Option B (TRS 정본 — 파트 2 OQ1 / 본 파트 OQ3 가 정리된 후) =====
        # (T_acc, S_acc) = weighted lerp on positions/scales
        # Q_acc          = sequential slerp (N==2 정확, N>2 근사) or Markley 일반해
        # OutLocalPose[i] = ComposeTRS(T_acc, Q_acc, S_acc)
```

> 본 트랙 이름 매칭: 본 인덱스는 `USkeleton` 기준으로 이미 통일됨 (파트 2 `RebuildTrackToBoneIndex`가 트랙 FName → USkeleton 본 인덱스로 resolve). 합성 단계에서 이름 매칭은 불필요. 시퀀스가 USkeleton에 없는 본 트랙을 가지면 그 트랙은 -1로 인덱스되어 합성에서 제외.

### 4.2 State Machine 평가

```
fn FAnimGraphNode_StateMachine::Evaluate(Ctx, OutLocalPose):
    dt = Ctx.DeltaTime    # 파트 2에 요청한 필드

    # 1) Transition 진행 중이 아니라면, 활성 State에서 출발하는 Transition 조건 평가
    if TransitionInProgress < 0:
        for tIdx, t in enumerate(Transitions):
            if t.FromStateIndex != ActiveStateIndex: continue
            if EvaluateAllConditions(t.Conditions, Owner, TimeInActiveState, RecentlyTriggeredNotifies):
                TransitionInProgress = tIdx
                TransitionElapsed    = 0
                if States[t.ToStateIndex].bResetTimeOnEnter:
                    States[t.ToStateIndex].Pose.Reset()       # SequencePlayer::Reset 호출
                break

    # 2) 평가
    if TransitionInProgress >= 0:
        T = Transitions[TransitionInProgress]
        Duration = max(T.BlendDuration, eps)
        Alpha    = clamp(TransitionElapsed / Duration, 0, 1)

        # from/to 양쪽 평가 후 합성 (4.3 참고)
        PFrom = Ctx.AllocScratchPose()
        PTo   = Ctx.AllocScratchPose()
        States[T.FromStateIndex].Pose.Evaluate(Ctx, PFrom)
        States[T.ToStateIndex  ].Pose.Evaluate(Ctx, PTo)
        BlendTwoPoses(PFrom, PTo, Alpha, OutLocalPose, Ctx.Skeleton)

        TransitionElapsed += dt
        if Alpha >= 1.0:
            ActiveStateIndex     = T.ToStateIndex
            TransitionInProgress = -1
            TimeInActiveState    = 0.0
    else:
        States[ActiveStateIndex].Pose.Evaluate(Ctx, OutLocalPose)
        TimeInActiveState += dt


fn EvaluateAllConditions(Conds, Owner, TimeInActive, RecentNotifies):
    for c in Conds:
        ok = false
        match c.Kind:
            TimeElapsed:  ok = (TimeInActive >= c.TimeThreshold)
            BoolVariable: ok = (Owner.BoolVariables.get(c.VariableName, false))
            OnNotify:     ok = (c.NotifyName in RecentNotifies)
            Custom:       ok = EvaluateCustom(c, Owner)        # OQ4 — 자리만
        if c.bInvert: ok = not ok
        if not ok: return false
    return true
```

### 4.3 Transition 진행 중 내부 blend (from/to 두 State 평가 후 alpha 합성)

```
fn BlendTwoPoses(PFrom, PTo, Alpha, OutLocalPose, Skeleton):
    Bones = Skeleton.GetBones()
    OutLocalPose.resize(Bones.size())
    for i in 0..Bones.size():
        # Option A (LocalMatrix 정본):
        OutLocalPose[i] = PFrom[i] * (1 - Alpha) + PTo[i] * Alpha
        # Option B (TRS 정본):
        # (T,Q,S) decompose each, lerp T/S, slerp Q, compose
```

OQ1에 따라 위 함수는 (a) `FAnimGraphNode_Blend2`의 인라인 코드를 호출하는 형태로 통일하거나, (b) StateMachine 안에 직접 인라인하는 두 안. 본 계획의 잠정 권고는 (a) — 코드 재사용.

### 4.4 State별 animation 연결 (State 진입 시 시퀀스 time 초기화 정책)

```
fn FAnimGraphNode_SequencePlayer::Reset():
    LocalTime = 0.0
    # 이 노드가 자기 LocalTime을 보유하지 않고 UAnimInstance::CurrentTime을 쓰는 구조라면,
    # StateMachine 측에서 Owner->ResetTime()을 호출해야 함 — 그러나 그러면 다른 노드의
    # 시간까지 리셋되어 부적절. 따라서 SequencePlayer가 노드별 LocalTime을 갖는 안이 안전.
    # (파트 2 OQ — 시간 운용을 instance 전역으로 둘지 노드 로컬로 둘지)
```

**정책**: `FAnimState::bResetTimeOnEnter == true`(기본)인 경우 To State의 sub-graph time을 0으로 초기화. false인 경우 이전 진입 시 멈춘 위치에서 이어 재생.

**노드 로컬 시간 vs 인스턴스 전역 시간**: 파트 2의 `UAnimInstance::CurrentTime`은 인스턴스 전역이라 State 별 reset이 충돌한다. 본 파트는 **SequencePlayer 노드가 노드 로컬 `LocalTime`을 가지는 방향**을 권고. 파트 2에 보강 요청(0.2 항목 6번에 포함).

### 4.5 Notify dispatch

```
fn UAnimInstance::DispatchNotifies():
    Seq = GetCurrentSequence()           # 활성 State의 시퀀스 (파트 2 SingleNode에선 CurrentSequence)
    if not Seq: return
    Length = Seq.GetPlayLength()

    TriggeredNames.clear()
    for n in Seq.GetNotifies():
        if n.IsTriggeredBetween(PreviousTime, CurrentTime, Length):
            OnNotify.BroadCast(n)         # TDelegate<const FAnimNotifyEvent&>
            TriggeredNames.push_back(n.NotifyName)

    # StateMachine에 OnNotify Transition 조건 평가를 위해 노출
    if AnimGraph and AnimGraph.Root is FAnimGraphNode_StateMachine:
        StateMachine.RecentlyTriggeredNotifies = TriggeredNames
```

**호출 위치**: `UAnimInstance::EvaluateGraph()` 종료 직후. 이렇게 두면 다음 tick의 StateMachine 평가가 `RecentlyTriggeredNotifies`를 참조할 수 있다 — 단 같은 tick 내에서 즉시 transition 트리거하려면 EvaluateGraph 진입 시점에 prev/curr 기준으로 미리 채우는 안도 가능. **본 계획의 잠정 권고**: 다음 tick에서 평가(1프레임 지연 허용) — 단순/안전.

> `FAnimNotifyEvent::IsTriggeredBetween`은 이미 구현(`AnimNotify.h:35-46`)되어 있고 루프 감김(prev>curr)도 처리. 본 파트는 호출 + dispatch만.

## 5. 데이터 흐름 — 제어 vs 포즈 분리

```
┌──────────────────────── 제어 데이터 경로 ────────────────────────┐
│                                                                  │
│  Lua / 게임 코드:                                                │
│     anim:SetBool("bShouldAttack", true)                          │
│                                                                  │
│  UAnimInstance.BoolVariables / FloatVariables                    │
│  UAnimInstance.CurrentTime / PreviousTime                        │
│  StateMachine.RecentlyTriggeredNotifies (1프레임 지연)           │
│       │                                                          │
│       ▼                                                          │
│  FAnimTransitionCondition::EvaluateAllConditions                 │
│       │                                                          │
│       ▼                                                          │
│  Transition 트리거 결정 + Alpha 산출                             │
│       │                                                          │
└───────┼──────────────────────────────────────────────────────────┘
        │  (Transition 결정이 포즈 합성 분기를 결정 ── 교차점)
        ▼
┌──────────────────────── 포즈 데이터 경로 ────────────────────────┐
│                                                                  │
│  UAnimSequence (각 State 소유)                                   │
│       │ (time ← SequencePlayer LocalTime)                        │
│       ▼                                                          │
│  FAnimGraphNode_SequencePlayer.Evaluate → LocalPose              │
│       │                                                          │
│       ▼                                                          │
│  FAnimGraphNode_StateMachine:                                    │
│    - 진행 없음 → 활성 State의 Pose 그대로                        │
│    - 진행 중    → from/to 두 Pose 평가 → BlendTwoPoses(α)        │
│       │                                                          │
│       ▼                                                          │
│  (선택) FAnimGraphNode_BlendN  (BlendSpace 등)                   │
│       │                                                          │
│       ▼                                                          │
│  UAnimInstance.OutputLocalPose                                   │
│       │                                                          │
│       ▼                                                          │
│  (컴포넌트) FK → Skinning  (파트 2의 4.4 / 4.5)                  │
└──────────────────────────────────────────────────────────────────┘

[교차 지점]
  - StateMachine 노드 안. 제어 경로의 산출(Transition / Alpha)이
    포즈 경로의 합성 방식(단일 State 평가 vs 두 State 합성)을 결정한다.
  - Notify dispatch는 포즈 경로의 결과(현재 시퀀스의 NotifyName)를
    제어 경로(EKind::OnNotify Transition 조건)로 되돌리는 피드백.
```

## 6. 구현 순서와 의존성

| 단계 | 작업 | 선행 | 검증 마일스톤 |
|---|---|---|---|
| T0 | 파트 2 보강: `FAnimEvalContext`에 `DeltaTime` + 스크래치 풀, `UAnimInstance`에 `BoolVariables`/`FloatVariables`/`OnNotify`/`GetCurrentSequence`, `SequencePlayer::Reset` + `LocalTime` | 파트 2 S1~S8 | 컴파일 통과 |
| T1 | `FAnimGraphNode_Blend2` (Option A 산술) | T0 | 두 시퀀스 Alpha=0/0.5/1 → 시각적 합성 정상 |
| T2 | `FAnimGraphNode_BlendN` (Option A 산술, weight 정규화) | T1 | N=2 결과가 T1과 수치 일치 |
| T3 | `FAnimState` / `FAnimTransitionCondition` / `FAnimTransition` 구조 | T0 | 컴파일 통과 |
| T4 | `FAnimGraphNode_StateMachine::Evaluate` — Transition 미트리거 경로 (활성 State만 평가) | T3 | 단일 State 시퀀스 viewer에서 재생 정상 |
| T5 | Transition 조건 평가 (`TimeElapsed` + `BoolVariable`) + 진행 중 from/to 합성 (Blend2 재사용 또는 인라인 — OQ1) | T1, T4 | 조건 충족 시 트리거 → BlendDuration 동안 합성 → To 확정 |
| T6 | `bResetTimeOnEnter` 지원 (SequencePlayer LocalTime reset) | T4 | true/false 모두 시각적으로 일치하는 동작 |
| T7 | Notify dispatch — `OnNotify` delegate + `DispatchNotifies()` 호출 패스 | 파트 2 S9 | `IsTriggeredBetween` 통과 시 broadcast 정확히 1회. 루프 감김 케이스 포함 |
| T8 | `EKind::OnNotify` Transition 조건 — `RecentlyTriggeredNotifies` 연결 | T5, T7 | NotifyName으로 transition 트리거 동작 |
| T9 | Lua 바인딩 — `LuaBindings.h`에 `RegisterAnimInstanceBinding`/`RegisterAnimSequenceBinding` 추가 (매핑 doc 3절) | T0~T8 | Lua에서 `anim:SetBool`/`anim:OnNotify:Add` 호출 가능 |

### Blend vs StateMachine 선후 — 근거

**Blend 선행**. 근거:
- StateMachine의 transition 진행 중 from/to 합성은 본질적으로 2-way blend와 동일 산술이다.
- Blend2를 먼저 만들어두면 StateMachine이 (a) 그것을 재사용(OQ1-A)하거나 (b) 동일 로직을 인라인(OQ1-B)할 때 검증된 산술을 그대로 쓸 수 있다.
- 역방향(StateMachine 먼저)이면 transition blend가 검증 안 된 임시 코드로 채워지고, 나중에 Blend 노드 도입 시 중복/모순 위험.

따라서 T1(Blend2) → T2(BlendN) → T4~T5(StateMachine 본체) 순.

## 7. 미해결 결정 사항 (Open Questions)

### OQ1. Transition 내부 blend를 `FAnimGraphNode_Blend2` 노드로 분리할지, StateMachine 안에 인라인할지

- **현황**: 두 합성이 동일 산술이지만, transition은 매 진행 시 from/to가 동적으로 바뀌므로 노드 트리에 transient한 Blend2 인스턴스를 끼우는 형태가 어색할 수 있다.
- **선택지**
  - **A**: Blend2 노드로 분리. transition 시작 시 임시 Blend2를 만들고 A=PFrom 노드 ref, B=PTo 노드 ref, Alpha 갱신. 종료 시 폐기.
  - **B**: StateMachine 내부에 `BlendTwoPoses()` 정적 함수로 인라인. Blend2 노드는 노드 트리 정적 구성에서만 사용.
- **영향 단계**: **T5**.
- **본 계획의 잠정 권고**: **B** (StateMachine 인라인). 근거 — 노드 동적 생성 비용/소유권 관리 부담 회피, 그러나 `BlendTwoPoses`의 산술 구현은 Blend2 노드의 그것과 동일 유틸 함수를 공유.
- **결정 보류 이유**: KraftonEngine의 노드 동적 생성/swap 관습 확인 필요. 본 계획 범위 외.

### OQ2. `FAnimationCurveData` 빈 구조 — curve 기반 transition / blend weight 불가

- **현황**: `AnimationTypes.h:104-112`에 자리만 있고 직렬화 no-op. 매핑 doc 5절도 명시.
- **본 파트에서의 영향**
  - `BlendDuration`이 선형 alpha만 지원 (ease-in/out 곡선 미지원).
  - BlendSpace 류(파라미터 → 가중치) 도입 시 곡선 미지원.
- **결정 보류 이유**: 자산 측 구조 신규 작성이 필요해 본 파트 3 범위 밖. 우선 선형 alpha로 진행, curve는 파트 1/3 후속.

### OQ3. Blend 본별 합성 — element-wise lerp vs TRS slerp (매핑 doc 5절 사슬)

- **현황**: 파트 2 OQ1과 동일 사슬. 파트 2의 SequencePlayer가 어느 정본을 쓰느냐에 따라 Blend도 따라간다.
- **N>2 회전 합성**: TRS 정본일 때 N=2는 slerp로 정확하나 N>2는 quaternion weighted average 일반해가 필요(Markley 등). 본 계획서는 N>2 회전을 **순차 slerp 근사**로 둘 것을 잠정 권고.
- **영향 단계**: **T1**(Blend2), **T2**(BlendN), **T5**(StateMachine transition blend).
- **결정 보류 이유**: 파트 2 OQ1과 동시에 결정되어야 일관됨.

### OQ4. Transition `EKind::Custom` 평가 메커니즘

- **선택지**: (a) Lua callback 평가 — `sol::function`을 condition에 보관, (b) native function pointer + register 패턴, (c) 간단한 DSL 표현식.
- **영향 단계**: **T9**.
- **본 계획의 잠정 권고**: 본 파트 3에서는 자리만 잡고 미구현. Lua 바인딩(T9) 작업 시 (a) 형태가 가장 자연스러움(`LuaScriptSubsystem`이 sol2 기반 — 매핑 doc 3절).
- **결정 보류 이유**: Lua 측 API 범위(파트 3가 Lua DSL로 state graph 정의를 지원할지 — 매핑 doc 의문점 4)와 함께 결정 필요.

### OQ5. `UAnimInstance::TrackToBoneIndex`의 다중 시퀀스 대응 (파트 2에 영향)

- **현황**: 파트 2 plan 3.1에서 `TrackToBoneIndex`가 인스턴스 단일 멤버. SingleNode에서는 동작하나, StateMachine은 State마다 다른 시퀀스를 동시에 들고 있을 수 있어 단일 캐시로 부족.
- **선택지**
  - **A**: `FAnimGraphNode_SequencePlayer`가 자체 `TrackToBoneIndex`를 멤버로 보유.
  - **B**: `UAnimInstance::TrackToBoneIndexBySequence: TMap<UAnimSequence*, TArray<int32>>`.
- **영향 단계**: 파트 2 plan S5 + 본 파트 T0 보강.
- **본 계획의 잠정 권고**: **A** — 노드 단위 자체 보유. 시퀀스가 노드에 ref로 들어가는 흐름과 일관.

### OQ6. Notify 시간 도메인 / 동시 시퀀스 발생

- **현황**: Transition 진행 중 from/to 두 시퀀스가 동시에 평가됨. 양쪽 모두에서 Notify를 발생시킬지(자연스러운 의미), to-쪽만 발생시킬지(폴리시) 미정.
- **영향 단계**: **T7**.
- **본 계획의 잠정 권고**: **양쪽 모두 발생** — 시퀀스가 개별 시간축을 가지므로 각자의 IsTriggeredBetween이 trigger되면 broadcast. dispatch 키(`NotifyName` + 시퀀스 식별자)는 필요시 페이로드에 추가.
- **결정 보류 이유**: 게임 측 리스너의 기대치(중복 발생 허용 여부) 확인 필요.

---

## 본 계획서 한계 / 검증 안 된 가정

- **[추정]** AnimGraph 노드의 동적 add/remove/swap이 가능한지는 파트 2가 노드 트리 구조까지 만들지 않음. 본 계획서는 StateMachine을 root에 두고 자식 노드들을 **정적으로** 사전 구성한다고 전제(에디터/스크립트가 graph를 만들고 런타임에서 평가만).
- **[추정]** `TDelegate<const FAnimNotifyEvent&>::BroadCast`의 정확한 시그니처와 member function binding 방법은 `Runtime/Delegate.h`(매핑 doc 3절에서만 언급)를 직접 확인하지 않았다. T7 구현 시 검증 필요.
- **[추정]** Lua 바인딩 형태(state graph DSL vs C++ 정의 + Lua 변수 set만)는 OQ4와 매핑 doc 의문점 4가 가리키는 모호함. 본 계획서는 "C++ 정의 + Lua 변수/이벤트 통로" 노선을 잠정 권고하나 단정 아님.
- **[추정]** `FAnimEvalContext`에 `DeltaTime` 필드를 추가한다고 했으나, 대안으로 `Owner->CurrentTime - Owner->PreviousTime`로 산출 가능. 두 안의 절충은 T0에서 결정.
- **[추정]** N>2 회전 합성을 순차 slerp 근사로 둔 것은 일반해(Markley 등) 미구현 시의 차선. 정확도 요구 강할 시 BlendN을 사실상 Blend2 트리로 펼치는 형태가 더 안전할 수 있다.
- **검증 안 된 가정**: `LuaBindings.h`의 `RegisterXxxBinding` 패턴(매핑 doc 3절)이 실제로 본 파트가 필요로 하는 형태(`anim:SetBool`, `anim:OnNotify:Add`)와 직결되는지 — 패턴은 알지만 helper / userdata 구조는 미확인.
- **검증 안 된 가정**: 파트 2 plan에서 권고된 `std::unique_ptr<AnimGraph>` 소유 관습(파트 2 한계 섹션)이 그대로 유지된다고 가정. UObject + Factory 관습으로 바뀌면 본 계획 3절 골격의 `unique_ptr<FAnimGraphNode_Base>` 부분도 동일한 방향으로 변경 필요.
- **[추정]** Notify dispatch 호출 시점(EvaluateGraph 종료 직후)으로 인해 `EKind::OnNotify` transition이 **1프레임 지연**되어 트리거된다. 즉시 트리거가 필요하면 EvaluateGraph 진입 직후에 prev/curr 기준으로 한 번 더 dispatch 패스를 도는 안이 있으나 본 계획서는 단순/안전을 우선해 1프레임 지연을 허용.
