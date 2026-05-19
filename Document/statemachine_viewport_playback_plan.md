# Plan: SkeletalMeshViewer 1-state StateMachine 재생 경로

## 1. Context

### 1-1. 진단 결과 요약
- `Document/statemachine_rendering_diagnosis.md`에서 확정: 평가/렌더링 사슬은 전 구간 살아 있고,
  외부 호출자가 `UAnimStateMachineInstance::SetStateMachineGraph(...)`에 도달할 통로 한 곳만 부재.
- 부차 끊김: (i) `PlayAnimation`/`SetAnimation`이 SingleNode 전용 Cast, (ii) `SetAnimationMode()`가
  인스턴스를 재생성하지 않음.

### 1-2. 레퍼런스 — 기존 SingleNode 재생 경로
ContentBrowser 더블클릭 → 경로 파싱(`Foo.fbx#Anim_<SkID>_<Idx>`) → FBX 씬 로드 →
`UAnimSequence*` 수령 → `PreviewMeshComponent->SetAnimation(Sequence)` → SingleNode 평가.

핵심 지점:
- 경로→sequence 로드: [FBXManager.cpp:600-648](../KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp)
  `FFBXManager::ResolveAnimSequenceReference(const FString& PathFileName) → UAnimSequence*`
- 위임: [MeshManager.cpp:146-150](../KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp)
  `FMeshManager::ResolveAnimSequenceReference` (단순 위임).
- viewport UI에서 컴포넌트에 주입: [SkeletalMeshEditorTab.cpp:252-282](../KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp)
  - 252-255: `PreviewMeshComponent->SetAnimation(CurrentSequence)` (현재 sequence와 다르면 교체)
  - 276-282: ImGui Combo 선택 시 `PreviewMeshComponent->SetAnimation(Sequence)` 후
    `SetBakedAnimTime(0.0f)` / `SetBakedAnimPaused(true)`
- 컴포넌트 보유: [SkeletalEditorPreviewScene.h:25-30](../KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.h)
  `FSkeletalEditorPreviewScene::PreviewMeshComponent` (`USkeletalMeshComponent*` 멤버)

→ 1-state StateMachine 경로는 **경로→sequence 로드까지는 그대로 재사용**, 컴포넌트 주입 메서드만
SingleNode 전용 `SetAnimation` 대신 새 진입점으로 갈아끼운다.

---

## 2. 코드 스캔 결과 (실제 사실 + 파일:라인 근거)

### 확인 1 — viewport SingleNode 주입 경로
- 경로 형식: `"<FbxPath>.fbx#Anim_<SkeletonId>_<AnimIndex>"`.
  - 근거: [FBXManager.cpp:600-648](../KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp)
    `ParseFbxSceneAnimSequenceReference` → `LoadFbxScene` → AnimSequences 인덱스 매칭.
- 로드 진입: ContentBrowser 더블클릭 → [EditorMainPanel.cpp:125-129](../KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp)
  `FEditorMainPanel::OpenAnimSequenceAsset(AssetPath)`.
- 주입: [SkeletalMeshEditorTab.cpp:254, 279](../KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp)
  `PreviewMeshComponent->SetAnimation(Sequence)` (`UAnimationAsset*` 받음).
- 컴포넌트 보유: [SkeletalEditorPreviewScene.h:25-30](../KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.h)
  `USkeletalMeshComponent* PreviewMeshComponent;` (struct 멤버).
- 재생 제어: [SkeletalMeshEditorTab.cpp:305-313](../KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp)
  Play/Pause/Reset 버튼이 `SetBakedAnimPaused(false)` 등을 호출.

### 확인 2 — 1-state 조립 구조
- [AnimGraph_StateMachine.h:23-30](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h)
  ```cpp
  struct FAnimState
  {
      FName                                Name;
      std::unique_ptr<FAnimGraphNode_Base> Sub;
      bool                                 bResetTimeOnEnter = true;
      bool                                 bLooping          = true;
      float                                SubLengthHint     = 0.0f;
  };
  ```
- [AnimGraph_StateMachine.h:61-81](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h)
  ```cpp
  struct FAnimGraphNode_StateMachine : FAnimGraphNode_Base
  {
      TArray<FAnimState>      States;
      TArray<FAnimTransition> Transitions;
      int32                   InitialStateIndex = 0;
      // 실시간 필드: ActiveStateIndex(-1), ActiveTransitionIndex(-1), StateLocalTimes 등
  };
  ```
