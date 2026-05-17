# Animation SingleNode 구조 교정 제안서

> 본 문서는 **제안서**다. 코드는 수정하지 않는다. 승인 후 별도 작업으로 (i) `UAnimSingleNodeInstance` 의 graph 경유 구조 교정, (ii) `USkeletalMeshComponent::TickComponent` 의 skin trigger 보강을 진행한다.
>
> 표기 규약: 추측 `[추정]` · 미확인 `[미확인]` · 코드/문서 불일치 `[불일치]`.

---

## 0. Context

TRS 추출(`PosKeys` / `RotKeys` / `ScaleKeys`) 머지가 끝났다(`d88fb2a` "TRS extraction pull", 그 전 `c996372` "Fix: RawAnimation 데이터 TRS Key 기반으로 변경"). `FAnimGraphNode_SequencePlayer::Evaluate`(`KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp:45-128`) 는 이미 TRS + Slerp 으로 완성돼 있으므로 **이번 작업에서는 읽기/검증 전용** 이다. 아래 두 가지 구조 문제만 다룬다.

1. `UAnimSingleNodeInstance` 가 단일 에셋 재생이면서 **graph 1-노드 트리를 경유**한다. UE 식 "graph 없는 단일 에셋 재생" 으로 교정 필요.
2. `USkeletalMeshComponent::TickComponent` 가 `RebuildMeshSpaceBoneMatrices()` 까지만 호출하고 `SkinVerticesToReferencePose()` / `EnsureRuntimeResources()` 를 호출하지 않아 **포즈가 GPU VB 에 반영되지 않는다**.

산출물 범위:
- **포함**: 위 두 항목의 현황 진단, 구조 교정안(권고 포함), 변경 지점 목록, 검증 연결.
- **제외**: `SequencePlayer::Evaluate` 수정안, `LocalMatrixKeys` 작업, importer 코드 수정, 실제 코드 변경.

---

## 1. STEP 1 — Merge 후 전체 스캔

### 1.1 importer 가 TRS 키를 실제로 채우는가 — **채운다**

| 항목 | 확인 결과 |
|---|---|
| 파일 | `KraftonEngine/Source/Engine/Asset/Import/FBX/Parser/FbxAnimationParser.cpp` |
| 채우는 함수 | `FFbxAnimationParser::ParseSkeletonAnimations` |
| `PosKeys` / `RotKeys` / `ScaleKeys` resize | `:186-188` — `Tracks[BoneIndex].InternalTrack.{Pos,Rot,Scale}Keys.resize(FrameCount, ...)` |
| TRS 분해 루프 | `:194-217` |
| 평가 함수 | FBX SDK `Node->EvaluateLocalTransform(Time)` (`:56`) → local matrix → TRS 분해 |
| 샘플 수 | `FrameCount = max(2, round(DurationSeconds * SampleRate) + 1)` (`:148-149`). 본마다 `FrameCount` 개 |
| `NumberOfKeys` | `FrameCount * BoneCount` (`:165`) |
| `LocalMatrixKeys` | 커밋 `c996372` 에서 `FRawAnimSequenceTrack` 에서 **필드 자체 제거** |
| 트랙 구조 정의 | `FRawAnimSequenceTrack` — `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimationTypes.h:63-76` |

검증 가능 지점: 임의 본 트랙에 대해 `track.PosKeys.size() == FrameCount` 가 성립함을 확인할 수 있다(STEP 4 의 CP1).

### 1.2 머지가 기존 파일에 미친 변경

| 파일 | 변경 |
|---|---|
| `AnimationTypes.h` | `LocalMatrixKeys` 필드 제거 (≈21줄 감소). `FRawAnimSequenceTrack` 가 순수 TRS 컨테이너로 정리됨 |
| `FbxAnimationParser.cpp` | `LocalMatrixKeys` 채우는 코드 제거, TRS 분해 루프로 일원화 (≈69줄 감소) |
| `AnimGraph.cpp` | `FAnimGraphNode_SequencePlayer::Evaluate` 가 TRS+Slerp(Option B) 로 완성 — `:122-124` (`FVector::Lerp` / `FQuat::Slerp` / `FVector::Lerp`) |
| `AnimInstance.*`, `AnimSingleNodeInstance.*`, `SkeletalMeshComponent.*`, `SkinnedMeshComponent.*` | 머지로 인한 구조 변경 없음. graph 경유 구조 및 skin trigger 누락이 그대로 남아 있음 |

