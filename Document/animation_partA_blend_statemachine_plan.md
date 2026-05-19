# 파트 A 구현 계획 v2 — Animation Blend / State Machine

> v1 → v2 개정. v1은 `Document/animation_partA_blend_statemachine_plan_v1_backup.md`로 보존됨.

## 0. v2 개정 요약

v1 §6 미해결 #3(SequencePlayer 노드별 시퀀스 ref + 캐시)을 **파트 A 범위 안에서 처리**하기로 결정 — 본 개정안의 명칭은 **길 2**.

**길 2의 핵심:** SequencePlayer 노드가 평가 입력을 자체 field로 **hold(ref, 소유 아님)**한다.

| v1 항목 | v2 변경 | 근거 |
|---|---|---|
| `FAnimEvalContext`에 `DataModel`/`Sequence`/`TrackToBoneIndex` 필드 보유 | **세 필드 제거**. `Skeleton`/`TimeSeconds`/`DeltaTime`/`OwningInstance`만 남김 | 노드가 입력을 직접 hold → Ctx가 운반할 필요 소멸. v1 §4.4의 "DataModel을 패치하지 않은" 결함이 자동 해소 |
| `FAnimGraphNode_SequencePlayer` stateless | **field 보유로 변경**: `const UAnimSequence* Sequence`(ref), `const UAnimDataModel* DataModel`(ref 캐시), `TArray<int32> TrackToBoneIndex`(값) | StateMachine 각 상태가 다른 시퀀스를 가지려면 노드 단위 분리 필수 |
| `UAnimInstance::TrackToBoneIndex`/`RebuildTrackToBoneIndex`/`GetActiveDataModel()` | **모두 제거**. 캐시·로직이 노드로 이관됨 | 정본 이중화 회피. `GetActiveDataModel`은 호출처가 `RebuildTrackToBoneIndex` 단 1곳뿐이라 함께 데드 |
| §4.4 `Ctx` 패치 대상: `TimeSeconds` + (잠재적으로 `DataModel`) | **`TimeSeconds`만** | 더 깨끗해진 메커니즘 |
| §5 A1 단계 = "시그니처 전환" 단일 | **A1a(입력 소스 전환) + A1b(출력 형식 전환) 2단계**로 분리 | 두 변경이 SequencePlayer 한 노드에 겹쳐 SingleNode 회귀 시 격리가 어렵기 때문 |
| §4.5 `GetActiveDataModel()` → nullptr 반환 권고 | **소멸** (훅 자체 제거) | 위 항목과 동일 사유 |
| §6-3 "파트 A 범위 초과" | **"해결됨 (길 2)"**로 상태 변경, 길 2가 어느 단계에서 처리되는지 §5로 가리킴 | — |

**용어 주의 (★ 코드 설계 불변식):** 노드는 asset을 **hold (ref)** 한다. **own 아니다.**
- `const UAnimSequence*` / `const UAnimDataModel*` 를 raw pointer로만 보유.
- `unique_ptr`/`shared_ptr`로 감싸지 않음. 노드 소멸자가 asset 수명에 관여하지 않음.
- `TrackToBoneIndex`는 값(`TArray<int32>`) 보유 — 캐시이지 외부 asset이 아님.

**전제 (스캔 + 사용자 결정으로 확정, 길 2 후에도 불변):**
- 출력 형식: `TArray<FTransform>` (시그니처 전환).
- StateMachine 컨테이너: 신규 `UAnimStateMachineInstance` (`UObjectManager::CreateObject<T>()`).
- Blend 산술: TRS+Slerp, 공용 free function `BlendTransform`.
- Notify dispatch / Lua 바인딩: 파트 B 대기.

**근거 문서:** `Document/animation_partA_infra_scan_result.md`

---

## 1. 보강 확인 결과

v2 개정에 직결되는 항목을 코드로 재확인한 결과 (v1 §1 위에 길 2용 추가 확인 합본).

### 1.1 ApplyEvaluatedPose / FK / 스킨 (v1과 동일 — 길 2 무영향)

- `USkinnedMeshComponent::ApplyEvaluatedPose(const TArray<FMatrix>&)` — `SkinnedMeshComponent.h:60`, `SkinnedMeshComponent.cpp:440-470`. `BoneOverrideMask`(`SkinnedMeshComponent.h:77`) 적용.
- `RebuildMeshSpaceBoneMatrices()` — `SkinnedMeshComponent.cpp:344-375`. FK 누적은 `FMatrix` 곱셈(`SkinnedMeshComponent.cpp:371-373`).
- `SkinVerticesToReferencePose()` — `SkinnedMeshComponent.cpp:377-438`. 스킨도 `FMatrix` 곱셈(`SkinnedMeshComponent.cpp:416`).
- 호출 경로: `SkeletalMeshComponent.cpp:141, 155`.
- 수동 본 편집: `SetBoneLocalPose(int32, const FMatrix&)` — `SkinnedMeshComponent.h:29`.

**함의 (v1과 동일):** `FTransform`→`FMatrix` 변환 경계 = `ApplyEvaluatedPose` 진입 1지점.

### 1.2 인스턴스 생성 경로 (v1과 동일)

- `SkeletalMeshComponent.cpp:44, 66, 106` 모두 `UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this)`.
- 소멸자: `SkeletalMeshComponent.cpp:26-33`.

### 1.3 `UObjectManager::CreateObject<T>` (v1과 동일)

- `Object/Object.h:120-133`. `DECLARE_CLASS` + `IMPLEMENT_CLASS` 필수, 기본 생성자 필요. raw `T*` 반환.

### 1.4 `USkeleton` 본 트리 API (v1과 동일)

- `Asset/Animation/Core/Skeleton.h:23-47`. `GetBones()` → `const TArray<FBoneInfo>&`. `FindBoneIndexByName(const FString&)` → `int32`. `FBoneInfo::ParentIndex`를 직접 읽음.

### 1.5 `FTransform` API (v1과 동일)

