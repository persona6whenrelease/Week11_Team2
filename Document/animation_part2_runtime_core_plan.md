# Animation Runtime Core (파트 2) 구현 계획서

## Context

KraftonEngine은 파트 1에서 Animation Asset Pipeline을 정비해 `USkeleton` / `UAnimSequence` / `UAnimDataModel` / `FAnimNotifyEvent`를 분리·확정했다. 그 결과 자산 측은 명세 완료, 런타임 측은 미설계 상태로 남았다. 본 계획서는 파트 2 "Animation Runtime Core"에서 신규로 추가할 `UAnimInstance` / `UAnimSingleNodeInstance` / `AnimGraph(최소 골격)`의 책임·인터페이스·평가 흐름을 설계한다. 파트 3(블렌딩·스테이트머신·Notify dispatch)이 그래프 노드를 확장할 때 토대로 쓸 수 있도록 인터페이스 경계만 확정하는 것이 목표다.

## 0. 전제 검증

매핑 문서(`Document/animation_part23_class_mapping.md`) 1·2절의 심볼/시그니처를 실제 헤더와 대조한 결과.

### 0.1 라인 번호 / 타입 일치 — OK

| 항목 | 매핑 doc | 실제 헤더 | 정합 |
|---|---|---|---|
| `FFrameRate` | `Core/AnimationTypes.h:22-38` | 동일 | ✓ |
| `FBoneInfo` | `Core/AnimationTypes.h:43-58` | 동일 | ✓ |
| `FRawAnimSequenceTrack` | `Core/AnimationTypes.h:66-81` | 동일 | ✓ |
| `FBoneAnimationTrack` | `Core/AnimationTypes.h:86-97` | 동일 | ✓ |
| `FAnimationCurveData` (빈 자리) | `Core/AnimationTypes.h:104-112` | 동일 | ✓ |
| `FAnimNotifyEvent` + `IsTriggeredBetween` | `Notify/AnimNotify.h:18-47` | 동일 | ✓ |
| `FSkeleton` / `USkeleton` | `Core/Skeleton.h:7-21` / `:23-47` | 동일 | ✓ |
| `UAnimationAsset`/`UAnimDataModel`/`UAnimSequenceBase`/`UAnimSequence` | `Core/AnimSequence.h:19-24/29-62/67-83/88-116` | 동일 | ✓ |

### 0.2 매핑 doc이 다루지 않은 사실 (신규 발견)

1. **본 이름 타입 불일치 (중요)** — `USkeleton::FindBoneIndexByName`은 `const FString&`을 받지만 `FBoneAnimationTrack::Name`은 `FName`. 트랙→본 인덱스 resolve 시 `FName::ToString()` 변환이 매번 일어난다. 본 계획에서는 트랙별 인덱스를 1회만 캐싱(`TrackToBoneIndex`)하는 설계로 회피.
2. **`UAnimSequenceBase`에 getter 두 개** — `GetDataModel()` 외에 `GetDataMode()`(오타로 추정)가 동시 존재(`AnimSequence.h:72-74`). 신규 코드는 `GetDataModel()` 한쪽만 호출.
3. **컴포넌트에 이미 들어있는 placeholder** — `USkeletalMeshComponent.h`는 매핑 doc 외에 다음 신호를 이미 갖고 있다:
   - `enum class EAnimationMode { AnimationSingleNode }` (`SkeletalMeshComponent.h:9-12`)
   - `void PlayAnimation(UAnimationAsset*, bool bLooping = true)` (`:25`)
   - `bool EvaluateAnimationPose(const UAnimSequence* Sequence, float TimeSeconds)` (`:47`)
   - `AnimToPlay` / `BakedAnimTime` / `bBakedAnimPaused` / `BakedAnimPlaybackSpeed` / `bBakedAnimLooping` (`:82-88`)

   즉, 신규 `UAnimInstance`가 들어갈 자리(시퀀스→포즈 평가)가 컴포넌트에 이미 자리잡혀 있다. 본 설계는 이 placeholder를 `UAnimInstance`로 이전하는 방향을 택한다.