### 1.3 SequencePlayer 현황 (읽기 전용 확인)

```
FAnimGraphNode_SequencePlayer::Evaluate(const FAnimEvalContext& Ctx,
                                        TArray<FMatrix>& OutLocalPose)
  └─ AnimGraph.cpp:45-128
       Pos: FVector::Lerp     (:122)
       Rot: FQuat::Slerp      (:123)
       Scl: FVector::Lerp     (:124)
       Out: FMatrix 합성       (:126)
```
입력 `FAnimEvalContext` 는 `AnimGraph.h:22-29` 에 정의 — `Skeleton`, `DataModel`, `TimeSeconds`, `TrackToBoneIndex`.

→ **이 단위(샘플링 로직)는 STEP 2 교정 후에도 그대로 호출 대상이다.** graph 트리만 우회한다.

### 1.4 기존 문서와의 불일치 / 미확인

- `[불일치]` `Document/animation_pipeline_validation_plan.md` 의 1.B-1 "TRS 분리 키 미충전 — `[끊김]`" 항목은 merge 후 **무효**. 해당 plan 의 해당 줄은 갱신 필요.
- `[미확인]` 현재 working tree 의 modified 파일(`AnimGraph.cpp`, `FBXImporter.cpp`) 변경 내용 — 보고서 상 BOM 추가뿐 `[추정]` 이라 했으나, 구현 작업 진입 전 실제 `git diff` 한 줄씩 재확인 권고.

---

## 2. STEP 2 — SingleNode 구조 교정 제안

### 2.A 현재 구조 분석

#### 호출 흐름 (graph 경유 — 교정 전)

```
USkeletalMeshComponent::TickComponent           [SkeletalMeshComponent.cpp:135]
  └─ AnimInstance->EvaluateGraph()              [virtual, AnimInstance.h:42]
       └─ AnimGraphPtr->Evaluate(Ctx, Out)      [AnimInstance.cpp:89-110]
            └─ AnimGraph::Evaluate              [AnimGraph.cpp:35-43]
                 └─ Root->Evaluate(Ctx, Out)
                      └─ FAnimGraphNode_SequencePlayer::Evaluate
                                                [AnimGraph.cpp:45-128]
```

추가 진입 경로: `USkeletalMeshComponent::EvaluateAnimationPose`(`SkeletalMeshComponent.cpp:90-124`) → `Single->EvaluateGraph()` (`:119`) — 스크럽/스냅샷용.

#### 핵심 사실

| 사실 | 위치 |
|---|---|
| `AnimGraphPtr` 가 **base** `UAnimInstance` 의 멤버이며 `std::unique_ptr<AnimGraph>` | `AnimInstance.h:95` |
| `EvaluateGraph()` 가 **base 의 `virtual`** | `AnimInstance.h:42` |
| base 의 `EvaluateGraph()` 본문이 `AnimGraphPtr->Evaluate(Ctx, OutputLocalPose)` 호출 | `AnimInstance.cpp:89-110` |
| `UAnimSingleNodeInstance` 생성자가 graph 를 만들고 `SequencePlayer` 를 Root 로 꽂음 | `AnimSingleNodeInstance.cpp:8-12` |
| SingleNode 는 `EvaluateGraph` 를 override **하지 않음** — base 구현을 그대로 사용 | (override 없음) |
| graph 경유 호출 지점은 단 두 곳 | `SkeletalMeshComponent.cpp:119`, `:135` |
| 파트3 클래스(Blend/StateMachine) 실제 코드 부재 — 문서에만 계획 | `Document/animation_part3_logic_system_plan.md` |

> 즉 base 가 "모든 AnimInstance 는 AnimGraph 를 가진다" 를 **타입 차원에서 전제** 한다. 이 전제가 깔린 상태에서 SingleNode 만 graph 를 우회시키는 두 가지 방법이 있다.