- `Transform.h:6-29`. T/R/S 보유. `Lerp`/`Identity`/`operator*` 모두 부재.

### 1.6 ★ 길 2 보강 확인 (v2 신규)

#### 1.6.1 `RebuildTrackToBoneIndex` / `TrackToBoneIndex` 호출처 전수

`Animation/` + `Component/` 전 범위 grep 결과 **추가 호출처 없음**. v1에서 알려진 4건이 전부:
- `AnimInstance.cpp:23` (`UAnimInstance::InitializeAnimation`)
- `AnimInstance.cpp:112-129` (함수 정의)
- `AnimSingleNodeInstance.cpp:14` (`SetAnimation`)
- `AnimSingleNodeInstance.cpp:25` (`InitializeAnimation` 안전망)

**처리 분류:**
- 호출 3건: **모두 단순 제거** (로직은 노드의 `SetSequence`로 이관됨).
- 함수 정의: **삭제**.

#### 1.6.2 `FAnimEvalContext`의 길 2 제거 대상 필드 사용처

- `Ctx.DataModel`: 호출자 2곳(`AnimInstance.cpp:105`, `AnimSingleNodeInstance.cpp:46`) + SequencePlayer(`AnimGraph.cpp:47`).
- `Ctx.TrackToBoneIndex`: 호출자 2곳(`AnimInstance.cpp:106`, `AnimSingleNodeInstance.cpp:47`) + SequencePlayer(`AnimGraph.cpp:68, 72`).
- `Ctx.Sequence`: **선언만 있고 read/write 0건**.

**처리:**
- 호출자 2곳의 `Ctx.DataModel = ...; Ctx.TrackToBoneIndex = &...;` 줄: **삭제**.
- SequencePlayer 내부의 `Ctx.DataModel` / `Ctx.TrackToBoneIndex` 참조: `this->DataModel` / `this->TrackToBoneIndex`로 **치환**.
- `FAnimEvalContext` 정의(`AnimGraph.h:22-29`)에서 세 필드 **제거**.

#### 1.6.3 노드 캐시 빌드용 API

- `UAnimSequence::IsValidSequence()` — `AnimSequence.h:107-111`. `DataModel && DataModel->GetPlayLength() > 0.0f && !DataModel->GetBoneAnimationTracks().empty()`. **노드의 `SetSequence` 가드로 활용 권고.**
- `UAnimDataModel::GetBoneAnimationTracks()` — `AnimSequence.h:37`. 캐시 빌드 시 트랙 이름 순회용.
- `UAnimSequenceBase::GetPlayLength()` — `AnimSequence.h:76-79`. StateMachine의 `SubLength` 자동 조회용(§4.4 개정).

#### 1.6.4 `GetActiveDataModel()` 호출처 — 단독 데드 코드 됨

`UAnimInstance::GetActiveDataModel()`(`AnimInstance.h:81`)의 유일 호출처는 `UAnimInstance::RebuildTrackToBoneIndex`(`AnimInstance.cpp:116`). 길 2가 `RebuildTrackToBoneIndex`를 제거하면 `GetActiveDataModel`은 **유실 호출처 없이 데드 코드**가 된다.

**처리:** base 가상 훅(`AnimInstance.h:81`)과 SingleNode override(`AnimSingleNodeInstance.h:31`, `.cpp:63-66`) 모두 **제거**. Notify 디스패치(파트 B)는 `GetActiveNotifies` 경로를 쓰므로 이 훅과 무관.

### 1.7 `[미확인]` (v2 신규 확인 후)

- `FTransform::operator*` 외부 정의 가능성 (v1과 동일, "없다" 전제).
- `AnimationMode` 변경 시 인스턴스 누수 (v1과 동일, 범위 외).

---

## 2. 공통 기반 변경

### 2.0 ★ 길 2 — SequencePlayer 노드 field화 (v2 신규)

#### 2.0.1 `FAnimGraphNode_SequencePlayer` 신규 인터페이스

```cpp
// Asset/Animation/Core/AnimGraph.h 변경 후
struct FAnimGraphNode_SequencePlayer : FAnimGraphNode_Base
{
    /**
     * 평가에 필요한 입력 시퀀스를 set하고 본 인덱스 캐시를 빌드한다.
     * Skeleton 또는 InSequence가 nullptr / invalid면 캐시는 비워둠(평가 시 bind pose).
     * @param Skeleton  ref. nullptr 허용.
     * @param InSequence ref. nullptr 허용. 노드는 이 포인터를 own하지 않는다.
     */
    void SetSequence(const USkeleton* Skeleton, const UAnimSequence* InSequence);

    void Evaluate(const FAnimEvalContext& Ctx, TArray<FTransform>& OutLocalPose) override;

    // ★ 모두 ref (raw pointer). 노드 소멸자가 해제하지 않는다.
    const UAnimSequence*  Sequence  = nullptr;
    const UAnimDataModel* DataModel = nullptr; // Sequence->GetDataModel() 캐시
    TArray<int32>         TrackToBoneIndex;    // 값 보유 (캐시)
};
```

**DataModel 캐시 결정 — "Sequence만 두고 매번 조회" vs "DataModel도 field"** :

| 안 | 비용 | 일관성 |
|---|---|---|
| (α) `Sequence`만 보유, 매 평가 `Sequence->GetDataModel()` 호출 | 매 프레임 가상-아님 inline getter 1회 (`AnimSequence.h:72`) — 사실상 0 | Sequence 교체 시 정합성 자동 |
| (β) `Sequence`+`DataModel` 둘 다 field, `SetSequence`에서 함께 갱신 | 갱신 1회만, 평가 hot path는 멤버 직접 접근 | `Sequence`와 `DataModel`의 두 ref가 동기화돼야 — `SetSequence` 외 경로로 누가 둘을 따로 건드리면 깨짐 |