- [AnimGraph.h:49-63](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h) /
  [AnimGraph.cpp:23-42](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp)
  `FAnimGraphNode_SequencePlayer::SetSequence(USkeleton*, UAnimSequence*)`: `Sequence`, `DataModel`,
  `TrackToBoneIndex`를 모두 캐시.
- 1-state 평가 정상성: [AnimGraph_StateMachine.cpp:45-127](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp)
  - `States.empty() || N == 0` 가드 통과 (라인 47-51).
  - 초기 진입: `ActiveStateIndex = clamp(InitialStateIndex, ...)` (라인 54-60).
  - `Transitions.empty()`이므로 `ActiveTransitionIndex < 0` 경로로 단일 상태 평가
    (라인 115-127): `States[ActiveStateIndex].Sub->Evaluate(Cx, OutLocalPose)`.
  - `StateLocalTimes[ActiveStateIndex] += Ctx.DeltaTime` (라인 64)로 시간 누적 정상.

### 확인 3 — 조립 순서 전제
- [AnimStateMachineInstance.cpp:11-36](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp)
  ```cpp
  for (FAnimState &S : InRoot->States)
  {
      if (S.SubLengthHint > 0.0f) continue;
      if (auto *SeqPlayer = dynamic_cast<FAnimGraphNode_SequencePlayer *>(S.Sub.get()))
      {
          if (SeqPlayer->Sequence)
              S.SubLengthHint = SeqPlayer->Sequence->GetPlayLength();
      }
  }
  AnimGraphPtr->SetRoot(std::move(InRoot));
  ```
  → `SeqPlayer->Sequence->GetPlayLength()`가 안전하려면 **호출 전에** SequencePlayer에
  `Sequence`가 set돼 있어야 함. 조립 순서는:

  1. `auto SeqPlayer = std::make_unique<FAnimGraphNode_SequencePlayer>();`
  2. `SeqPlayer->SetSequence(Skeleton, Sequence);`
  3. `FAnimState State; State.Sub = std::move(SeqPlayer); State.Name = ...;`
  4. `auto Root = std::make_unique<FAnimGraphNode_StateMachine>();`
     `Root->States.emplace_back(std::move(State)); Root->InitialStateIndex = 0;`
  5. `Instance->SetStateMachineGraph(std::move(Root));`

- `FAnimGraphNode_StateMachine`은 `FAnimGraphNode_Base` 파생이므로 `AnimGraph::SetRoot`가 직접 수용
  ([AnimGraph.h:106-116](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h)). 추가 wrapping 불필요.

### 확인 4 — 모드 전환 함정 회피
- [SkeletalMeshComponent.cpp:36-50](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)
  `EnsureAnimInstance()` 첫 줄 `if (AnimInstance) return;` 가드 → 타입 미검사. 이미 SingleNode면 그대로 둠.
- 폐기 패턴은 소멸자에 존재: [SkeletalMeshComponent.cpp:27-34](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)
  ```cpp
  if (AnimInstance)
  {
      UObjectManager::Get().DestroyObject(AnimInstance);
      AnimInstance = nullptr;
  }
  ```
- `Cast<T>`는 RTTI 기반: [Object.h:164-173](../KraftonEngine/Source/Engine/Object/Object.h)
  `Cast<UAnimStateMachineInstance>(AnimInstance)` → mismatch면 nullptr.
- 안전한 모드 강제 시퀀스:
  ```
  SetAnimationMode(EAnimationMode::AnimationStateMachine);
  if (AnimInstance && Cast<UAnimStateMachineInstance>(AnimInstance) == nullptr)
  {
      UObjectManager::Get().DestroyObject(AnimInstance);
      AnimInstance = nullptr;
  }
  EnsureAnimInstance();
  ```
- `UAnimInstance::InitializeAnimation`은 멱등 (재호출 안전):
  [AnimInstance.cpp:14-21](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp).

---

## 3. Scope

### 포함
- `USkeletalMeshComponent`에 1-state StateMachine 주입 진입점 1개 추가.
- 컴포넌트 내부에서 1-state graph 조립 (SequencePlayer → State → StateMachine → SetStateMachineGraph).
- 모드 전환 함정 회피 (타입 mismatch 시 폐기·재생성).
- viewport(`FSkeletalMeshEditorTab`)에서 새 진입점을 호출하는 최소 코드.