### 2.B 핵심 결정 — base 의 `AnimGraphPtr` / `virtual EvaluateGraph` 처리

#### (A) base 유지 + SingleNode 가 `EvaluateGraph()` 만 override

- 골격: base 의 `AnimGraphPtr` / `virtual EvaluateGraph()` 시그니처 **그대로**. SingleNode 가 생성자에서 graph 생성을 **중단**하고, 값 멤버로 `FAnimGraphNode_SequencePlayer SequencePlayer` 를 보유. SingleNode 의 `EvaluateGraph()` override 가 graph 를 거치지 않고 `SequencePlayer.Evaluate(Ctx, OutputLocalPose)` 를 직접 호출.
- base 의 `AnimGraphPtr` 은 SingleNode 인스턴스에서 `nullptr` 로 유휴 (`unique_ptr` 멤버 ≈ 8B `[추정]`).

```
SingleNode 호출 흐름 (교정 후 — A 안)

USkeletalMeshComponent::TickComponent
  └─ AnimInstance->EvaluateGraph()                    [virtual]
       └─ UAnimSingleNodeInstance::EvaluateGraph()    [override, 신규]
            └─ FAnimGraphNode_SequencePlayer::Evaluate(Ctx, Out)
                                                       [그대로 재사용]
```

#### (B) `AnimGraphPtr` 을 base 에서 파생으로 내림

- 골격: base `UAnimInstance` 에서 `AnimGraphPtr` 과 `EvaluateGraph` 를 제거. graph 를 쓰는 인스턴스(파트3 의 가칭 `UAnimGraphInstance`) 만 graph 멤버 보유. base 에는 다형 진입점만(예: `EvaluatePose()`) 남김. SingleNode / GraphInstance 가 각자 구현.
- 호출자(`SkeletalMeshComponent`)는 새 진입점에 맞춰 수정 필요.

#### 비교

| 축 | (A) base 유지 + override | (B) AnimGraphPtr 파생 강하 |
|---|---|---|
| base 인터페이스 변경 | 없음 | 있음 — `AnimGraphPtr`/`EvaluateGraph` 제거 또는 재명명 |
| 호출자(`SkeletalMeshComponent.cpp:119`, `:135`) 변경 | **없음** (base 포인터 다형 호출 그대로) | **있음** — 새 진입점에 맞춰 호출 측 수정 |
| 변경 파일 수 | SingleNode 1쌍(.h/.cpp) | base + SingleNode + 호출자 + 파트3 파생까지 광범위 |
| 의미 모델 | "모든 AnimInstance 는 graph 슬롯을 가짐. SingleNode 는 그 슬롯을 비워둠" | "graph 가 필요한 놈만 graph 를 가짐" — 더 깔끔 |
| 메모리 비용 | SingleNode 당 `unique_ptr` 1개 유휴 (≈ 8B `[추정]`) | 0 |
| 파트3(Blend/SM) 영향 | **없음** — 파트3 graph 인스턴스는 base 의 `AnimGraphPtr` 슬롯을 그대로 채워 사용 | **큼** — 파트3 도입 시 base 의 다형 진입점이 충분한지 재검토 + 인터페이스 재설계 |
| `EvaluateAnimationPose` (스크럽 경로) | 동일 `EvaluateGraph()` 다형 호출이라 자동 정상 | 진입점 명세 변경에 따라 별도 검토 필요 |
| 실패 위험 | 낮음 — 변경 범위가 SingleNode 한 곳 | 중 — base 인터페이스 변경이 호출자/파생 전반에 파급 |

#### 권고: **(A) 채택**

- **근거 1.** 변경 범위가 SingleNode 한 곳으로 국한되어 파트3 까지 base 인터페이스가 안정. 호출자(`SkeletalMeshComponent`) 무수정.
- **근거 2.** base 포인터로 `EvaluateGraph()` 를 다형 호출하는 기존 패턴을 그대로 보존 — 파트3 graph 인스턴스가 추가돼도 호출 측 변경 없음. 파트3 가 base 의 `AnimGraphPtr` 슬롯을 채워 쓰면 됨.
- **근거 3.** (B) 의 이점인 의미 모델 정돈은 매력적이지만, 비용(호출자 광범위 수정 + 파트3 진입 시 재설계 가능성) 대비 이번 교정의 본 목표(SingleNode graph 우회) 를 넘어선다. 메모리 비용 ≈ 8B `[추정]` 은 의미 모델 정돈을 위해 base 를 흔들 만한 트레이드오프가 아니다.

