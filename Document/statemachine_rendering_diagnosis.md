# 진단: AnimationStateMachine 모드 — asset 할당 후 rendering 가능 여부

## 1. 결론 한 줄

**현재 코드 상태에서는 `EAnimationMode::AnimationStateMachine` 모드로 화면에 그릴 수 없다.**
핵심 차단 지점: `USkeletalMeshComponent::AnimInstance` 멤버가 `protected`이고 public getter도
friend 선언도 없어서, 외부에서 `UAnimStateMachineInstance::SetStateMachineGraph(...)`를 **호출할 통로가 없다.**
graph 주입 API와 평가/렌더링 사슬은 모두 살아 있지만, 진입 경로 한 곳에서 사슬이 끊긴다.

> 보조 발견: `PlayAnimation` / `SetAnimation`은 `Cast<UAnimSingleNodeInstance>` 분기로만 동작하므로
> StateMachine 인스턴스에 대해서는 묵음 무시된다. 그리고 `SetAnimationMode()`는 모드 변수만 갱신하므로,
> 이미 SingleNode로 만들어진 인스턴스가 있는 상태에서 모드를 바꿔도 StateMachine 인스턴스로 교체되지 않는다.

---

## 2. Rendering 사슬 다이어그램

```
[외부 호출자 / Editor / Lua]
        │
        │  ⛔ 끊김 #1 — AnimInstance는 protected, getter/friend 없음
        ▼  (그래서 아래 메서드를 호출할 통로가 없다)
UAnimStateMachineInstance::SetStateMachineGraph(unique_ptr<FAnimGraphNode_StateMachine>)
        │  ✅ 메서드 자체는 선언·정의 모두 존재
        │  ✅ AnimGraphPtr->SetRoot(...)로 root 등록
        ▼
FAnimGraphNode_StateMachine (States/Transitions public 멤버, SequencePlayer를 Sub로 보유)
        │  ✅ FAnimGraphNode_SequencePlayer::SetSequence(Skeleton, Sequence)로 asset 주입 가능
        ▼
USkeletalMeshComponent::TickComponent
        │  ✅ AnimInstance->Update() → EvaluateGraph() → ApplyEvaluatedPose() (타입 무관 공통 경로)
        ▼
UAnimInstance::EvaluateGraph()                  ← UAnimStateMachineInstance는 override 안 함
        │  ✅ AnimGraphPtr->Evaluate(Ctx, OutputLocalPose)
        ▼
FAnimGraphNode_StateMachine::Evaluate
        │  ✅ States 비면 OutLocalPose.clear() (크래시 아님, 빈 pose)
        │  ✅ States ≥ 1이면 active state의 Sub(SequencePlayer)를 평가해 OutLocalPose 채움
        ▼
UAnimInstance::OutputLocalPose (TArray<FTransform>)
        │  ✅ GetOutputLocalPose()로 컴포넌트가 수령
        ▼
USkinnedMeshComponent::ApplyEvaluatedPose
        │  ✅ FTransform::ToMatrix() → LocalBonePoseMatrices (override 마스크 존중)
        │  ✅ RebuildMeshSpaceBoneMatrices() → MeshSpaceBoneMatrices
        │  ✅ SkinVerticesToReferencePose() → SkinnedVertices
        ▼
RuntimeMeshBuffer.UpdateVertices() → GPU
        │  ✅ 렌더 경로 정상
        ▼
화면 출력
```

추가 잠재 끊김 지점:

- ⛔ 끊김 #2 — `PlayAnimation` / `SetAnimation`은 `Cast<UAnimSingleNodeInstance>` 분기로만 동작.
  StateMachine 인스턴스에 대해서는 아무 일도 하지 않는다 (의도된 무시지만, 외부에서 asset을 넣는
  편의 진입점이 사실상 SingleNode 전용).
- ⛔ 끊김 #3 — `SetAnimationMode()`는 enum 값만 바꾼다. 이미 `AnimInstance`가 만들어진 뒤에는
  StateMachine 인스턴스로 자동 재생성되지 않는다. 따라서 "기본으로 SingleNode 생성 → 나중에 모드 전환"
  순서로 쓰면 영원히 StateMachine으로 못 간다 (헤더 주석에도 명시).

---

## 3. Step별 발견 사실

### Step 1 — StateMachine 인스턴스 생성 경로