4. **컨테이너 관습** — `UAnimSequence::AddNotify`가 `Notifies.push_back` 호출(`AnimSequence.h:103`)을 사용. `TArray`가 사실상 `std::vector` 별칭으로 보이며 신규 코드에서도 동일 관습 사용.

이외 매핑 doc과 헤더 간 의미적 불일치는 발견되지 않음.

## 1. 클래스별 책임 정의

### 1.1 `UAnimInstance` (신규 — 추상/기반)

**하는 일**
- 한 프레임 애니메이션 평가 총괄. `Update(DeltaTime)` 진입점 제공.
- 시간 상태 보유: `CurrentTime`, `PreviousTime`, `PlaybackSpeed`, `bLooping`, `bPaused`.
- 평가 결과 **로컬 포즈 행렬 배열** `OutputLocalPose`를 멤버로 보관 → 컴포넌트가 read만.
- `USkeleton*` 참조(소유 X) 보유.
- `AnimGraph`를 소유(`std::unique_ptr`). 평가는 그래프에 위임.
- Notify 트리거 판정을 위해 `PreviousTime`을 유지.

**하지 않는 일**
- 특정 시퀀스 선택/조합 로직 — 파생 또는 그래프 노드 소관.
- FK / 스키닝 매트릭스 산출 — 컴포넌트의 `RebuildMeshSpaceBoneMatrices` / `SkinVerticesToReferencePose` 소관.
- Notify dispatch 수신자 호출 — 파트 3에서 `TDelegate`로 확장.

### 1.2 `UAnimSingleNodeInstance` (신규 — `UAnimInstance` 파생, 구체)

**하는 일**
- 단일 `UAnimSequence*` 재생. 가장 단순한 `UAnimInstance` 구체.
- `SetAnimation(UAnimSequence*)`, `SetLooping(bool)`.
- 그래프 평가 시 root 노드가 단일 `FAnimGraphNode_SequencePlayer`인 형태.

**하지 않는 일**
- 블렌딩 / 다중 시퀀스 합성 / state 전환 — 파트 3.

### 1.3 `AnimGraph` (신규 — 최소 골격만)

**하는 일**
- 평가 진입점 1개: `Evaluate(Ctx, OutLocalPose)`.
- Root 노드 슬롯과 노드 베이스 추상(`FAnimGraphNode_Base`) 인터페이스만 둔다.
- 파트 2에서 유일하게 구체화되는 노드: `FAnimGraphNode_SequencePlayer`(단일 시퀀스 샘플링).

**하지 않는 일 (확장 지점 — 파트 3)**
- 그래프 편집/직렬화.
- 추가 노드(`FAnimGraphNode_Blend2`, `FAnimGraphNode_StateMachine` 등).
- 트리 구조 관리.

### 1.4 책임 분리의 근거

- **`UAnimInstance` vs `UAnimSingleNodeInstance`**: 시간 관리 / 평가 진입 / 포즈 보유는 모든 인스턴스 공통이므로 base. "어떤 시퀀스를 어떻게 조합하나"는 placeholder / state machine / blendspace 등으로 다양하게 분기되므로 파생에서만 결정한다.
- **`AnimGraph` 분리**: 단일 클립이라면 그래프 없이 평가 가능하지만, 파트 3에서 블렌딩/state machine이 들어오는 순간 평가 트리가 필수다. 미리 진입점만 만들어두면 파트 3에서 노드 추가만으로 확장 가능. 본 계획서는 **노드 베이스 + Root 슬롯 + SequencePlayer 노드 1개**까지만 잡는다.

## 2. 소유/참조 관계도