> 결정 부담을 줄이기 위한 메모: (A) 를 택하더라도 파트3 이후에 dead-weight 가 실제 문제가 된다고 판단되면 그 시점에 (B) 로 마이그레이션 가능. 반대 방향(B → A) 은 (A) 가 base 인터페이스를 유지하므로 비용이 더 든다. 즉 (A) 가 **나중 결정의 자유도가 더 크다**.

### 2.C 권고안 (A) 구현 시 변경 지점 목록

| # | 파일 | 함수/멤버 | 변경 성격 |
|---|---|---|---|
| 1 | `AnimSingleNodeInstance.h` | 멤버 신설 | `FAnimGraphNode_SequencePlayer SequencePlayer;` (값 보유) |
| 2 | `AnimSingleNodeInstance.h` | `EvaluateGraph()` 선언 | `void EvaluateGraph() override;` 추가 |
| 3 | `AnimSingleNodeInstance.cpp:8-12` | 생성자 | `AnimGraphPtr = make_unique<AnimGraph>(); SetRoot(...)` **삭제**. base `AnimGraphPtr` 은 `nullptr` 로 유지 |
| 4 | `AnimSingleNodeInstance.cpp` | `EvaluateGraph()` 신규 | `FAnimEvalContext` 채운 뒤 `SequencePlayer.Evaluate(Ctx, OutputLocalPose)` 직접 호출 |
| 5 | `AnimInstance.h` / `.cpp` | base | **무변경** — `AnimGraphPtr`, `virtual EvaluateGraph()` 그대로 |
| 6 | `SkeletalMeshComponent.cpp:119`, `:135` | 호출자 | **무변경** — base 포인터로 `EvaluateGraph()` 다형 호출 유지 |
| 7 | (선택) `AnimInstance.h/.cpp` | `protected` 헬퍼 추가 | `PrepareEvalContext(FAnimEvalContext& Out) const` 로 `AnimInstance.cpp:89-110` 의 컨텍스트 채우기 로직을 추출 — base 와 SingleNode override 양쪽에서 재사용. **중복 회피 목적. base 의 외부 인터페이스는 불변.** |

> 새 코드는 **graph 트리를 우회**할 뿐 샘플링은 기존 `FAnimGraphNode_SequencePlayer::Evaluate` 를 호출한다. **새 로직을 발명하지 않는다.**

### 2.D `SequencePlayer` 재사용 명시

- `FAnimGraphNode_SequencePlayer::Evaluate(const FAnimEvalContext&, TArray<FMatrix>&)` 의 시그니처와 본문(TRS+Slerp 샘플링)을 **그대로 호출**한다. graph 트리만 우회하며 샘플링 단위는 보존.
- 입력 컨텍스트 `FAnimEvalContext`(`AnimGraph.h:22-29`) 도 그대로 사용. SingleNode override 에서 `Skeleton`, `DataModel`, `CurrentTime`, `TrackToBoneIndex` 를 채워 호출.
- base 의 `AnimInstance.cpp:89-110` 컨텍스트 채우기 로직이 동일하게 재사용 가능 — `[추정]` 별도 `protected` 헬퍼 추출이 합리적. 헬퍼 도입 자체는 외부 인터페이스 변경이 아니므로 (A) 안의 "base 무변경" 원칙과 충돌하지 않는다.

---

## 3. STEP 3 — TickComponent skin trigger 보강 제안

### 3.A 현황 재확인

#### `USkeletalMeshComponent::TickComponent` (`SkeletalMeshComponent.cpp:126-143`)