**사실 1-1.** `EAnimationMode` enum에 `AnimationStateMachine` 멤버가 실제로 존재한다.
- 근거: [SkeletalMeshComponent.h:13-17](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h)
  ```cpp
  enum class EAnimationMode
  {
      AnimationSingleNode,
      AnimationStateMachine
  };
  ```

**사실 1-2.** `EnsureAnimInstance()`는 모드에 따라 분기해 `UAnimStateMachineInstance`를 실제로 생성한다.
- 근거: [SkeletalMeshComponent.cpp:36-50](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)
  ```cpp
  void USkeletalMeshComponent::EnsureAnimInstance()
  {
      if (AnimInstance) return;
      switch (AnimationMode)
      {
      case EAnimationMode::AnimationStateMachine:
          AnimInstance = UObjectManager::Get().CreateObject<UAnimStateMachineInstance>(this);
          break;
      case EAnimationMode::AnimationSingleNode:
      default:
          AnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
          break;
      }
      AnimInstance->InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh));
  }
  ```

**사실 1-3 (생성 순서 함정).** `SetAnimationMode()`는 모드 변수만 갱신할 뿐 기존 `AnimInstance`를
파괴/재생성하지 않는다. 이미 SingleNode로 만들어진 뒤 모드를 바꿔도 StateMachine 인스턴스로 안 바뀐다.
- 근거: [SkeletalMeshComponent.h:37-40](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h)
  ```cpp
  void SetAnimationMode(EAnimationMode InAnimationMode)
  {
      AnimationMode = InAnimationMode;
  }
  ```
- 헤더 주석도 명시: [SkeletalMeshComponent.h:92-95](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h)
  `"런타임 모드 변경 후 자동 재생성은 미지원 — 별도 처리 필요."`

### Step 2 — graph 주입 경로 존재 여부 (이 진단의 핵심)

**사실 2-1.** `UAnimStateMachineInstance::SetStateMachineGraph` 선언·정의 모두 존재.
- 선언: [AnimStateMachineInstance.h:31](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h)
  ```cpp
  void SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine> InRoot);
  ```
- 정의: [AnimStateMachineInstance.cpp:11-36](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp)
  - 내부에서 `AnimGraphPtr`가 없으면 생성, `InRoot`의 각 state에 `SubLengthHint` 자동 도출,
    `AnimGraphPtr->SetRoot(std::move(InRoot))` 수행.

**사실 2-2.** `States` / `Transitions`는 `FAnimGraphNode_StateMachine`의 public 멤버이므로,
호출자가 unique_ptr을 생성해 직접 채워 넣을 수 있다.
- 근거: [AnimGraph_StateMachine.h:61-65](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h)
  ```cpp
  struct FAnimGraphNode_StateMachine : FAnimGraphNode_Base
  {
      TArray<FAnimState>      States;
      TArray<FAnimTransition> Transitions;
      int32                   InitialStateIndex = 0;
      ...
  };
  ```

**사실 2-3.** State 내부 `Sub`에 들어가는 `FAnimGraphNode_SequencePlayer`에 asset을 주입하는
공식 진입점은 `SetSequence(Skeleton, Sequence)`. TrackToBoneIndex 캐시까지 빌드한다.
- 근거: [AnimGraph.cpp:23-42](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp)

**사실 2-4 (치명적).** `USkeletalMeshComponent::AnimInstance` 멤버는 **protected** 섹션에 있고,
public getter도 friend 선언도 없다. 외부에서 `AnimInstance`를 가져와 `SetStateMachineGraph`를
호출할 통로가 코드상 존재하지 않는다.
- 근거: [SkeletalMeshComponent.h:89-109](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h)
  ```cpp
  protected:
      ...
      UAnimInstance *AnimInstance = nullptr;
  ```
  public 섹션(라인 21-87)에 `GetAnimInstance()` 같은 메서드 부재. `friend` 선언 부재.

**사실 2-5.** 코드베이스 전체에서 `SetStateMachineGraph`를 **호출하는 곳이 없다.**
선언/정의/주석 외에는 grep 매치가 없다 (test/editor/Lua/FBXImporter 어디에도 호출자 없음).

