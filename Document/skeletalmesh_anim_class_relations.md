# SkeletalMesh 관련 Anim 클래스: 상속·포함 관계 정리

`Source/Engine/Asset/Animation/Core/` 와 `Source/Engine/Asset/Mesh/SkeletalMesh/` 의 Graph / Instance / Sequence 계열 클래스들이 어떻게 상속되고 어떤 멤버를 통해 서로를 소유/참조하는지 한 곳에 모은다. 코드 본문은 인용하지 않고 `file:line` 레퍼런스만 둔다.

---

## 1. 인용 근거 파일

| # | 파일 |
|---|---|
| 1 | `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h` |
| 2 | `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h` |
| 3 | `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h` |
| 4 | `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h` |
| 5 | `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h` |
| 6 | `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h` |
| 7 | `KraftonEngine/Source/Engine/Asset/Mesh/SkeletalMesh/SkeletalMesh.h` |

---

## 2. 상속 트리

```
UObject
├── UAnimInstance                                    AnimInstance.h:20
│   ├── UAnimSingleNodeInstance                      AnimSingleNodeInstance.h:15
│   └── UAnimStateMachineInstance                    AnimStateMachineInstance.h:18
├── UAnimationAsset                                  AnimSequence.h:19
│   └── UAnimSequenceBase                            AnimSequence.h:67
│       └── UAnimSequence                            AnimSequence.h:88
├── USkeleton                                        Skeleton.h:23
└── USkeletalMesh                                    SkeletalMesh.h:20

FAnimGraphNode_Base (POD struct, no UObject)         AnimGraph.h:38
├── FAnimGraphNode_SequencePlayer                    AnimGraph.h:49
├── FAnimGraphNode_Blend2                            AnimGraph.h:71
├── FAnimGraphNode_BlendN                            AnimGraph.h:92
└── FAnimGraphNode_StateMachine                      AnimGraph_StateMachine.h:61

AnimGraph (container, no inheritance)                AnimGraph.h:106
```

> **주의:** `AnimGraph_StateMachine` 은 `AnimGraph` 의 파생이 **아니다**. `FAnimGraphNode_StateMachine` 이 `FAnimGraphNode_Base` 의 파생이며, `AnimGraph` 컨테이너의 root 노드 자리에 들어가는 일반 노드일 뿐이다.

---

## 3. 포함(소유/참조) 관계

```
USkeletalMesh                                        SkeletalMesh.h:20
 ├── FSkeletalMesh*        SkeletalMeshAsset  [owned] SkeletalMesh.h:54
 ├── USkeleton*            Skeleton           [ref]   SkeletalMesh.h:55
 └── TArray<FMeshMaterial> Materials          [val]   SkeletalMesh.h:56
 (※ AnimInstance / AnimGraph 와 직접 관계 없음 — USkinnedMeshComponent 가 매개)

UAnimInstance                                        AnimInstance.h:20
 ├── USkeleton*                Skeleton                       [ref]   AnimInstance.h:88
 ├── unique_ptr<AnimGraph>     AnimGraphPtr                   [owned] AnimInstance.h:89
 ├── TArray<FTransform>        OutputLocalPose                [val]   AnimInstance.h:90
 └── TArray<FName>             TriggeredNotifiesThisFrame     [val]   AnimInstance.h:91

UAnimSingleNodeInstance : UAnimInstance              AnimSingleNodeInstance.h:15
 ├── UAnimSequence*                  CurrentSequence  [ref]  AnimSingleNodeInstance.h:34
 └── FAnimGraphNode_SequencePlayer   SequencePlayer   [val]  AnimSingleNodeInstance.h:35
 (※ 상속한 AnimGraphPtr 미사용 — EvaluateGraph 오버라이드로 SequencePlayer 직접 호출)

UAnimStateMachineInstance : UAnimInstance            AnimStateMachineInstance.h:18
 └── unordered_map<FName,bool>  BoolVariables  [val]  AnimStateMachineInstance.h:38
 (※ FAnimGraphNode_StateMachine 은 상속받은 AnimGraphPtr 의 Root 로 소유 —
     SetStateMachineGraph 에서 std::move 로 이전, AnimStateMachineInstance.cpp:35)

AnimGraph                                            AnimGraph.h:106
 └── unique_ptr<FAnimGraphNode_Base>  Root  [owned]  AnimGraph.h:115

FAnimGraphNode_SequencePlayer : FAnimGraphNode_Base  AnimGraph.h:49
 ├── const UAnimSequence*   Sequence          [ref]         AnimGraph.h:60
 ├── const UAnimDataModel*  DataModel         [ref, cache]  AnimGraph.h:61
 └── TArray<int32>          TrackToBoneIndex  [val]         AnimGraph.h:62

FAnimGraphNode_Blend2 : FAnimGraphNode_Base          AnimGraph.h:71
 ├── unique_ptr<FAnimGraphNode_Base>  ChildA  [owned]  AnimGraph.h:73
 └── unique_ptr<FAnimGraphNode_Base>  ChildB  [owned]  AnimGraph.h:74

FAnimGraphNode_BlendN : FAnimGraphNode_Base          AnimGraph.h:92
 └── TArray<unique_ptr<FAnimGraphNode_Base>>  Children  [owned]  AnimGraph.h:94

FAnimGraphNode_StateMachine : FAnimGraphNode_Base    AnimGraph_StateMachine.h:61
 ├── TArray<FAnimState>       States       [val]  AnimGraph_StateMachine.h:63
 │     └── FAnimState.Sub  : unique_ptr<FAnimGraphNode_Base>  [owned]  AnimGraph_StateMachine.h:26
 └── TArray<FAnimTransition>  Transitions  [val]  AnimGraph_StateMachine.h:64

UAnimSequence : UAnimSequenceBase                    AnimSequence.h:88
 ├── UAnimDataModel*           DataModel    [ref, 상속]  AnimSequence.h:82
 ├── FString                   SequenceName / SkeletonAssetPath  AnimSequence.h:114-115
 └── TArray<FAnimNotifyEvent>  Notifies     [val]        AnimSequence.h:116
```