```
USkeletalMeshComponent
 ├─ ref ─> USkeletalMesh*               (USkinnedMeshComponent::SkeletalMesh)
 │           └─ ref ─> USkeleton*       (mesh가 가리키는 스켈레톤 자산)
 │
 ├─ owns ─> UAnimInstance*              (component 별 1개; 컴포넌트 소멸 시 동시 소멸)
 │   │
 │   ├─ ref ─> USkeleton*               (component->SkeletalMesh→Skeleton 경유; 캐시)
 │   ├─ holds ─> TArray<FMatrix>  OutputLocalPose   (size = BoneCount)
 │   ├─ holds ─> float CurrentTime / PreviousTime / PlaybackSpeed
 │   ├─ holds ─> bool  bLooping / bPaused
 │   ├─ holds ─> TArray<int32> TrackToBoneIndex     (FName→idx 캐시, 시퀀스 set 시 1회 빌드)
 │   │
 │   └─ owns ─> std::unique_ptr<AnimGraph>
 │              └─ owns ─> Root 노드 (파트 2: FAnimGraphNode_SequencePlayer 1개)
 │
 └─ 진입점: PlayAnimation(UAnimSequence*, bool bLoop)
       └─ AnimInstance에 시퀀스 set + 시간 초기화 + 트랙→본 인덱스 캐시 재빌드
```

### 진입점: `PlayAnimation(UAnimSequence*, bool bLoop)`

현재 `USkeletalMeshComponent::PlayAnimation(UAnimationAsset*, bool)`(`SkeletalMeshComponent.h:25`) 시그니처를 유지하되 내부 동작은:

1. `AnimToPlay = NewAnimToPlay`
2. `AnimInstance`가 없으면 `AnimationMode`에 따라 `UAnimSingleNodeInstance` 생성.
3. `static_cast<UAnimSingleNodeInstance*>(AnimInstance)->SetAnimation(static_cast<UAnimSequence*>(AnimToPlay))`
4. `AnimInstance->SetLooping(bLooping); AnimInstance->ResetTime(); AnimInstance->SetPaused(false);`

### 수명 요약

| 객체 | 소유자 | 수명 만료 |
|---|---|---|
| `USkeleton` / `UAnimSequence` / `UAnimDataModel` | 자산 시스템 | 자산 unload 시 |
| `UAnimInstance` / `UAnimSingleNodeInstance` | `USkeletalMeshComponent` | 컴포넌트 소멸 시 |
| `AnimGraph` + 노드 | `UAnimInstance` | AnimInstance 소멸 시 |
| `OutputLocalPose` / `TrackToBoneIndex` | `UAnimInstance` 내부 멤버 | 위와 동일 |

자산이 swap되거나 unload되는 경우, 컴포넌트가 `AnimInstance`에 invalidate 신호를 보내고 `AnimInstance`는 캐시(`TrackToBoneIndex`)와 `OutputLocalPose`를 비운다.

## 3. 헤더 인터페이스 골격

### 3.1 `UAnimInstance`

```cpp
class UAnimInstance : public UObject
{
public:
    DECLARE_CLASS(UAnimInstance, UObject)

    UAnimInstance();
    ~UAnimInstance() override;

    // 라이프사이클
    virtual void InitializeAnimation(USkeleton* InSkeleton);
    virtual void Update(float DeltaTime);          // 시간 누적만
    virtual void EvaluateGraph();                  // AnimGraph 평가 -> OutputLocalPose

    // 결과 조회 (컴포넌트가 read)
    const TArray<FMatrix>& GetOutputLocalPose() const { return OutputLocalPose; }

    // 시간 상태
    void  SetLooping(bool b)        { bLooping = b; }
    void  SetPlaybackSpeed(float s) { PlaybackSpeed = s; }
    void  SetPaused(bool b)         { bPaused = b; }
    void  ResetTime()               { PreviousTime = CurrentTime = 0.0f; }
    float GetCurrentTime() const    { return CurrentTime; }
    float GetPreviousTime() const   { return PreviousTime; }

    // 그래프 접근 (파트 3에서 노드 교체용)
    AnimGraph* GetAnimGraph() const { return AnimGraph.get(); }

protected:
    // 파생이 길이를 제공 (단일 시퀀스 / 스테이트머신 등에서 다르게 계산)
    virtual float GetEffectivePlayLength() const = 0;

    // 트랙(FName) → 본 인덱스 캐시 빌드 (시퀀스 변경 시 1회 호출)
    void  RebuildTrackToBoneIndex(const UAnimDataModel* Model);

    // 데이터
    USkeleton*                 Skeleton = nullptr;     // ref, not owned
    std::unique_ptr<AnimGraph> AnimGraph;              // owned
    TArray<FMatrix>            OutputLocalPose;        // size = BoneCount
    TArray<int32>              TrackToBoneIndex;       // size = current DataModel 트랙 수

    float CurrentTime    = 0.0f;
    float PreviousTime   = 0.0f;
    float PlaybackSpeed  = 1.0f;
    bool  bLooping       = true;
    bool  bPaused        = true;
};
```