**사실 2-6.** `PlayAnimation` / `SetAnimation`은 SingleNode 전용 경로.
- 근거: [SkeletalMeshComponent.cpp:70-80](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)
  ```cpp
  void USkeletalMeshComponent::SetAnimation(UAnimationAsset *NewAnimToPlay)
  {
      AnimToPlay = NewAnimToPlay;
      EnsureAnimInstance();
      if (auto *Single = Cast<UAnimSingleNodeInstance>(AnimInstance))
      {
          Single->SetAnimation(Cast<UAnimSequence>(NewAnimToPlay));
      }
      // StateMachine 인스턴스인 경우 아무 일도 하지 않음
  }
  ```
  `PlayAnimation`은 내부에서 `SetAnimation`을 호출하므로 동일하게 StateMachine에 대해서는 묵음 무시.

→ **판정: 분기 A.** API는 살아 있으나 컴포넌트 측 노출이 없어 외부에서 호출 불가.
실용적으로 "주입 경로가 존재하지 않는" 상태. Step 3–5는 "주입 경로가 있었다고 가정했을 때
그 다음 사슬이 온전한지"를 정적 분석으로만 확인한다.

### Step 3 — StateMachine 노드의 pose 산출 경로

**사실 3-1.** `UAnimStateMachineInstance`는 `EvaluateGraph`를 **override하지 않는다.**
부모 `UAnimInstance::EvaluateGraph()`를 그대로 쓴다.
- 근거: [AnimStateMachineInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h) 헤더 주석에 명시 —
  `"평가 시 base UAnimInstance::EvaluateGraph가 AnimGraphPtr->Evaluate 호출 — 별도 우회 없음."`
- 부모 구현: [AnimInstance.cpp:87-108](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp)
  ```cpp
  void UAnimInstance::EvaluateGraph()
  {
      if (!Skeleton || !AnimGraphPtr) { OutputLocalPose.clear(); return; }
      ...
      AnimGraphPtr->Evaluate(Ctx, OutputLocalPose);
  }
  ```

**사실 3-2.** `FAnimGraphNode_StateMachine::Evaluate`는 `States`가 비어 있으면 크래시가 아니라
`OutLocalPose.clear()`를 반환한다 (즉, graph 미주입 상태로 평가가 돌아가도 크래시는 없고
빈 pose가 산출되어 → 결과적으로 `ApplyEvaluatedPose`에서 `EvaluatedLocalPose.empty()` early-return).
- 근거: [AnimGraph_StateMachine.cpp:45-54](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp)
  ```cpp
  void FAnimGraphNode_StateMachine::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
  {
      const size_t N = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
      if (States.empty() || N == 0) { OutLocalPose.clear(); return; }
      ...
  }
  ```

**사실 3-3.** State Sub가 `SequencePlayer`이고 `Sequence`/`DataModel`/`TrackToBoneIndex`가
정상적으로 채워져 있다면 pose가 평가된다. `SetStateMachineGraph`는 `InRoot` 안의 각 state의
`SubLengthHint`까지 자동 도출한다.
- 근거: [AnimStateMachineInstance.cpp:20-31](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp)
- 다만 **asset 자체의 주입은 호출자 책임** — 호출자가 unique_ptr을 만들 때 SequencePlayer를
  생성하고 그 위에 `SetSequence(Skeleton, Sequence)`까지 호출해야 한다.

### Step 4 — pose → rendering 경계

**사실 4-1.** `OutputLocalPose`는 `UAnimInstance`의 protected 멤버, 컴포넌트는
`GetOutputLocalPose()`(또는 동등 메서드)로 수령. `ApplyEvaluatedPose`로 전달.
- 근거: [AnimInstance.h:90](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h) — `TArray<FTransform> OutputLocalPose;`

**사실 4-2.** `USkinnedMeshComponent::ApplyEvaluatedPose`는 인스턴스 타입과 무관하게 동일 경로.
`FTransform::ToMatrix() → LocalBonePoseMatrices`, override 마스크는 사용자 값을 보존.
- 근거: [SkinnedMeshComponent.cpp:440-476](../KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp)
  - 입력 비었으면 early-return (StateMachine이 빈 pose 산출했을 때 안전).
  - `RebuildMeshSpaceBoneMatrices()` → `SkinVerticesToReferencePose()` → `EnsureRuntimeResources()` → bounds dirty.

**사실 4-3.** `TickComponent`는 인스턴스 타입을 검사하지 않는다 (virtual dispatch만 사용).
StateMachine 인스턴스에 대해서도 동일하게 `EvaluateGraph` → `ApplyEvaluatedPose`가 호출된다.
- 근거: [SkeletalMeshComponent.cpp:131-147](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)
  ```cpp
  if (!AnimInstance) return;
  AnimInstance->Update(DeltaTime);
  AnimInstance->EvaluateGraph();
  ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose());
  ```
  type-check 분기 없음. **SingleNode 전용 분기가 평가/렌더링 경로에는 끼어 있지 않다.**