**권고: (β) 둘 다 field로 캐시.** 근거: `SetSequence`가 유일 진입점으로 두 ref를 동시 갱신하면 동기화 우려는 자체 봉인된다. 평가 hot path가 SequencePlayer만이 아니라 향후 BlendSpace 등 다른 시퀀스 보유 노드에서도 같은 패턴을 쓰게 될 가능성이 높아 일관성 있는 캐시 패턴을 미리 정착시키는 게 좋다. (α)는 모양은 깔끔하나 노드별 캐시 패턴이 헐겁다.

#### 2.0.2 `SetSequence` 구현 의사코드

```cpp
void FAnimGraphNode_SequencePlayer::SetSequence(const USkeleton* InSkeleton, const UAnimSequence* InSequence)
{
    Sequence  = InSequence;
    DataModel = (InSequence ? InSequence->GetDataModel() : nullptr);

    TrackToBoneIndex.clear();

    if (!Sequence || !Sequence->IsValidSequence() || !InSkeleton) return; // 캐시 빌드 불가 — 평가 시 bind pose

    const TArray<FBoneAnimationTrack>& Tracks = DataModel->GetBoneAnimationTracks();
    TrackToBoneIndex.resize(Tracks.size());
    for (size_t TIdx = 0; TIdx < Tracks.size(); ++TIdx)
    {
        const FString BoneName = Tracks[TIdx].Name.ToString();
        TrackToBoneIndex[TIdx] = InSkeleton->FindBoneIndexByName(BoneName); // -1이면 트랙 무시
    }
}
```

이는 기존 `UAnimInstance::RebuildTrackToBoneIndex`(`AnimInstance.cpp:112-129`)의 로직을 **노드 내부로 이관**한 형태. 알고리즘 동일, 데이터 출처가 `GetActiveDataModel()`/`Skeleton` → `Sequence->GetDataModel()`/명시 `InSkeleton`으로 바뀐 것뿐.

#### 2.0.3 `FAnimEvalContext` 신규 형태

```cpp
struct FAnimEvalContext
{
    const USkeleton* Skeleton       = nullptr;
    float            TimeSeconds    = 0.0f; // "현재 노드가 평가해야 할 로컬 시간"으로 의미 재정의
    float            DeltaTime      = 0.0f; // §4.3 dt 도입
    class UAnimInstance* OwningInstance = nullptr; // §4.6 BoolVariable 평가용 (forward decl)
};
```

`DataModel`/`Sequence`/`TrackToBoneIndex` 3필드 **삭제**.

**`TimeSeconds`의 의미 재정의:** 길 2 이전엔 "인스턴스의 글로벌 재생 시간"이었으나, 길 2 이후엔 **"현재 평가 중인 노드 입장에서 자기 sub-graph가 봐야 할 시간"**으로 해석된다. SingleNode 경로에서는 두 의미가 동일(인스턴스의 `CurrentTime` 그대로). StateMachine 경로에서는 `StateLocalTimes[i]`로 패치된 값(§4.4).

#### 2.0.4 `UAnimSingleNodeInstance` 변경

`UAnimSingleNodeInstance::SetAnimation`(`AnimSingleNodeInstance.cpp:11-16`):
```cpp
// 변경 후
void UAnimSingleNodeInstance::SetAnimation(UAnimSequence *InSequence)
{
    CurrentSequence = InSequence;
    SequencePlayer.SetSequence(Skeleton, CurrentSequence); // ★ 노드에 setting
    ResetTime();
}
```

`UAnimSingleNodeInstance::InitializeAnimation`(`AnimSingleNodeInstance.cpp:18-27`):
```cpp
// 변경 후 — base의 RebuildTrackToBoneIndex 호출이 사라졌으므로 안전망 패턴이 단순해짐
void UAnimSingleNodeInstance::InitializeAnimation(USkeleton *InSkeleton)
{
    UAnimInstance::InitializeAnimation(InSkeleton); // base는 RebuildTrackToBoneIndex 호출 안 함 (제거됨)
    if (CurrentSequence)
    {
        SequencePlayer.SetSequence(Skeleton, CurrentSequence); // ★ 스켈레톤 늦게 들어온 케이스 안전망
    }
}
```

`UAnimSingleNodeInstance::EvaluateGraph`(`AnimSingleNodeInstance.cpp:29-51`):
```cpp
// 변경 후 — Ctx에 DataModel/TrackToBoneIndex 채우는 줄 삭제
void UAnimSingleNodeInstance::EvaluateGraph()
{
    if (!Skeleton) { OutputLocalPose.clear(); return; }
    const size_t BoneCount = Skeleton->GetBones().size();
    if (OutputLocalPose.size() != BoneCount) OutputLocalPose.resize(BoneCount);

    FAnimEvalContext Ctx;
    Ctx.Skeleton       = Skeleton;
    Ctx.TimeSeconds    = CurrentTime;
    Ctx.DeltaTime      = LastDeltaTime; // §4.3
    Ctx.OwningInstance = this;          // §4.6 (SingleNode는 사용 안 하지만 일관성)

    SequencePlayer.Evaluate(Ctx, OutputLocalPose);
}
```

#### 2.0.5 `UAnimInstance` 변경

- `TArray<int32> TrackToBoneIndex;` (`AnimInstance.h:97`) **삭제**.
- `void RebuildTrackToBoneIndex();` 선언(`AnimInstance.h:87`)·구현(`AnimInstance.cpp:112-129`) **삭제**. 로직은 노드의 `SetSequence`로 이관됨.
- `virtual const UAnimDataModel* GetActiveDataModel() const` 훅(`AnimInstance.h:81`) **삭제**. SingleNode override(`AnimSingleNodeInstance.h:31`, `.cpp:63-66`)도 함께 **삭제**.
- `InitializeAnimation`(`AnimInstance.cpp:16-24`)에서 `RebuildTrackToBoneIndex()` 호출(line 23) **삭제**.
- `EvaluateGraph`(`AnimInstance.cpp:89-110`)의 `Ctx.DataModel = ...; Ctx.TrackToBoneIndex = ...;` 줄(105-106) **삭제**. `Ctx.DeltaTime = LastDeltaTime;`, `Ctx.OwningInstance = this;` 추가.
- `LastDeltaTime` 멤버 신규 (§4.3) — `Update(DeltaTime)`에서 저장.