---

## 4. 평가 흐름 (요약)

```
USkinnedMeshComponent (외부, 본 문서 범위 밖)
   │ tick
   ├──> UAnimInstance::Update(dt)            시간/Notify 갱신
   └──> UAnimInstance::EvaluateGraph()
          │
          ├── UAnimSingleNodeInstance:
          │     SequencePlayer.Evaluate() 직접 호출 (graph 트리 우회)
          │     AnimSingleNodeInstance.cpp:31
          │
          └── UAnimInstance / UAnimStateMachineInstance:
                AnimGraphPtr->Evaluate()                 AnimInstance.cpp:107
                  └─ Root(FAnimGraphNode_*)->Evaluate(Ctx, OutLocalPose)
                       └─ Ctx.OwningInstance = this  (StateMachine 노드의 변수 조회용)
```

---

## 5. 핵심 포인트

1. **SkeletalMesh 는 애니메이션을 직접 소유하지 않는다.** `Skeleton*` 참조만 보유하며, AnimInstance 와의 결합은 외부 컴포넌트(`USkinnedMeshComponent`)가 매개한다.
2. **AnimInstance 의 평가 다형성은 두 갈래로 구현된다.**
   - **SingleNode**: 상속한 `AnimGraphPtr` 을 쓰지 않고 멤버 `SequencePlayer` 를 `EvaluateGraph()` 오버라이드에서 직접 호출.
   - **StateMachine**: 상속한 `AnimGraphPtr` 의 Root 에 `FAnimGraphNode_StateMachine` 을 심어 베이스 평가 경로(`AnimGraphPtr->Evaluate`)를 그대로 탄다.
3. **AnimSequence 는 standalone 자산**이다. Instance/Node 측은 `const UAnimSequence*` 로 참조만 보유하며 소유하지 않는다(외부 자산 라이프사이클).
4. **노드 vs 컨테이너 구분:** `AnimGraph` 는 `FAnimGraphNode_Base` 단 하나(Root)를 담는 얇은 컨테이너이고, 블렌딩·스테이트머신·시퀀스 재생 등 복합 로직은 모두 노드 트리(`FAnimGraphNode_*`) 안에서 표현된다.
5. **노드 평가 컨텍스트(`FAnimEvalContext`, AnimGraph.h:27)** 는 `OwningInstance` 포인터를 통해 StateMachine 노드가 상위 인스턴스의 BoolVariable 등을 조회할 수 있게 한다 — 노드 → 인스턴스 역참조는 평가 시점 컨텍스트에 한정된다.