```
1. AnimInstance->Update(DeltaTime)               [:134]
2. AnimInstance->EvaluateGraph()                 [:135]
3. LocalBonePoseMatrices = GetOutputLocalPose()  [:138]
4. RebuildMeshSpaceBoneMatrices()                [:139]
   ─── [STOP] ───
   SkinVerticesToReferencePose() : 호출 없음
   EnsureRuntimeResources()      : 호출 없음
```

#### 비교 기준 — `USkinnedMeshComponent::SetBoneLocalPose` (`SkinnedMeshComponent.cpp:94-118`)

```
1. LocalBonePoseMatrices[BoneIndex] = LocalPose  [:112]
2. RebuildMeshSpaceBoneMatrices()                [:113]
3. SkinVerticesToReferencePose()                 [:114]
4. EnsureRuntimeResources()                      [:115]
```

#### 비교 기준 — `USkinnedMeshComponent::ResetBonePoseToBindPose` (`:67-92`)

```
1. LocalBonePoseMatrices.clear()                 [:69]
2. MeshSpaceBoneMatrices.clear()                 [:70]
3. LocalBonePoseMatrices 재구성 (bind pose)      [:82-86]
4. RebuildMeshSpaceBoneMatrices()                [:88]
5. SkinVerticesToReferencePose()                 [:89]
6. EnsureRuntimeResources()                      [:90]
```

#### 결론

CPU 측 `LocalBonePoseMatrices` 는 매 틱 갱신되지만 `RebuildMeshSpaceBoneMatrices()` 이후의 **CPU 스킨 → GPU VB 업로드 체인이 누락** 되어 화면 포즈가 갱신되지 않는다. `SetBoneLocalPose` / `ResetBonePoseToBindPose` 패턴은 이 체인을 동일 호출 순서로 수행한다.

`EnsureRuntimeResources()` 내부 체인 (`SkinnedMeshComponent.cpp:262 →:308`):
```
EnsureRuntimeResources()
  ├─ RuntimeMeshBuffer.CreateDynamic() [최초 1회]
  └─ UploadSkinnedVertices()
       └─ RuntimeMeshBuffer.UpdateVertices(Context, SkinnedVertices)
```

### 3.B 보강안

`TickComponent` 의 `RebuildMeshSpaceBoneMatrices()` (`SkeletalMeshComponent.cpp:139`) 직후에 다음 두 호출을 **추가**:

```
4. RebuildMeshSpaceBoneMatrices()
5. SkinVerticesToReferencePose()     ← 추가
6. EnsureRuntimeResources()          ← 추가 (내부에서 UploadSkinnedVertices)
```

- 호출 순서·위치는 `SetBoneLocalPose:113-115` / `ResetBonePoseToBindPose:88-90` 패턴을 **그대로 따른다**.
- **새 스킨 로직을 발명하지 않는다.** 기존 함수 호출 추가에 한정.
- (선택) 정지(`bPaused`) 시 호출 빈도 정책은 별도 결정 사항 — 매 틱 무조건 vs 변경 시에만. 본 제안서에서는 결정하지 않는다.

### 3.C STEP 2 와 독립적

본 보강은 graph 우회 여부와 무관하다. `EvaluateGraph` 가 어떻게 평가되든 결과를 GPU VB 에 동기화하기 위한 트리거 누락 보강이다. **별도 PR 로 분리** 를 권고. 두 변경을 한 PR 에 묶으면 회귀 발생 시 원인 분리가 어렵다.

---

## 4. STEP 4 — 검증 연결

`Document/animation_pipeline_validation_plan.md` 의 체크포인트(CP) 들과 연결한다.

### 4.A TRS 추출 검증 (STEP 1 결과 기반 — importer 측)

| ID | 내용 | 근거 |
|---|---|---|
| CP1 | import 후 임의 본 트랙에 대해 `track.PosKeys.size() == FrameCount`, `RotKeys.size() == FrameCount`, `ScaleKeys.size() == FrameCount` | `FbxAnimationParser.cpp:186-188` 가 resize 수행 |
| CP1.b | `EvaluateLocalTransform(t0)` 결과의 translation / rotation / scale 분해값이 `track.PosKeys[0]` / `RotKeys[0]` / `ScaleKeys[0]` 과 허용 오차 내 일치 | `:194-217` 의 분해 루프 |