#### 2.0.6 `FAnimGraphNode_SequencePlayer::Evaluate` 본문 수정

`AnimGraph.cpp:45-128`의 변경 요지:
- `const UAnimDataModel* Model = Ctx.DataModel;` → `const UAnimDataModel* Model = this->DataModel;`
- `if (!Ctx.TrackToBoneIndex) { ... }` → `if (TrackToBoneIndex.empty()) { ... }` (값 보유로 바뀌었으므로 empty 검사)
- `const TArray<int32>& Track2Bone = *Ctx.TrackToBoneIndex;` → `const TArray<int32>& Track2Bone = this->TrackToBoneIndex;`
- 그 외 보간·fallback 로직 동일.

### 2.1 `Evaluate` 시그니처 전환 (v1 §2 — 출력 형식 부분, 길 2와 직교)

```cpp
// 기존 (AnimGraph.h:37)
virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose) = 0;

// 변경 후
virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) = 0;
```

### 2.2 전체 영향 범위 (길 2 합본 — v1 표 갱신)

| # | 파일·라인 | 변경 요지 | 길 2 영향 |
|---|---|---|---|
| 1 | `AnimGraph.h:22-29` (`FAnimEvalContext`) | `DataModel`/`Sequence`/`TrackToBoneIndex` 3필드 삭제, `DeltaTime`/`OwningInstance` 추가 | **★ 길 2 핵심** |
| 2 | `AnimGraph.h:34-38` (`FAnimGraphNode_Base`) | `Evaluate` 출력 파라미터 `TArray<FTransform>&` | 시그니처 전환 |
| 3 | `AnimGraph.h:45-48` (`FAnimGraphNode_SequencePlayer`) | field 추가(`Sequence`/`DataModel`/`TrackToBoneIndex`) + `SetSequence` 메서드 + `Evaluate` 시그니처 | **★ 길 2 핵심** |
| 4 | `AnimGraph.h:53-63` (`AnimGraph`) | `Evaluate` 시그니처 | 시그니처 전환 |
| 5 | `AnimGraph.cpp:19-32` (`FillBindPose` 헬퍼) | `TArray<FTransform>` 채우도록 + 본 분해 | 시그니처 전환 |
| 6 | `AnimGraph.cpp:35-43` (`AnimGraph::Evaluate`) | 시그니처 일치 | 시그니처 전환 |
| 7 | `AnimGraph.cpp:45-128` (`SequencePlayer::Evaluate`) | `Ctx.DataModel`/`Ctx.TrackToBoneIndex` → `this->...`, 최종 출력 `FTransform` 직접 대입 | **★ 길 2 + 시그니처 전환** |
| 8 | `AnimGraph.cpp` 신규 | `SetSequence` 구현 — §2.0.2 의사코드 | **★ 길 2 핵심** |
| 9 | `AnimInstance.h:44` | `GetOutputLocalPose()` 반환형 `const TArray<FTransform>&` | 시그니처 전환 |
| 10 | `AnimInstance.h:81` | `GetActiveDataModel()` 가상 훅 **삭제** | **★ 길 2** |
| 11 | `AnimInstance.h:87` | `RebuildTrackToBoneIndex` 선언 **삭제** | **★ 길 2** |
| 12 | `AnimInstance.h:96` | `OutputLocalPose` 타입 `TArray<FTransform>` | 시그니처 전환 |
| 13 | `AnimInstance.h:97` | `TrackToBoneIndex` 멤버 **삭제** | **★ 길 2** |
| 14 | `AnimInstance.h` 신규 | `LastDeltaTime` 멤버 추가 + `bPaused` 시 0 세팅 책임 | dt 도입 |
| 15 | `AnimInstance.cpp:16-24` (`InitializeAnimation`) | `RebuildTrackToBoneIndex()` 호출 줄 삭제 | **★ 길 2** |
| 16 | `AnimInstance.cpp:26-87` (`Update`) | `DeltaTime` 인자를 `LastDeltaTime`에 저장(`bPaused` 시 0) | dt 도입 |
| 17 | `AnimInstance.cpp:89-110` (`EvaluateGraph`) | `Ctx.DataModel`/`Ctx.TrackToBoneIndex` 세팅 삭제, `DeltaTime`/`OwningInstance` 추가, `OutputLocalPose` 타입 변경 영향 | **★ 길 2 + 시그니처 전환 + dt** |
| 18 | `AnimInstance.cpp:112-129` | `RebuildTrackToBoneIndex` 함수 정의 **삭제** | **★ 길 2** |
| 19 | `AnimInstance.cpp:131-145` (`FillBindPose`) | `FBoneInfo::LocalBindPose` → `FTransform` 분해 | 시그니처 전환 |
| 20 | `AnimSingleNodeInstance.h:31` | `GetActiveDataModel` override 선언 **삭제** | **★ 길 2** |
| 21 | `AnimSingleNodeInstance.cpp:11-16` (`SetAnimation`) | `RebuildTrackToBoneIndex()` → `SequencePlayer.SetSequence(Skeleton, CurrentSequence)` | **★ 길 2** |
| 22 | `AnimSingleNodeInstance.cpp:18-27` (`InitializeAnimation`) | `RebuildTrackToBoneIndex()` → `SequencePlayer.SetSequence(...)` 안전망 | **★ 길 2** |
| 23 | `AnimSingleNodeInstance.cpp:29-51` (`EvaluateGraph`) | `Ctx.DataModel`/`Ctx.TrackToBoneIndex` 세팅 삭제, `DeltaTime`/`OwningInstance` 추가 | **★ 길 2 + dt** |
| 24 | `AnimSingleNodeInstance.cpp:63-66` (`GetActiveDataModel`) | override 구현 **삭제** | **★ 길 2** |
| 25 | `SkeletalMeshComponent.cpp:141, 155` | `ApplyEvaluatedPose` 호출 시그니처 변경 영향 (자동 일치) | 시그니처 전환 |
| 26 | `SkinnedMeshComponent.h:60` | `ApplyEvaluatedPose` 시그니처 `const TArray<FTransform>&` | 시그니처 전환 |
| 27 | `SkinnedMeshComponent.cpp:440-470` | `FTransform` → `FMatrix` 변환 후 `LocalBonePoseMatrices`에 기록 | 시그니처 전환 |