### 3.2 `UAnimSingleNodeInstance`

```cpp
class UAnimSingleNodeInstance : public UAnimInstance
{
public:
    DECLARE_CLASS(UAnimSingleNodeInstance, UAnimInstance)

    void           SetAnimation(UAnimSequence* InSequence);
    UAnimSequence* GetAnimation() const { return CurrentSequence; }

    void Update(float DeltaTime) override;
    void EvaluateGraph() override;

protected:
    float GetEffectivePlayLength() const override;  // CurrentSequence->GetPlayLength()

private:
    UAnimSequence* CurrentSequence = nullptr;       // ref, not owned
};
```

### 3.3 `AnimGraph` 최소 골격

```cpp
// 평가 컨텍스트 — 파트 3에서 필드 확장 예정 (블렌드 가중치, state 식별자 등)
struct FAnimEvalContext
{
    const USkeleton*      Skeleton         = nullptr;
    const UAnimDataModel* DataModel        = nullptr; // 단일 노드 경로 단축용
    const TArray<int32>*  TrackToBoneIndex = nullptr;
    float                 TimeSeconds      = 0.0f;
};

// 노드 베이스 (파트 3에서 다양한 노드로 파생) — 확장 지점
struct FAnimGraphNode_Base
{
    virtual ~FAnimGraphNode_Base() = default;
    virtual void Evaluate(const FAnimEvalContext& Ctx,
                          TArray<FMatrix>&        OutLocalPose) = 0;
};

// 파트 2의 유일한 구체 노드: 단일 시퀀스 샘플링 노드
struct FAnimGraphNode_SequencePlayer : FAnimGraphNode_Base
{
    void Evaluate(const FAnimEvalContext& Ctx,
                  TArray<FMatrix>&        OutLocalPose) override;
};

// 그래프 본체 — 파트 2에서는 Root 슬롯 1개만
class AnimGraph
{
public:
    void SetRoot(std::unique_ptr<FAnimGraphNode_Base> Node) { Root = std::move(Node); }
    void Evaluate(const FAnimEvalContext& Ctx, TArray<FMatrix>& OutLocalPose);

private:
    std::unique_ptr<FAnimGraphNode_Base> Root;
};
```

> **파트 3 확장 지점**
> - 추가 노드: `FAnimGraphNode_Blend2`, `FAnimGraphNode_StateMachine`, ...
> - `FAnimEvalContext` 확장 필드: 블렌드 가중치, 상태 식별자, 입력 핀 등
> - `AnimGraph`에 다중 노드 / 연결 표현 추가

## 4. 핵심 기능별 의사코드

### 4.1 Animation time update (loop / reverse)

```
fn UAnimInstance::Update(dt):
    if bPaused: return
    Length = GetEffectivePlayLength()         # 파생이 제공 (single node = Sequence->GetPlayLength())
    if Length <= 0:
        PreviousTime = CurrentTime = 0; return

    PreviousTime = CurrentTime
    NewTime = CurrentTime + dt * PlaybackSpeed   # PlaybackSpeed 음수 -> reverse

    if bLooping:
        Wrapped = fmod(NewTime, Length)
        if Wrapped < 0: Wrapped += Length        # reverse 시 음수 보정
        CurrentTime = Wrapped
    else:
        Clamped = clamp(NewTime, 0, Length)
        CurrentTime = Clamped
        if NewTime >= Length or NewTime < 0:
            bPaused = true                       # 끝에서 자동 정지
```