### 제외
- `PlayAnimation`/`SetAnimation`의 SingleNode 전용 Cast (의도된 동작 — 손대지 않음).
- 2-state·transition·blend graph (1-state 확정. blend 검증은 별도 단계).
- `SetAnimationMode()`의 전역 인스턴스 재생성 확장 (진입점 내부 국소 해결).
- `GetAnimInstance()` 일반 getter.
- viewport의 본격 UI/하네스 재설계 — 호출 1줄 거는 최소 변경만.

---

## 4. 파일별 변경 + 의사코드

### 4-1. `KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h`
- **public 메서드 1개 추가** (헤더 [37-40](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h)
  `SetAnimationMode` 인근, 또는 [52-56](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h)
  `SetAnimation` 인근):

  ```cpp
  /**
   * 1-state StateMachine 점검용 진입점.
   * 내부에서 모드를 StateMachine으로 강제하고, UAnimStateMachineInstance를 보장한 뒤
   * State 1개·Transition 0개짜리 graph를 조립해 주입한다.
   * SingleNode 경로의 SetAnimation()과 비주얼 동치 여부를 점검하기 위한 통로.
   */
  void SetAnimationAsStateMachine(UAnimSequence* InSequence);
  ```

- 필요한 forward declaration 추가: `class UAnimSequence;`가 이미 있다면 무시.

### 4-2. `KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp`
- 필요한 include 추가:
  ```cpp
  #include "Engine/Asset/Animation/Core/AnimStateMachineInstance.h"
  #include "Engine/Asset/Animation/Core/AnimGraph_StateMachine.h"
  #include "Engine/Asset/Animation/Core/AnimGraph.h"            // FAnimGraphNode_SequencePlayer
  #include "Engine/Asset/Animation/Core/AnimSequence.h"
  ```
- **신규 메서드 정의** (`SetAnimation` 정의 [70-80](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) 직후):

  ```cpp
  void USkeletalMeshComponent::SetAnimationAsStateMachine(UAnimSequence* InSequence)
  {
      AnimToPlay = InSequence;

      // 1) 모드 강제. 이미 SingleNode 인스턴스가 만들어졌다면 폐기·재생성.
      SetAnimationMode(EAnimationMode::AnimationStateMachine);
      if (AnimInstance && Cast<UAnimStateMachineInstance>(AnimInstance) == nullptr)
      {
          UObjectManager::Get().DestroyObject(AnimInstance);
          AnimInstance = nullptr;
      }
      EnsureAnimInstance();

      auto* SM = Cast<UAnimStateMachineInstance>(AnimInstance);
      if (!SM)
      {
          return; // 방어 — 일반적으로 도달 불가
      }

      USkeleton* Skeleton = ResolveSkeletonFromMesh(SkeletalMesh);
      if (!Skeleton || !InSequence)
      {
          return; // 캐시 빌드 불가. graph 미주입 상태로 둠 (bind pose).
      }

      // 2) 1-state graph 조립. 순서는 확인 3에서 확정.
      auto SeqPlayer = std::make_unique<FAnimGraphNode_SequencePlayer>();
      SeqPlayer->SetSequence(Skeleton, InSequence);

      FAnimState State;
      State.Name              = FName("StateMachineProbe");
      State.Sub               = std::move(SeqPlayer);
      State.bResetTimeOnEnter = true;
      State.bLooping          = true;
      // SubLengthHint는 SetStateMachineGraph 내부에서 자동 도출됨.

      auto Root = std::make_unique<FAnimGraphNode_StateMachine>();
      Root->States.emplace_back(std::move(State));
      Root->Transitions.clear();
      Root->InitialStateIndex = 0;

      SM->SetStateMachineGraph(std::move(Root));
  }
  ```

  주의: `FAnimState`는 move-only (unique_ptr 멤버) → `emplace_back(std::move(State))` 사용.