### 2.3 `FTransform → FMatrix` 변환 경계 (v1 §2.3과 동일)

변환은 `USkinnedMeshComponent::ApplyEvaluatedPose` 한 곳. `BoneOverrideMask`와의 정합성 유지. 의사코드는 v1과 동일하므로 생략 (백업 참조).

### 2.4 SequencePlayer 출력 변경 (v1 §2.4 + 길 2 입력 변경)

`AnimGraph.cpp:122-126` 변경 후:
```cpp
const FVector P = FVector::Lerp(Raw.PosKeys[FrameA],   Raw.PosKeys[FrameB],   Blend);
const FQuat   R = FQuat::Slerp (Raw.RotKeys[FrameA],   Raw.RotKeys[FrameB],   Blend);
const FVector S = FVector::Lerp(Raw.ScaleKeys[FrameA], Raw.ScaleKeys[FrameB], Blend);
OutLocalPose[BoneIdx] = FTransform(P, R, S); // .ToMatrix() 제거
```
`Raw`는 `this->DataModel->GetBoneAnimationTracks()[TIdx].InternalTrack`에서 얻음(루프 변수 변경 없음).

### 2.5 bind pose fallback — `FMatrix` → `FTransform` (v1 §2.5와 동일)

`BindPoseToTransform(M)` 헬퍼 (`AnimPoseUtils.h`):
```cpp
inline FTransform BindPoseToTransform(const FMatrix& M)
{
    return FTransform(M.GetLocation(), FQuat::FromMatrix(M), M.GetScale());
}
```
`Matrix.h:109-110`, `Quat.h:129` 활용.

### 2.6 SingleNode 비주얼 동치성 입증 (v2 갱신 — 길 2 추가 입증 필요)

길 2 후에도 SingleNode가 같은 키 샘플링 결과를 내는지의 입증 포인트:
- **데이터 흐름:** `SetAnimation(seq)` → `SequencePlayer.SetSequence(Skeleton, seq)` → 노드 field에 `Sequence`/`DataModel`/`TrackToBoneIndex` 채워짐. v1에서는 같은 데이터가 인스턴스 멤버에 채워졌고 `EvaluateGraph`에서 `Ctx`에 실어 노드로 전달됐다. **출처는 같고 운반 경로만 노드 내부로 이동**.
- **알고리즘:** `SequencePlayer::Evaluate`의 보간/fallback 로직은 줄 단위로 동일. 입력 출처만 `Ctx.X` → `this->X`로 바뀜.
- **결론:** 본별 TRS는 비트 동치. `FTransform`→`FMatrix` 변환이 SequencePlayer 내부에서 컴포넌트 진입점으로 옮겨지는 것 외에 차이 없음(시그니처 전환 §2.6 입증과 동일).

A1a(입력 소스 전환) / A1b(출력 형식 전환) 각각의 검증은 §5 참조.

---

## 3. Blend 노드 설계 (v1 §3과 동일 — 길 2 무영향)

Blend 노드는 시퀀스를 직접 보유하지 않고 자식 sub-graph를 호출만 한다(자식이 SequencePlayer라면 그 노드가 자체 field로 시퀀스를 보유). 따라서 길 2의 영향이 없다.

§3.1 인터페이스 스케치(`Blend2`/`BlendN`), §3.2 스크래치 정책(노드 멤버), §3.3 `BlendTransform` free function, §3.4 Blend2 의사코드, §3.5 BlendN 누적 Slerp 근사, §3.6 경계 케이스 — **모두 v1과 동일** (백업 참조).

---

## 4. State / Transition 및 `FAnimGraphNode_StateMachine`

### 4.1 데이터 구조 헤더 스케치 (v2 갱신 — `SubLengthHint` 자동 도출 가능)

```cpp
// Asset/Animation/Core/AnimGraph_StateMachine.h (신규)
struct FAnimState
{
    FName                                Name;
    std::unique_ptr<FAnimGraphNode_Base> Sub; // 보통 SequencePlayer 또는 Blend
    bool                                 bResetTimeOnEnter = true;
    bool                                 bLooping = true;
    /**
     * looping wrap에 쓸 길이. 0이면 wrap 안 함(clamp).
     * Sub가 SequencePlayer면 SetStateMachineGraph 시점에 자동으로 Sub->Sequence->GetPlayLength()로
     * 채워줄 수 있다(§4.4 참조). 사용자 명시 지정도 허용.
     */
    float                                SubLengthHint = 0.0f;
};

enum class EAnimTransitionConditionKind : uint8
{
    TimeElapsed,
    BoolVariable,
    OnNotify,    // 파트 B 대기
    Custom       // 파트 B 대기
};

struct FAnimTransitionCondition
{
    EAnimTransitionConditionKind Kind = EAnimTransitionConditionKind::TimeElapsed;
    float TimeThreshold = 0.0f;
    FName VarName;
    bool  bExpectedValue = true;
    FName NotifyName;
};

struct FAnimTransition
{
    int32                            FromStateIndex = -1;
    int32                            ToStateIndex   = -1;
    float                            BlendDuration  = 0.2f;
    TArray<FAnimTransitionCondition> Conditions;
};

struct FAnimGraphNode_StateMachine : FAnimGraphNode_Base
{
    TArray<FAnimState>      States;
    TArray<FAnimTransition> Transitions;
    int32                   InitialStateIndex = 0;

    void Evaluate(const FAnimEvalContext& Ctx, TArray<FTransform>& OutLocalPose) override;

  private:
    int32              ActiveStateIndex      = -1;
    int32              ActiveTransitionIndex = -1;
    float              TransitionElapsed     = 0.0f;
    TArray<float>      StateLocalTimes;
    TArray<FTransform> ScratchFrom;
    TArray<FTransform> ScratchTo;
};
```