> **길이 출처**: `GetEffectivePlayLength()` → `UAnimSingleNodeInstance`에서 `CurrentSequence->GetPlayLength()` = `UAnimSequenceBase::GetPlayLength()` = `DataModel->GetPlayLength()`.
> **fmod**: 단일 fmod 후 음수면 +Length 한 번. 반복 fmod 회피.

### 4.2 Keyframe sampling (FrameA / FrameB / Blend)

```
fn FAnimGraphNode_SequencePlayer::Evaluate(Ctx, OutLocalPose):
    Model    = Ctx.DataModel
    Skeleton = Ctx.Skeleton
    if not Model or not Skeleton:
        FillBindPose(OutLocalPose, Skeleton); return

    NumKeys = Model.GetNumberOfKeys()
    FPS     = Model.GetFrameRate().AsDecimal()      # 0 가드 내장
    if NumKeys <= 0 or FPS <= 0:
        FillBindPose(OutLocalPose, Skeleton); return

    # 1) bind pose로 초기화 (트랙 누락 본을 위한 fallback)
    Bones = Skeleton.GetBones()
    OutLocalPose.resize(Bones.size())
    for i in 0..Bones.size():
        OutLocalPose[i] = Bones[i].LocalBindPose

    if NumKeys == 1:
        # 모든 트랙: 단일 키만 적용 (Blend 없음)
        SampleAllTracksAt(Model, Ctx.TrackToBoneIndex, 0, OutLocalPose); return

    # 2) FrameA / FrameB / Blend 계산
    FrameFloat = Ctx.TimeSeconds * FPS
    FrameA     = clamp(floor(FrameFloat), 0, NumKeys - 1)
    FrameB     = min(FrameA + 1, NumKeys - 1)
    Blend      = clamp(FrameFloat - FrameA, 0, 1)

    # 3) 트랙별 키 보간
    Tracks = Model.GetBoneAnimationTracks()
    for tIdx in 0..Tracks.size():
        BoneIndex = (*Ctx.TrackToBoneIndex)[tIdx]
        if BoneIndex < 0: continue
        Raw = Tracks[tIdx].InternalTrack

        # ===== Option A (본 계획의 1차 안 — LocalMatrixKeys element-wise lerp) =====
        if Raw.LocalMatrixKeys.size() >= NumKeys:
            MA = Raw.LocalMatrixKeys[FrameA]
            MB = Raw.LocalMatrixKeys[FrameB]
            OutLocalPose[BoneIndex] = MatrixLerp(MA, MB, Blend)
            continue

        # ===== Option B (향후 — TRS 분리, Open Question OQ1 참고) =====
        # P = VectorLerp(Raw.PosKeys[FrameA],   Raw.PosKeys[FrameB],   Blend)
        # R = QuatSlerp (Raw.RotKeys[FrameA],   Raw.RotKeys[FrameB],   Blend)
        # S = VectorLerp(Raw.ScaleKeys[FrameA], Raw.ScaleKeys[FrameB], Blend)
        # OutLocalPose[BoneIndex] = ComposeTRS(P, R, S)
```

### 4.3 Local Pose 계산

4.2의 산출 `OutLocalPose: TArray<FMatrix>(BoneCount)`이 곧 Local Pose다. 별도 산출 단계 없음. `UAnimInstance::OutputLocalPose`에 그대로 보관.

### 4.4 Component Space Pose 계산 (FK)

**신규 코드 아님.** 기존 `USkinnedMeshComponent::RebuildMeshSpaceBoneMatrices()`가 수행. 본 설계는 `OutputLocalPose`를 컴포넌트로 노출하는 것까지만 책임.

참고용 (컴포넌트 측, 변경 없음):