### 4-3. `KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp`
- viewport 호출 1지점 추가. 가장 단순한 형태: **ImGui Combo 인근에 점검용 버튼/체크박스 1개**를
  `RenderAnimationPlaybackPanel()` ([229-334](../KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp))
  안에 추가.

  ```cpp
  // RenderAnimationPlaybackPanel 안, Sequence Combo 직후 적절한 위치:
  if (ImGui::Button("Inject as 1-state StateMachine"))
  {
      // CurrentSequence는 기존 코드에서 이미 ResolveAnimSequenceReference 등을 거쳐 얻은 UAnimSequence*.
      if (CurrentSequence && PreviewMeshComponent)
      {
          PreviewMeshComponent->SetAnimationAsStateMachine(CurrentSequence);
          PreviewMeshComponent->SetBakedAnimTime(0.0f);
          PreviewMeshComponent->SetBakedAnimPaused(false);
      }
  }
  ```

  - 이 버튼이 비주얼 동치 점검의 진입. 기존 SingleNode Combo 선택 흐름은 그대로 두고,
    버튼을 추가로 누르면 같은 sequence를 1-state StateMachine으로 재주입.
  - 같은 sequence를 다시 SingleNode로 돌리고 싶을 때는 기존 Combo를 다시 선택 — `SetAnimation()`이
    호출되면서 `EnsureAnimInstance()`가 가드돼 SingleNode로 갈아끼우지는 못함. 따라서
    **toggle 검증을 위해서는 viewport 재진입(탭 닫고 다시 열기)이 필요**하다 — 진단 끊김 #3의 영향.
    이 사실을 plan §6 verification에 명시.

---

## 5. 실행 순서

1. `SkeletalMeshComponent.h`에 `SetAnimationAsStateMachine(UAnimSequence*)` public 선언 추가.
2. `SkeletalMeshComponent.cpp`에 includes + 정의 추가 (4-2).
3. `SkeletalMeshEditorTab.cpp`에 viewport 호출 버튼 추가 (4-3).
4. Full rebuild — 오류 0건 확인.
5. Verification (§6).

---

## 6. Verification

### 6-1. 정적 검증
- `Grep "SetStateMachineGraph"` → 호출처가 `SkeletalMeshComponent.cpp::SetAnimationAsStateMachine`
  단 1곳에 추가됐는지 확인. 그 외 변화 없음.
- `Grep "GetAnimInstance"` → 새로 추가된 게 없는지 확인 (일반 getter는 노출하지 않음).
- 헤더의 protected 섹션 변화 없음. `AnimInstance` 멤버 접근 변경 없음.

### 6-2. 컴파일
- Full rebuild → 0 errors.

### 6-3. 런타임 — 비주얼 동치 점검 (핵심)
- 같은 FBX(예: `Capoeira.fbx#Anim_<SkID>_0`)로 viewer를 연다.
- **레퍼런스 동선 (SingleNode):**
  1. ContentBrowser에서 더블클릭 → 기존 흐름대로 sequence가 ComboBox에서 선택됨.
  2. Combo에서 sequence 선택 → `SetAnimation()` 호출 → 화면에서 모션 재생.
  3. 모션이 정상적으로 도는 것을 시각적으로 확인.
- **점검 동선 (1-state StateMachine):**
  1. 같은 viewer 탭을 새로 열거나 재시작.
  2. Combo에서 같은 sequence 선택 (`CurrentSequence`가 채워지도록).
  3. 새 버튼 "Inject as 1-state StateMachine" 클릭 → `SetAnimationAsStateMachine()` 호출.
  4. 화면에서 모션이 도는지 확인.
- **동치 판정:**
  - ✅ 통과: 두 동선 모두 같은 모션이 같은 길이로 looping 재생.
  - ⛔ 실패 #1: 1-state 경로에서 모델이 bind pose로만 정지 → `SetSequence`/`SetStateMachineGraph`
    조립 순서나 Skeleton 전달 누락.
  - ⛔ 실패 #2: 1-state 경로에서 모델이 일그러짐/NaN → `TrackToBoneIndex` 캐시 미빌드 또는
    `MakeChildCtx` 시간 전달 문제.
  - ⛔ 실패 #3: 1-state 경로 진입 후 SingleNode로 복귀 불가 → 진단 끊김 #3 영향 (예상된 한계).
    탭 재시작으로 회피.

### 6-4. 로그/디버그
- 진입점에 `assert(SM != nullptr)` 또는 1회용 로그 한 줄을 두고 모드 전환·인스턴스 폐기가
  실제로 일어나는지 확인 (이후 제거).

---

## 7. Out of scope / 다음 단계

- 2-state + transition으로 확장한 blend 검증 — 본 plan의 비주얼 동치가 ✅ 통과한 뒤 별도 plan.
- `SetAnimation`/`PlayAnimation`에 StateMachine 분기 추가 — 의도된 단일 경로 가정을 유지.
- `SetAnimationMode()` 자체에 인스턴스 재생성 로직 통합 — 본 plan은 진입점 내부 국소 해결로
  스코프를 작게 가져감.
- viewport에 모드 선택 토글 UI 정식화 — 본 plan은 임시 버튼 1개로 점검만 가능하게 함.