### 4.2 `UAnimStateMachineInstance` 헤더 스케치 (v1과 거의 동일, `GetActiveDataModel` override 제거)

```cpp
class UAnimStateMachineInstance : public UAnimInstance
{
  public:
    DECLARE_CLASS(UAnimStateMachineInstance, UAnimInstance)

    UAnimStateMachineInstance();
    ~UAnimStateMachineInstance() override = default;

    void SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine> InRoot);

    // BoolVariables (파트 B Lua 바인딩 대상)
    void SetBoolVariable(const FName& Name, bool Value) { BoolVariables[Name] = Value; }
    bool GetBoolVariable(const FName& Name, bool Default = false) const;
    const TMap<FName, bool>& GetBoolVariables() const { return BoolVariables; }

    void InitializeAnimation(USkeleton* InSkeleton) override;

  protected:
    float                                 GetEffectivePlayLength() const override; // 0 반환 — Update의 시간 누적 비활성
    const TArray<FAnimNotifyEvent>       *GetActiveNotifies() const override;       // 파트 B
    // ★ GetActiveDataModel override 없음 — base에서 훅 자체가 제거됐으므로

  private:
    TMap<FName, bool> BoolVariables;
};
```

`BoolVariables` 저장 위치 결정(파생에 둠), 근거는 v1 §4.2와 동일.

### 4.3 `Ctx.DeltaTime` 도입 (v1 §4.3과 동일 — 길 2 후에도 유효)