```
fn RebuildMeshSpaceBoneMatrices():
    Bones = SkeletalMesh.Skeleton.GetBones()
    Local = AnimInstance.GetOutputLocalPose()
    Mesh  = MeshSpaceBoneMatrices
    Mesh.resize(Bones.size())
    for i in 0..Bones.size():
        Parent = Bones[i].ParentIndex
        if Parent < 0:
            Mesh[i] = Local[i]
        else:
            assert(Parent < i)            # FBX importer가 정렬 보장 (매핑 doc 4절 주장)
            Mesh[i] = Local[i] * Mesh[Parent]
```

### 4.5 Skinning Matrix 생성

**신규 코드 아님.** 기존 `USkinnedMeshComponent::SkinVerticesToReferencePose` 내부에서 `SkinMatrix = InverseBindPose * MeshSpaceBone` 형태.

```
fn BuildSkinningMatrices(Mesh, Skeleton) -> SkinMatrix[]:
    Bones = Skeleton.GetBones()
    for i in 0..Bones.size():
        SkinMatrix[i] = Bones[i].InverseBindPose * Mesh[i]
```

### 4.6 Bone 이름 ↔ 인덱스 resolve

```
fn UAnimInstance::RebuildTrackToBoneIndex(Model):
    if not Model or not Skeleton:
        TrackToBoneIndex.clear(); return

    Tracks = Model.GetBoneAnimationTracks()
    TrackToBoneIndex.resize(Tracks.size())
    for tIdx in 0..Tracks.size():
        TrackName = Tracks[tIdx].Name              # FName
        BoneIdx   = Skeleton.FindBoneIndexByName(TrackName.ToString())
        TrackToBoneIndex[tIdx] = BoneIdx           # 못 찾으면 -1
```

**호출 시점**: `UAnimSingleNodeInstance::SetAnimation()` 또는 `InitializeAnimation()` 내 1회. 매 프레임 resolve 회피. `FName::ToString()` 비용을 시퀀스 set 시 1회로 한정.

## 5. 한 프레임 데이터 흐름

```
USkeletalMeshComponent::TickComponent(dt)
   │
   ├─ AnimInstance->Update(dt)
   │     └─ 시간 누적 / loop / reverse 보정 → CurrentTime 갱신
   │        (PreviousTime은 누적 전 값 저장 — Notify 트리거 판정에 사용)
   │
   ├─ AnimInstance->EvaluateGraph()
   │     ├─ FAnimEvalContext 구성 (Skeleton, DataModel, TrackToBoneIndex, CurrentTime)
   │     ├─ AnimGraph.Evaluate(Ctx, OutputLocalPose)
   │     │     └─ Root 노드 (SequencePlayer) → 키 샘플링 → OutputLocalPose 채움
   │     └─ Notify 트리거 판정
   │          - Sequence->GetNotifies() 각 항목에 대해
   │            FAnimNotifyEvent::IsTriggeredBetween(PreviousTime, CurrentTime, Length) 호출
   │          - 트리거된 NotifyName 리스트를 노출하는 자리까지만. (dispatch는 파트 3)
   │
   ├─ (Component) RebuildMeshSpaceBoneMatrices()
   │     └─ OutputLocalPose + Bones[i].ParentIndex 체이닝 → MeshSpaceBoneMatrices
   │
   └─ (Component) SkinVerticesToReferencePose()
         └─ MeshSpaceBoneMatrices + Bones[i].InverseBindPose → 정점 변환
```

매핑 doc 4절 흐름도를 신규 클래스 관점으로 재배치한 것이며, 본 설계의 핵심 변경은:
- `Update` / `EvaluateGraph` 단계까지가 `UAnimInstance`로 이동.
- FK / 스키닝은 기존대로 컴포넌트 책임.
- Notify 트리거 **판정**은 `UAnimInstance`에서, **dispatch**는 파트 3로 분리.

## 6. 구현 순서와 의존성