**사실 4-4.** rendering 후속 — `LocalBonePoseMatrices → MeshSpaceBoneMatrices → SkinnedVertices →
RuntimeMeshBuffer → GPU` 경로는 인스턴스 타입과 완전히 무관.
- 근거: [SkinnedMeshComponent.cpp:344-375](../KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp) (RebuildMeshSpaceBoneMatrices),
  [SkinnedMeshComponent.cpp:377-438](../KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp) (SkinVerticesToReferencePose),
  [SkinnedMeshComponent.cpp:335-342](../KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp) (UploadSkinnedVertices).

### Step 5 — 종합

asset → 화면 사슬을 한 줄로:

```
호출자 ⛔ AnimInstance(protected, no getter)
       └→ [도달 가능했다면 ↓ 모두 정상]
       └→ SetStateMachineGraph(unique_ptr<FAnimGraphNode_StateMachine>)
          └→ AnimGraphPtr->SetRoot
             └→ TickComponent → EvaluateGraph → StateMachine::Evaluate → SequencePlayer::Evaluate
                └→ OutputLocalPose → ApplyEvaluatedPose → LocalBonePoseMatrices
                   └→ MeshSpaceBoneMatrices → SkinnedVertices → GPU → ✅ 화면
```

**끊긴 지점:** [SkeletalMeshComponent.h:108](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h)의 `AnimInstance` 노출 부재.
실제로 그 한 곳에서만 사슬이 끊긴다. 그 뒤 평가-렌더링 사슬 전체는 SingleNode와 동일한 경로로 살아 있다.

**부차적으로 끊긴 지점:**
- `SetAnimation` / `PlayAnimation`이 SingleNode 전용 Cast이므로, asset 주입 편의 진입점은 SingleNode 전용이다.
- `SetAnimationMode()`가 인스턴스를 재생성하지 않으므로, 모드 전환 순서를 잘못 잡으면 영원히 StateMachine 인스턴스가 생성되지 않는다.

---

## 4. 막힌 지점을 풀기 위해 최소로 필요한 것 (제안만 — 이 진단에서는 구현하지 않음)

진단을 실제로 진행 가능하게 만들려면 **컴포넌트 측에서 graph 주입을 위임할 통로 1개**가 필요하다.
가능한 옵션은 (택1):

1. **public 위임 메서드**
   `USkeletalMeshComponent::SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine>)` 추가 →
   내부에서 `EnsureAnimInstance()` 후 `Cast<UAnimStateMachineInstance>(AnimInstance)` 성공 시 위임 호출.
   장점: 캡슐화 유지, 잘못된 모드일 때 안전하게 묵음 처리 가능.

2. **public getter**
   `UAnimInstance* GetAnimInstance() const` 추가. 외부에서 `Cast<UAnimStateMachineInstance>`로
   캐스팅해 직접 `SetStateMachineGraph` 호출.
   장점: 변경 폭 최소. 단점: 외부에 AnimInstance 일반 접근을 허용하게 됨.

추가로 모드 전환 순서 함정 회피를 위해, 최소한 다음 중 하나가 필요:
- `EnsureAnimInstance()` 호출 전에 `SetAnimationMode(StateMachine)`을 부르는 사용 규약을 문서화한다.
- 또는 `SetAnimationMode()`가 인스턴스 타입 mismatch일 때 인스턴스를 폐기/재생성하도록 확장한다.
  (단, 이 진단은 제안만 하고 구현하지 않는다.)

---

## 5. 진단 메모

- 위 진단은 plan 문서 §B4의 "graph 주입 경로 제외" 표기와 실제 코드가 일치함을 재확인한 결과다.
  `SetStateMachineGraph` 함수 본체는 이미 작성되어 있으나, 컴포넌트 측 노출이 빠져 있어
  외부에서 사실상 호출할 수 없는 상태.
- transition rule / blend 정확도 / Lua 바인딩 / state machine 자체의 동작 정합성 등은 본 진단 범위 밖.
- 코드 수정 0건, 빌드 0건, 커밋 0건으로 정적 분석만으로 사슬 끊김 지점을 특정했다.