- `FAnimEvalContext`에 `float DeltaTime = 0.0f` 추가(§2.0.3에 통합).
- `UAnimInstance` 측 `LastDeltaTime` 멤버에 `Update(dt)`에서 저장(`bPaused`면 0). `EvaluateGraph`에서 `Ctx.DeltaTime = LastDeltaTime;` (미해결 #2 권고 채택).
- 두 호출자(`AnimInstance.cpp:103-`, `AnimSingleNodeInstance.cpp:44-`) 모두 보강.

### 4.4 상태별 시간 — `StateMachine`이 `Ctx.TimeSeconds`만 패치 (v2 갱신 — 더 깨끗해짐)

**v1 결함 소멸:** v1 §4.4는 `Ctx`를 복사·패치할 때 `TimeSeconds`만 패치하고 `DataModel`은 패치하지 않았다. 길 2 이후 `DataModel`은 `Ctx`에 없으므로(노드 field) **패치 대상이 `TimeSeconds` 단일로 자연히 줄어든다**. 결함 자체가 소멸.

**구체 동작 (v1과 동일하지만 단순해짐):**
1. `StateLocalTimes[ActiveStateIndex] += Ctx.DeltaTime`
2. transition 진행 중이면 `StateLocalTimes[ToStateIndex] += Ctx.DeltaTime`
3. 자식 평가 시: `FAnimEvalContext ChildCtx = Ctx; ChildCtx.TimeSeconds = StateLocalTimes[i];` 후 `Sub->Evaluate(ChildCtx, ScratchX);`

**Looping wrap (v2 개선):** v1은 `SubLengthHint`를 에디터/사용자가 명시 지정한다고 했다. 길 2 후 sub가 SequencePlayer면 노드 field `Sequence`에서 `Sequence->GetPlayLength()`(`AnimSequence.h:76-79`)로 길이를 알 수 있다.

**정책:**
- `SetStateMachineGraph(...)` 호출 시점에 각 `FAnimState`에 대해, `Sub`가 `FAnimGraphNode_SequencePlayer`로 dynamic_cast되면 자동으로 `SubLengthHint = sub->Sequence->GetPlayLength()` 채움.
- 그 외(Blend 등 시퀀스 introspection 불가능한 sub-graph)는 사용자 명시 지정 또는 0(clamp 동작) 유지.
- `dynamic_cast`가 부담스러우면 `FAnimGraphNode_Base`에 `virtual float GetEstimatedDuration() const { return 0.0f; }` 같은 옵션 훅을 두는 안도 가능 — §6에 미해결로 기록.

### 4.5 protected 가상 훅 override (v2 갱신)

- **`GetActiveDataModel() const`:** v1에서 "nullptr 반환 권고"였으나 **v2는 base 훅 자체가 제거됨** (§2.0.5). override 자체가 사라짐.
- `GetEffectivePlayLength() const`: **0 반환**. v1과 동일.
- `GetActiveNotifies() const`: 파트 B 대기, **`nullptr` 반환**. v1과 동일.

### 4.6 StateMachine `Evaluate` 의사코드 (v1과 동일 — 길 2 후에도 알고리즘 무변경)

```cpp
void FAnimGraphNode_StateMachine::Evaluate(const FAnimEvalContext& Ctx, TArray<FTransform>& OutLocalPose)
{
    const size_t N = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
    if (States.empty() || N == 0) { OutLocalPose.clear(); return; }

    if (ActiveStateIndex < 0)
    {
        ActiveStateIndex = Clamp(InitialStateIndex, 0, (int32)States.size() - 1);
        StateLocalTimes.assign(States.size(), 0.0f);
    }

    // 1) 시간 누적 (paused는 호출자가 Ctx.DeltaTime=0으로 처리)
    StateLocalTimes[ActiveStateIndex] += Ctx.DeltaTime;
    ApplyLoopOrClamp(ActiveStateIndex);
    if (ActiveTransitionIndex >= 0)
    {
        const auto& T = Transitions[ActiveTransitionIndex];
        TransitionElapsed += Ctx.DeltaTime;
        StateLocalTimes[T.ToStateIndex] += Ctx.DeltaTime;
        ApplyLoopOrClamp(T.ToStateIndex);
    }

    // 2) transition 완료 판정
    if (ActiveTransitionIndex >= 0 && TransitionElapsed >= Transitions[ActiveTransitionIndex].BlendDuration)
    {
        ActiveStateIndex = Transitions[ActiveTransitionIndex].ToStateIndex;
        if (States[ActiveStateIndex].bResetTimeOnEnter) StateLocalTimes[ActiveStateIndex] = 0.0f;
        ActiveTransitionIndex = -1;
        TransitionElapsed = 0.0f;
    }

    // 3) 발화 조건 검사
    if (ActiveTransitionIndex < 0)
    {
        for (int32 i = 0; i < (int32)Transitions.size(); ++i)
        {
            const auto& T = Transitions[i];
            if (T.FromStateIndex != ActiveStateIndex) continue;
            if (EvaluateConditions(Ctx.OwningInstance, T.Conditions, StateLocalTimes[ActiveStateIndex]))
            {
                ActiveTransitionIndex = i;
                TransitionElapsed = 0.0f;
                if (States[T.ToStateIndex].bResetTimeOnEnter) StateLocalTimes[T.ToStateIndex] = 0.0f;
                break;
            }
        }
    }

    // 4) 자식 평가 + 합성
    auto MakeChildCtx = [&](int32 StateIdx) {
        FAnimEvalContext C = Ctx; C.TimeSeconds = StateLocalTimes[StateIdx]; return C;
    };

    if (ActiveTransitionIndex < 0)
    {
        auto Cx = MakeChildCtx(ActiveStateIndex);
        if (States[ActiveStateIndex].Sub) States[ActiveStateIndex].Sub->Evaluate(Cx, OutLocalPose);
        else FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);
    }
    else
    {
        ScratchFrom.resize(N); ScratchTo.resize(N); OutLocalPose.resize(N);
        const auto& T = Transitions[ActiveTransitionIndex];
        auto CxFrom = MakeChildCtx(T.FromStateIndex);
        auto CxTo   = MakeChildCtx(T.ToStateIndex);
        if (States[T.FromStateIndex].Sub) States[T.FromStateIndex].Sub->Evaluate(CxFrom, ScratchFrom);
        else FillBindPoseTransforms(Ctx.Skeleton, ScratchFrom);
        if (States[T.ToStateIndex].Sub) States[T.ToStateIndex].Sub->Evaluate(CxTo, ScratchTo);
        else FillBindPoseTransforms(Ctx.Skeleton, ScratchTo);

        const float Alpha = (T.BlendDuration > 0.0f) ? Clamp(TransitionElapsed / T.BlendDuration, 0.0f, 1.0f) : 1.0f;
        for (size_t i = 0; i < N; ++i)
            OutLocalPose[i] = BlendTransform(ScratchFrom[i], ScratchTo[i], Alpha);
    }
}
```

`EvaluateConditions`: `Ctx.OwningInstance`를 `dynamic_cast<UAnimStateMachineInstance*>`해 `BoolVariables` 조회. `TimeElapsed`/`BoolVariable` 두 케이스 평가. `OnNotify`/`Custom`은 항상 false(파트 B 대기).

---

## 5. 구현 순서와 의존성 (v2 — A1 → A1a/A1b 분리)

### 5.1 단계별 작업 표

| 단계 | 작업 | 선행 | 검증 마일스톤 | 분리선 |
|---|---|---|---|---|
| **A0** | `AnimPoseUtils.h` — `BlendTransform`, `FillBindPoseTransforms`, `BindPoseToTransform` | — | 단위 검사 | Blend 프롬프트 |
| **A1a** | **★ 길 2 입력 소스 전환.** `FAnimEvalContext`에서 `DataModel`/`Sequence`/`TrackToBoneIndex` 제거, `SequencePlayer` field화, `SetSequence` 구현, `UAnimInstance` 측 `TrackToBoneIndex`/`RebuildTrackToBoneIndex`/`GetActiveDataModel` 제거, `UAnimSingleNodeInstance` 측 setting 경로 갱신. **출력은 아직 `TArray<FMatrix>` 유지.** | A0 | **SingleNode 시퀀스 재생 비주얼 동치** (`Capoeira.fbm` 등 기존 테스트 에셋). 본별 행렬 dump 비교. | Blend 프롬프트 |
| **A1b** | **출력 형식 전환.** `FAnimGraphNode_Base::Evaluate` 출력 파라미터 `TArray<FTransform>&`. `SequencePlayer::Evaluate` 마지막 `.ToMatrix()` 제거. `OutputLocalPose`/`GetOutputLocalPose`/`FillBindPose`/`ApplyEvaluatedPose` 전파. | A1a | SingleNode 비주얼 동치 + 변환 경계가 `ApplyEvaluatedPose` 1지점에 한정됨을 코드 검사 | Blend 프롬프트 |
| **A2** | `FAnimGraphNode_Blend2` 구현 | A1b | alpha=0/0.5/1 비주얼 확인 | Blend 프롬프트 |
| **A3** | `FAnimGraphNode_BlendN` 구현 (누적 Slerp) | A2 | 3 자식 weight 변경 비주얼 + weight=0 fallback | Blend 프롬프트 |
| **B0** | `FAnimEvalContext::DeltaTime`/`OwningInstance` 호출자 보강 (A1a에서 필드 추가 이미 됐다면 호출자 채움만) | A1a | SingleNode 회귀 동치 | StateMachine 프롬프트 |
| **B1** | `FAnimState`/`FAnimTransitionCondition`/`FAnimTransition` 데이터 구조 + `AnimGraph_StateMachine.h` | B0 | 컴파일 통과 | StateMachine 프롬프트 |
| **B2** | `FAnimGraphNode_StateMachine` 구현 — TimeElapsed 조건만 우선 | B1, A2(공용 유틸) | 2-state TimeElapsed transition 비주얼 + BlendDuration alpha | StateMachine 프롬프트 |
| **B3** | `UAnimStateMachineInstance` + `BoolVariables` + `BoolVariable` 조건 활성화 | B2 | 외부 fixture로 `SetBoolVariable` → transition 발화 확인 | StateMachine 프롬프트 |
| **B4** | `USkeletalMeshComponent::AnimationMode` 분기 + `EAnimationMode::AnimationStateMachine` + `CreateObject<UAnimStateMachineInstance>` | B3 | StateMachine 모드 정상 + SingleNode 모드 회귀 없음 | StateMachine 프롬프트 |

### 5.2 A1a → A1b 순서 권고 근거

A1a → A1b 순이 회귀 격리에 유리하다. 근거:
- **A1a를 먼저:** 길 2(입력 소스 전환)는 데이터 출처를 노드로 옮기는 변경이며, 같은 알고리즘이 같은 데이터를 받는다. 출력 형식이 `FMatrix`로 동일하므로 시각·수치 비교 모두 직접 가능. 회귀 발생 시 원인이 "노드로의 setting 누락/순서 오류"로 좁혀짐.
- **A1b를 다음:** 시그니처만 바뀌는 변경. A1a가 통과한 상태에서 출력 타입만 `FTransform`으로 바꾸면, 회귀 발생 시 원인이 "변환 경계(`ApplyEvaluatedPose`)/bind pose 분해 오류"로 좁혀짐.
- **역순(b→a) 부적합:** 시그니처를 먼저 바꾸면 SingleNode 동치성 검증을 위한 `FMatrix` dump 비교에 매번 변환 경계 동작이 끼어들어 격리가 어렵다.

### 5.3 분리선

- **Blend 구현 프롬프트:** A0 ~ A3. **길 2(A1a)가 포함**됨에 주의 — Blend 프롬프트가 SequencePlayer 노드 field화와 `UAnimInstance`/`SingleNode` 측 캐시 제거까지 모두 처리한다.
- **StateMachine 구현 프롬프트:** B0 ~ B4.

### 5.4 회귀 보장 체크포인트

- A1a 종료: SingleNode 비주얼·dump 동치. 노드 field에 정확한 `Sequence`/`DataModel`/`TrackToBoneIndex`가 setting됨을 디버거로 확인.
- A1b 종료: SingleNode 비주얼 동치. `FTransform→FMatrix` 변환이 `ApplyEvaluatedPose` 외에서 일어나지 않음을 grep으로 검증.
- A2/A3 종료: Blend graph만 영향. SingleNode 회귀 없음.
- B2 이후: SingleNode ↔ StateMachine 모드 전환 시 컴포넌트 정상.

---

## 6. 미해결 결정 사항 (v2 갱신)

| # | 항목 | 영향 | 비고 |
|---|---|---|---|
| 1 | `FAnimEvalContext::OwningInstance` 추가 | §4.6 BoolVariable 평가 | **권고 채택 — v2에서 §2.0.3에 포함됨.** SingleNode 동작 무영향. |
| 2 | `bPaused` 시 `Ctx.DeltaTime=0` 세팅 책임 | §4.5, B0 | **호출자(`UAnimInstance::EvaluateGraph` 직전)가 `LastDeltaTime`에 0 저장.** |
| 3 | ~~SequencePlayer 노드별 트랙→본 캐시~~ | ~~§4.5~~ | **해결됨 (길 2). v1 미룸 → v2 A1a에서 처리.** |
| 4 | `OnNotify` 조건 평가 | §4.1 enum, B2 이후 | **파트 B 대기.** |
| 5 | `BoolVariables`의 Lua 바인딩 | §4.2 | **파트 B 대기.** 시그니처만 노출. |
| 6 | `FAnimGraphNode_*` 직렬화 / 에디터 graph 정의 포맷 | 전반 | 범위 외. 외부 코드 주입 전제. |
| 7 | 런타임 중 `AnimationMode` 변경 시 기존 인스턴스 해제 | B4 | 범위 외 버그. 별도 작업으로 분리. |
| 8 | **(v2 신규)** `FAnimGraphNode_Base::GetEstimatedDuration()` 옵션 훅 도입 여부 | §4.4 (StateMachine이 `SubLengthHint`를 sub-graph에서 자동 도출) | dynamic_cast 회피용. 현재는 dynamic_cast 또는 사용자 명시로 충분. 후속 BlendSpace 등 다른 길이-있는 노드 도입 시 정식 검토. |

---

## 7. 자평 — 이 개정 계획서로 충분한가

v2는 v1 §6-3 미룸을 길 2로 종결하고, 그에 따라 (a) `FAnimEvalContext`의 3필드 제거, (b) `FAnimGraphNode_SequencePlayer`의 field 보유와 `SetSequence` 도입, (c) `UAnimInstance`의 `TrackToBoneIndex`/`RebuildTrackToBoneIndex`/`GetActiveDataModel` 삭제, (d) v1 §4.4의 "DataModel 미패치" 결함 해소, (e) §5 A1을 A1a/A1b 두 하위 단계로 분리해 회귀 격리 — 까지 구현 프롬프트가 코드를 쓸 수 있는 수준으로 정리됐다. own↔hold 구분이 §0와 §2.0.1에 명확히 못박혀 있다. v1에서 소멸한 항목(§4.5의 `GetActiveDataModel→nullptr`, §6-3의 미룸)은 "v1에서 이러했으나 v2 길 2로 해소됨"으로 추적성을 유지한다. 새 미해결 #8(옵션 훅)은 파트 A 진행을 막지 않으며 후속 검토 대상으로 분리. **추가 스캔 없이 구현 프롬프트 분리(Blend / StateMachine)로 곧장 진입 가능한 상태.**