| 단계 | 작업 | 선행 조건 | 검증 마일스톤 |
|---|---|---|---|
| S1 | `UAnimInstance` 헤더 골격 + 시간 멤버 + `OutputLocalPose` | 본 계획 승인 | 컴파일 통과 |
| S2 | `AnimGraph` / `FAnimGraphNode_Base` / `FAnimGraphNode_SequencePlayer` 헤더 골격 | S1 | 컴파일 통과 |
| S3 | `UAnimSingleNodeInstance` 헤더 + `SetAnimation` | S1, S2 | 컴파일 통과, 자산 set 시 nullptr 가드 동작 |
| S4 | `UAnimInstance::Update` 본문 (시간 누적/loop/reverse) | S1 | `dt → CurrentTime` 누적이 단위 테스트 수준으로 검증 (loop wrap, 음수 reverse wrap, 정지) |
| S5 | `RebuildTrackToBoneIndex` | S1, S3 | 트랙 N개 → 캐시 size N, 미존재 본은 -1 |
| S6 | `FAnimGraphNode_SequencePlayer::Evaluate` (LocalMatrixKeys 경로) | S2, S5 | t=0 → 첫 키, t=Length → 마지막 키, 누락 트랙 본은 bind pose |
| S7 | `UAnimSingleNodeInstance::EvaluateGraph` wire-up | S3, S6 | 임의 t에서 OutputLocalPose 본 수 일치 + 값 유한 |
| S8 | `USkeletalMeshComponent` ↔ `UAnimInstance` 연결: `PlayAnimation`이 SingleNode 인스턴스 생성/세팅, `TickComponent`가 `Update`+`EvaluateGraph` 호출, `EvaluateAnimationPose`는 위임 형태로 보존 | S4, S7 | 파트 4 viewer에서 시퀀스 재생/스크럽 시각화 정상 |
| S9 | Notify 트리거 판정 패스 (dispatch 제외) | S4 | `IsTriggeredBetween`이 prev/curr/length로 NotifyName 리스트 산출. 루프 감김 케이스 포함 |

### 파트 3가 의존할 인터페이스가 확정되는 시점

- **S2 종료 시**: `FAnimGraphNode_Base::Evaluate(Ctx, OutLocalPose)` 시그니처 확정 → 파트 3가 신규 노드 추가 시 이 시그니처에 맞추면 됨.
- **S9 종료 시**: `UAnimInstance`가 노출하는 `(PreviousTime, CurrentTime, Length, Notifies)` 4튜플 확정 → 파트 3 Notify dispatch가 이 입력만 받으면 됨.

## 7. 미해결 결정 사항 (Open Questions)

### OQ1. `LocalMatrixKeys` vs TRS 분리 키 — 어느 쪽을 포즈 정본으로?

- **현황**: `FRawAnimSequenceTrack`은 `PosKeys`/`RotKeys`/`ScaleKeys` **와** `LocalMatrixKeys`를 동시에 보관(`AnimationTypes.h:66-81`, 주석 `:62-65`에 "추후 TRS 분해 샘플링으로 넘어가면 LocalMatrixKeys는 줄일 수 있다"). 컴포넌트의 과거 코드는 `LocalMatrixKeys` element-wise lerp 기반이라, 회전이 rotation slerp가 아니라 4x4 행렬 element-wise lerp로 보간되어 affine drift가 잠재. 30fps에서 무시 가능하다는 가정이 코드 코멘트로 남아 있었음.
- **선택지**
  - **A**: 본 파트 2는 `LocalMatrixKeys` 그대로 유지 (현 컴포넌트 동작과 동등). slerp 도입은 추후.
  - **B**: 본 파트 2에서 TRS 분리 + slerp 도입. `ComposeTRS()` 유틸 필요. `LocalMatrixKeys`는 디스크 호환 위해 유지하되 런타임 샘플링에서 사용 안 함.
- **영향 단계**: **S6**(`SequencePlayer::Evaluate`)에만 영향. 다른 단계 영향 없음.
- **결정 보류 이유**: TRS 보간 도입은 (i) `Math/Quat.h`에 slerp 존재 여부, (ii) FBX importer가 `PosKeys`/`RotKeys`/`ScaleKeys`를 `LocalMatrixKeys`와 같은 시간 정합성으로 채우는지 확인 필요. 본 계획서 범위 밖.