→ pipeline_validation_plan 의 1.B-1 "TRS 분리 키 미충전 `[끊김]`" 항목은 merge 후 `[불일치]` 이므로, 본 검증 항목이 그 자리를 대체한다.

### 4.B SingleNode 교정 검증 (STEP 2 후)

| ID | 내용 | 근거 |
|---|---|---|
| CP2 | `UAnimSingleNodeInstance::EvaluateGraph` 호출 시 base `AnimGraphPtr->Evaluate` 가 **호출되지 않음** 을 확인 (디버거 break 또는 로그 카운터) | (A) 안의 graph 우회 목적 |
| CP2.b | 동일 시퀀스·동일 시각 t 에 대해 graph 경유(교정 전) 결과와 직접 호출(교정 후) 결과 `OutputLocalPose` 의 매 본 매트릭스가 비트-동등 또는 허용 오차 내 일치 | 샘플링 단위 동일성(`SequencePlayer` 재사용) |
| CP2.c | `EvaluateAnimationPose` (`SkeletalMeshComponent.cpp:90-124`) 스크럽 경로에서도 임의 t 에 대해 CP2.b 와 동일한 결과 | 동일 `EvaluateGraph()` 다형 호출 — `[추정]` 자동 충족 |

### 4.C TickComponent skin 검증 (STEP 3 후)

| ID | 내용 | 근거 |
|---|---|---|
| CP3 | 매 틱 `UploadSkinnedVertices` 가 호출됨을 카운터로 확인 | 보강안의 호출 추가 |
| CP3.b | 검증 plan 7단계 뷰포트에서 본 회전이 화면에 실제 반영됨을 시각 확인 | CPU 스킨 → GPU VB 업로드 체인 완성 |
| CP3.c | (정책 결정 후) `bPaused` 시 호출 빈도가 정책과 일치 | 정책 별도 결정 항목 |

### 4.D 합산 검증

(CP1 + CP2 + CP3) 가 모두 통과하면 "TRS 추출 → SingleNode 직접 호출 → CPU 스킨 → GPU VB → 화면" 전 경로가 정합성 있게 동작함을 의미한다. 이는 `animation_pipeline_validation_plan.md` 의 8단계 파이프라인 검증에 직접 매핑된다.

---

## 5. 한계와 검증 안 된 가정

| ID | 내용 | 표기 |
|---|---|---|
| L1 | base `unique_ptr<AnimGraph>` 의 SingleNode 측 유휴 멤버 비용 추정치 ≈ 8B. `sizeof` 미실측 | `[추정]` |
| L2 | `AnimGraphPtr == nullptr` 일 때 base `EvaluateGraph()`(`AnimInstance.cpp:89-110`) 가 early-return 으로 안전 동작 — 한 줄 단위 본인 재확인 미수행. 권고안 (A) 는 base `EvaluateGraph()` 를 override 로 가려 호출되지 않으므로 영향은 없으나, 안전망 점검은 권고 | `[미확인]` |
| L3 | 현재 working tree 의 modified 파일(`AnimGraph.cpp`, `FBXImporter.cpp`) 이 BOM 추가만이라는 보고를 본인이 `git diff` 로 재확인 미수행 | `[추정]` |
| L4 | 파트3(Blend/StateMachine) 실제 코드 미존재 — 결정 (A) 의 파트3 영향 평가는 설계 문서(`animation_part3_logic_system_plan.md`) 기반. 실제 구현 단계에서 base 의 다형 진입점이 충분한지 재검증 필요 | `[미확인]` |
| L5 | `EvaluateAnimationPose` 임의 t 스크럽 경로의 정상 동작은 동일 `EvaluateGraph()` 다형 호출이라는 사실에서 추론. 별도 검증 항목으로 CP2.c 명시 | `[추정]` |
| L6 | `bPaused` / 시간 변화 0 인 틱에서의 skin 호출 빈도 정책은 본 제안서에서 결정하지 않음 | 정책 별도 결정 |
| L7 | base 에 `PrepareEvalContext()` 헬퍼를 추가하는 것은 외부 인터페이스 변경 아님이라는 판단 — 헬퍼는 `protected` | `[추정]` |