### OQ2. `FAnimationCurveData` 빈 구조의 처리

- **현황**: `AnimationTypes.h:104-112`에 자리만 잡힘. 직렬화 operator도 사실상 no-op.
- **본 파트 2에서의 처리**: 사용하지 않음. `UAnimInstance`가 참조하지도 않음.
- **영향 단계**: 없음.
- **파트 3로 이관**: blend weight curve / transition curve가 필요해지면 그때 본 구조를 확장.

### OQ3. `EvaluateAnimationPose(const UAnimSequence*, float)` 처리

- **현황**: `USkeletalMeshComponent.h:47`에 이미 선언. 본문 구현 위치/상태는 본 계획 범위 밖.
- **선택지**
  - **A**: 메서드를 `UAnimInstance`로 이전, 컴포넌트는 호출자로 전환.
  - **B**: 컴포넌트 측 메서드는 시그니처 유지하되 내부에서 `AnimInstance->EvaluateGraph()`로 위임.
- **영향 단계**: **S8**.
- **본 계획의 잠정 권고**: **B**. 파트 4 viewer 등 외부 호출자가 이미 이 메서드명을 알 가능성이 있어 시그니처 보존이 안전.

### OQ4. `UAnimSequenceBase::GetDataMode()` (오타로 추정) 처리

- 신규 코드는 `GetDataModel()`만 호출하기로 한다. 오타 getter 제거는 본 계획 범위 외 별도 작업.

---

## 본 계획서 한계 / 검증 안 된 가정

- **[추정]** 신규 `UAnimInstance`가 컴포넌트의 anim 멤버를 옮겨받는 방향이 자연스럽다고 적었으나, 기존 `AnimToPlay` / `BakedAnimTime` / `bBakedAnimPaused` / `BakedAnimPlaybackSpeed` / `bBakedAnimLooping` 멤버를 어디까지 제거 또는 forward 시킬지는 S8 단계의 세부 설계로 분리한다.
- **[추정]** `std::unique_ptr<AnimGraph>`로 소유한다고 적었으나, KraftonEngine UObject 시스템이 raw pointer + `FObjectFactory` 등록 관습을 강하게 따른다면 그 관습에 맞춰 조정 필요. `Object/ObjectFactory.h` 사용 패턴 확인 후 결정.
- **[추정]** Notify 트리거 판정의 prev/curr 시간 의미는 "wrap 후의 `[0, Length)` 범위 시간"이라는 해석. `FAnimNotifyEvent::IsTriggeredBetween`이 prev>curr 케이스를 처리하는 코드 경로(`AnimNotify.h:45`)에 기반한 추정이며, FBX 임포터·에디터 측에서 시간 도메인을 다르게 다룰 가능성은 검증 안 됨.
- **검증 안 된 가정**: `Bones[i].ParentIndex < i` 정렬은 매핑 doc 및 컴포넌트 주석이 보장한다고 진술. FBX importer 코드 직접 확인은 본 계획 범위 외.
- **검증 안 된 가정**: `TArray`가 `std::vector` 별칭이라는 점은 `Notifies.push_back` 호출(`AnimSequence.h:103`)로 추정. 단정 아님.
- **검증 안 된 가정**: `Math/Matrix.h`에 행렬 element-wise lerp 또는 `Math/Quat.h`에 slerp 유틸이 존재하는지 여부는 본 계획 범위 외. S6 실제 구현 시 별도 확인 필요.
- **[추정]** `UAnimInstance::OutputLocalPose`의 size 운용은 "스켈레톤 본 수에 맞춰 매 평가 시 resize"로 적었으나, 본 수가 변하지 않는다면 `InitializeAnimation` 한 번에 reserve/resize하는 편이 효율적. 본 계획은 단순함을 우선해 후자를 강제하지 않음.
