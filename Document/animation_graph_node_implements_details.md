# 파트 A — A1a/A1b/A2/A3/B0/B1/B2/B3 (완료) + B4 구현 계획

> **A1a~B2 상태:** 완료. 아래 §A1a~§B2로 원문 보존.
> **B3 상태:** 완료 (`UAnimStateMachineInstance` + `BoolVariable` 활성화, 단 `TMap`→`std::unordered_map` 직접 사용으로 plan에서 변경, Engine 풀 rebuild 오류 0건). 아래 §B3로 원문 보존.
> **현재 단계:** B4 — `EAnimationMode::AnimationStateMachine` 추가 + 컴포넌트 `AnimInstance` 생성 경로를 mode 분기로 라우팅.

---

## §A1a (완료) — 원문 보존

### Context

`Document/animation_partA_blend_statemachine_plan.md` (v2 플랜) §5의 첫 코드 변경 단계인 **A1a**를 실행한다. 이 단계의 목적은 향후 StateMachine 구현에서 각 상태가 서로 다른 시퀀스를 가질 수 있도록 **`FAnimGraphNode_SequencePlayer`가 자기 입력(`Sequence`/`DataModel`/`TrackToBoneIndex`)을 노드 필드로 hold**하게 만드는 것이다. 현재는 stateless 노드가 매 평가마다 `FAnimEvalContext`에서 입력을 받고, `UAnimInstance` 측이 인스턴스 단위 캐시 하나만 들고 있어 노드 단위 분기가 불가능하다.

**A1a 후 출력 형식은 여전히 `TArray<FMatrix>`로 유지**한다 (`FMatrix → FTransform` 시그니처 전환은 A1b의 책임). 이렇게 분리해야 A1a 회귀 검증 시 `FMatrix` dump를 그대로 비교할 수 있어 원인 격리가 쉽다 (v2 §5.2 근거).

**용어 불변식 (v2 §0):** 노드는 asset을 **hold(ref)**한다 — own 아니다. raw `const T*`만 보유, `unique_ptr/shared_ptr`로 감싸지 않는다.

## Scope

**포함:**
- `FAnimEvalContext`에서 `DataModel`/`Sequence`/`TrackToBoneIndex` 3필드 제거
- `FAnimGraphNode_SequencePlayer`에 필드 3개 + `SetSequence(Skeleton, Sequence)` 추가
- `FAnimGraphNode_SequencePlayer::Evaluate` 본문의 입력 출처를 `Ctx.*` → `this->*`로 치환
- `UAnimInstance`의 `TrackToBoneIndex` 멤버, `RebuildTrackToBoneIndex` 선언/정의, `GetActiveDataModel` 가상 훅 제거
- `UAnimSingleNodeInstance`의 `SetAnimation`/`InitializeAnimation`/`EvaluateGraph` 갱신, `GetActiveDataModel` override 제거

**제외 (별도 단계):**
- A1b: `Evaluate` 출력형 `TArray<FMatrix>&` → `TArray<FTransform>&` 전환
- A0: `AnimPoseUtils.h` — A1a에 불필요 (출력이 여전히 `FMatrix`이므로 `BlendTransform`/`BindPoseToTransform`/`FillBindPoseTransforms` 호출자 0)
- B0: `Ctx.DeltaTime`, `Ctx.OwningInstance` 추가 — StateMachine 프롬프트에서 처리
- A2/A3 (Blend), B1~B4 (StateMachine)

## Critical files

1. [KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h)
2. [KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp)
3. [KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h)
4. [KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp)
5. [KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h)
6. [KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp)

## Reused APIs (변경 없이 활용)

- `UAnimSequence::IsValidSequence()` — `AnimSequence.h:107-111`. `SetSequence`의 캐시 빌드 가드.
- `UAnimSequence::GetDataModel()` — `AnimSequence.h:72` 부근. inline getter, hot path 비용 0.
- `UAnimDataModel::GetBoneAnimationTracks()` — `AnimSequence.h:37`. 트랙 이름 순회.
- `USkeleton::FindBoneIndexByName(const FString&)` — `Skeleton.h:43`. 트랙→본 인덱스 매핑.
- `FBoneAnimationTrack::Name.ToString()` — 기존 `UAnimInstance::RebuildTrackToBoneIndex`(`AnimInstance.cpp:126`)와 동일 패턴.

## 파일별 변경

### 1. [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h)

- **22-29 `FAnimEvalContext`:** `DataModel`/`Sequence`/`TrackToBoneIndex` 3필드 **삭제**. 결과는 `Skeleton`/`TimeSeconds`만 남는 형태. (B0에서 `DeltaTime`/`OwningInstance` 추가 예정 — 이번 단계에는 추가하지 않음.)
- **45-48 `FAnimGraphNode_SequencePlayer`:** 다음 필드 + 메서드 추가.
  ```cpp
  void SetSequence(const USkeleton* InSkeleton, const UAnimSequence* InSequence);
  const UAnimSequence*  Sequence  = nullptr;
  const UAnimDataModel* DataModel = nullptr;
  TArray<int32>         TrackToBoneIndex;
  ```

### 2. [AnimGraph.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp)

- **47-72 `SequencePlayer::Evaluate` 본문:**
  - line 47 `Ctx.DataModel` → `this->DataModel`
  - line 68 `if (!Ctx.TrackToBoneIndex)` → `if (TrackToBoneIndex.empty())` (값 보유로 바뀜)
  - line 72 `const TArray<int32> &Track2Bone = *Ctx.TrackToBoneIndex;` → `const TArray<int32> &Track2Bone = this->TrackToBoneIndex;`
- **신규 정의 추가:** `FAnimGraphNode_SequencePlayer::SetSequence` (v2 §2.0.2 의사코드). 알고리즘은 기존 `UAnimInstance::RebuildTrackToBoneIndex`(`AnimInstance.cpp:112-129`)와 동일하되 데이터 출처가 `GetActiveDataModel()`/멤버 `Skeleton` → 인자 `InSequence->GetDataModel()`/인자 `InSkeleton`으로 바뀜. `IsValidSequence` 가드 추가.

### 3. [AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h)

- **78-81 `GetActiveDataModel()` 가상 훅 삭제** (`UAnimDataModel` 전방 선언이 다른 곳에 쓰이지 않으면 함께 정리).
- **83-87 `RebuildTrackToBoneIndex()` 선언 삭제** (주석 포함).
- **97 `TArray<int32> TrackToBoneIndex;` 멤버 삭제**.
- **30 주석 갱신:** "OutputLocalPose / TrackToBoneIndex 캐시를 초기화한다" → "OutputLocalPose를 초기화한다" (TrackToBoneIndex 언급 제거).

### 4. [AnimInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp)

- **23 `RebuildTrackToBoneIndex();` 호출 삭제** (`InitializeAnimation` 안).
- **105-106 두 줄 삭제:** `Ctx.DataModel = GetActiveDataModel();` / `Ctx.TrackToBoneIndex = &TrackToBoneIndex;`. (남는 세팅: `Ctx.Skeleton`, `Ctx.TimeSeconds`)
- **112-129 `RebuildTrackToBoneIndex` 정의 전체 삭제**.

### 5. [AnimSingleNodeInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h)

- **31 `GetActiveDataModel` override 선언 삭제** (`UAnimDataModel` forward decl 정리 검토).

### 6. [AnimSingleNodeInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp)

- **11-16 `SetAnimation`:** `RebuildTrackToBoneIndex();` (line 14) → `SequencePlayer.SetSequence(Skeleton, CurrentSequence);`.
- **18-27 `InitializeAnimation`:** line 21-22 주석을 "base에서 RebuildTrackToBoneIndex 호출이 사라졌으므로 시퀀스가 이미 set인 상태에서 스켈레톤이 늦게 들어오는 경우 노드에 직접 setting" 류로 갱신. line 25 `RebuildTrackToBoneIndex();` → `SequencePlayer.SetSequence(Skeleton, CurrentSequence);`.
- **44-48 `EvaluateGraph`:** line 46-47 두 줄 (`Ctx.DataModel`/`Ctx.TrackToBoneIndex` 세팅) 삭제.
- **63-66 `GetActiveDataModel` override 정의 삭제**.

## 실행 순서

전 호출처가 동일 변경 세트에 묶여 있어 부분 빌드는 불가능. 다음 한 번의 일괄 변경 후 풀 빌드:

1. AnimGraph.h — 컨텍스트 슬림화 + 노드 필드 추가
2. AnimGraph.cpp — Evaluate 내부 치환 + SetSequence 정의
3. AnimInstance.h — 멤버/선언 3건 삭제
4. AnimInstance.cpp — 호출/세팅/정의 3건 삭제
5. AnimSingleNodeInstance.h — override 선언 삭제
6. AnimSingleNodeInstance.cpp — SetSequence 경로 갱신 + override 정의 삭제

## Verification

**컴파일:** 풀 빌드 통과. (KraftonEngine 빌드 명령은 사용자 환경 확인 필요.)

**런타임 회귀 (SingleNode 비주얼 동치):** v2 §2.6 입증 기준.
- 에디터에서 기존 Mixamo 시퀀스(예: `Asset/FBX/MixamoFBX/Capoeira.fbm`) 재생.
- 본별 transform 출처가 같고 알고리즘이 같으므로 **비트 동치**가 기대됨. 시각적으로 변화 없음을 확인.

**static 검증 (변경 누락 방지):** Grep으로 다음이 0건임을 확인.
- `RebuildTrackToBoneIndex`
- `GetActiveDataModel`
- `UAnimInstance::TrackToBoneIndex` (인스턴스 멤버; 노드 필드는 OK)
- `Ctx.DataModel`, `Ctx.Sequence`, `Ctx.TrackToBoneIndex`

**디버거 확인 (선택):** `UAnimSingleNodeInstance::SetAnimation` 후 `SequencePlayer.Sequence`/`DataModel`/`TrackToBoneIndex.size()`에 정확한 값이 들어갔는지 한 번 확인.

## Out of scope / 다음 단계

- **A1b:** `Evaluate` 출력형 전환 (`FMatrix` → `FTransform`). 호출 경계 `USkinnedMeshComponent::ApplyEvaluatedPose`도 함께.
- **A0:** `AnimPoseUtils.h` (`BlendTransform`/`FillBindPoseTransforms`/`BindPoseToTransform`) — A2 Blend 단계 직전에 추가.
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §A1b — 출력형 `TArray<FTransform>` 전환

### Context

A1a로 노드가 자기 입력을 hold하게 되었지만 출력은 여전히 `TArray<FMatrix>`이다. **A1b의 목적은 노드 평가 결과를 `TArray<FTransform>`으로 산출하고, `FTransform → FMatrix` 변환을 `USkinnedMeshComponent::ApplyEvaluatedPose` 한 지점으로 일원화**하는 것이다 (v2 §2.3). 이로써 (a) 향후 Blend 노드가 TRS+Slerp 합성을 손쉽게 표현하고, (b) `FMatrix` 변환이 매 평가마다 일어나지 않는 깔끔한 경계를 얻는다.

A1a와 분리하는 이유는 회귀 격리(v2 §5.2): 출력 타입이 그대로일 때 입력 출처 전환만 비주얼·비트 비교로 검증할 수 있고, 이후 출력 타입만 단독으로 바꿔 변환 경계 오류를 격리할 수 있다.

### Scope

**포함:**
- 신규 헤더 `Asset/Animation/Core/AnimPoseUtils.h` 도입 (A0의 부분 도입):
  - `BindPoseToTransform(const FMatrix&) -> FTransform`
  - `FillBindPoseTransforms(const USkeleton*, TArray<FTransform>&)`
  - (BlendTransform은 A2에서 추가 — 이번엔 도입하지 않음)
- `FAnimGraphNode_Base::Evaluate` / `AnimGraph::Evaluate` / `FAnimGraphNode_SequencePlayer::Evaluate` 출력 파라미터 `TArray<FMatrix>&` → `TArray<FTransform>&`
- `FAnimGraphNode_SequencePlayer::Evaluate` 최종 대입에서 `.ToMatrix()` 제거 — `OutLocalPose[BoneIdx] = FTransform(P, R, S);`
- AnimGraph.cpp 익명 namespace `FillBindPose` 헬퍼 제거 → `AnimPoseUtils::FillBindPoseTransforms` 호출로 대체
- `UAnimInstance::OutputLocalPose` 타입 `TArray<FMatrix>` → `TArray<FTransform>`, `GetOutputLocalPose()` 반환형 일치
- `UAnimInstance::FillBindPose` 멤버 함수 본문: `OutputLocalPose[i] = Bones[i].LocalBindPose;` → `BindPoseToTransform(Bones[i].LocalBindPose)` (또는 `AnimPoseUtils::FillBindPoseTransforms(Skeleton, OutputLocalPose)`로 단순화)
- `USkinnedMeshComponent::ApplyEvaluatedPose` 시그니처 `const TArray<FMatrix>&` → `const TArray<FTransform>&`, 본문에서 `EvaluatedLocalPose[i].ToMatrix()`로 변환 후 `LocalBonePoseMatrices`에 기록 (BoneOverrideMask 적용 로직 보존)
- include 정리: 새 타입으로 인해 더 이상 필요 없게 된 `Math/Matrix.h` 제거 (예: AnimGraph.h, AnimGraph.cpp, AnimInstance.h), `Math/Transform.h` 추가

**제외 (별도 단계):**
- `BlendTransform` 함수 — A2에서 도입
- B0 `Ctx.DeltaTime`/`OwningInstance` 추가 — StateMachine 단계
- `SetBoneLocalPose(int32, const FMatrix&)` 시그니처 — 외부 호출처 없음(검증 결과), A1b 범위 외 유지
- `LocalBonePoseMatrices` 멤버 타입 — `FMatrix` 유지 (FK가 행렬 곱셈, v2 §1.1)

### Critical files

1. [KraftonEngine/Source/Engine/Asset/Animation/Core/AnimPoseUtils.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimPoseUtils.h) — 신규
2. [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h)
3. [AnimGraph.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp)
4. [AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h)
5. [AnimInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp)
6. [SkinnedMeshComponent.h](KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.h)
7. [SkinnedMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp)

[SkeletalMeshComponent.cpp:141, 155](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:141)는 `ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose())` 형태이므로 시그니처 변경 시 자동 일치 — 변경 불요.

### Reused APIs (변경 없이 활용)

- `FTransform(const FVector&, const FQuat&, const FVector&)` — `Math/Transform.h:13`
- `FTransform::ToMatrix()` — `Math/Transform.h:28`, 정의 `Math/Transform.cpp:3`
- `FQuat::FromMatrix(const FMatrix&)` — 정의 `Math/Quat.cpp:67`
- `FMatrix::GetLocation()` — 정의 `Math/Matrix.cpp:503`
- `FMatrix::GetScale()` — 정의 `Math/Matrix.cpp:509`
- `FBoneInfo::LocalBindPose` 타입 `FMatrix` — `Asset/Animation/Core/AnimationTypes.h:47`

### 파일별 변경

#### 1. [AnimPoseUtils.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimPoseUtils.h) — 신규

```cpp
#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

class USkeleton;

namespace AnimPoseUtils
{
    inline FTransform BindPoseToTransform(const FMatrix &M)
    {
        return FTransform(M.GetLocation(), FQuat::FromMatrix(M), M.GetScale());
    }

    void FillBindPoseTransforms(const USkeleton *Skeleton, TArray<FTransform> &OutLocalPose);
}
```

`FillBindPoseTransforms`는 `.cpp` 분리도 가능하지만 본 함수만 작아 inline 헤더 정의(또는 별도 `AnimPoseUtils.cpp` 추가) 모두 OK. **inline 헤더 정의** 권장 — 추가 빌드 단위 없이 끝남.

#### 2. AnimGraph.h

- **line 11 `#include "Math/Matrix.h"` 제거**, `#include "Math/Transform.h"` 추가.
- **line 37 `FAnimGraphNode_Base::Evaluate`:** `TArray<FMatrix> &` → `TArray<FTransform> &`.
- **`FAnimGraphNode_SequencePlayer::Evaluate` 동일.**
- **`AnimGraph::Evaluate` 동일.**

#### 3. AnimGraph.cpp

- **익명 namespace `FillBindPose` 헬퍼(lines 19-32) 삭제.** 호출처 2곳(`AnimGraph::Evaluate`의 Root nullptr fallback, `SequencePlayer::Evaluate`의 초기 채움/early-return fallback)을 `AnimPoseUtils::FillBindPoseTransforms(Skeleton, OutLocalPose)` 호출로 변경.
- **`AnimGraph::Evaluate` / `SequencePlayer::Evaluate` 시그니처** `TArray<FTransform>&`로.
- **`SequencePlayer::Evaluate` 최종 라인 (현재 line 147):** `OutLocalPose[BoneIdx] = FTransform(P, R, S).ToMatrix();` → `OutLocalPose[BoneIdx] = FTransform(P, R, S);`
- include: `Math/Matrix.h` 제거 가능 여부 확인(헬퍼 이관 후 cpp가 직접 FMatrix 안 씀), `Asset/Animation/Core/AnimPoseUtils.h` 추가.

#### 4. AnimInstance.h

- **line 12 `#include "Math/Matrix.h"` 제거**, `#include "Math/Transform.h"` 추가.
- **`OutputLocalPose` 멤버 타입** `TArray<FMatrix>` → `TArray<FTransform>` (line 84 부근).
- **`GetOutputLocalPose()` 반환형** `const TArray<FMatrix>&` → `const TArray<FTransform>&` (line 43 부근).

#### 5. AnimInstance.cpp

- **`FillBindPose` 멤버 함수 본문 단순화:** `AnimPoseUtils::FillBindPoseTransforms(Skeleton, OutputLocalPose)` 호출로 통합 (Skeleton nullptr 분기는 헬퍼 안에서 처리).
- **`EvaluateGraph`:** `OutputLocalPose.resize(BoneCount)` / `clear()`는 타입 변경에 따라 자동 일치 (TArray 인터페이스 동일).
- include: `Asset/Animation/Core/AnimPoseUtils.h` 추가. `Asset/Animation/Core/Skeleton.h`는 `FBoneInfo` 접근 사라지므로 제거 검토(헬퍼 안에서 처리되면 불필요).

#### 6. SkinnedMeshComponent.h

- **line 60 `ApplyEvaluatedPose` 시그니처** `const TArray<FMatrix>&` → `const TArray<FTransform>&`.
- include 정리: `Math/Transform.h` 추가, `Math/Matrix.h`는 `LocalBonePoseMatrices`(여전히 FMatrix)와 `SetBoneLocalPose` 시그니처 때문에 유지.

#### 7. SkinnedMeshComponent.cpp

- **`ApplyEvaluatedPose` 본문(lines 440-470):**
  - 입력 `EvaluatedLocalPose[i]`는 이제 `FTransform`.
  - `LocalBonePoseMatrices[i] = EvaluatedLocalPose[i].ToMatrix();` 형태로 변환하며 기록.
  - `BoneOverrideMask` 분기는 그대로 보존 — 사용자가 수동 편집한 본은 변환된 결과를 덮어쓰지 않는다.
- include: `Math/Transform.h` 추가.

### 실행 순서

부분 빌드 불가능, 일괄 변경 후 풀 빌드:

1. AnimPoseUtils.h 신규 작성
2. AnimGraph.h — Evaluate 3개 시그니처 변경
3. AnimGraph.cpp — 익명 namespace 헬퍼 제거, fallback을 AnimPoseUtils로, `.ToMatrix()` 제거
4. AnimInstance.h — OutputLocalPose / GetOutputLocalPose 타입 변경
5. AnimInstance.cpp — FillBindPose 본문을 AnimPoseUtils 호출로
6. SkinnedMeshComponent.h — ApplyEvaluatedPose 시그니처
7. SkinnedMeshComponent.cpp — ApplyEvaluatedPose 본문에서 `.ToMatrix()` 변환

### Verification

**컴파일:** `KraftonEngine.sln`을 MSBuild Debug x64로 풀 빌드. 오류 0건.

**런타임 회귀 (SingleNode 비주얼 동치):**
- 에디터에서 동일한 Mixamo 시퀀스(예: Capoeira) 재생.
- A1a 직후와 시각적 동치 기대 (FK 결과 = TRS Matrix 변환 결과, 비트 동치까지는 ToMatrix 부동소수 round-trip 때문에 보장 어려움 — **시각적 동치**만 보장).

**static 검증 (변환 경계 일원화 확인):** Grep으로 다음 패턴이 `SkinnedMeshComponent::ApplyEvaluatedPose` 외에서 나타나지 않음을 확인.
- `\.ToMatrix\(\)` — 애니메이션 평가 경로(`Asset/Animation/`, `Component/SkeletalMesh*`, `Component/SkinnedMesh*`) 안에서 `ApplyEvaluatedPose` 본문 외 0건이어야 함.
- `TArray<FMatrix>` — 위 경로에서 `LocalBonePoseMatrices` / `MeshSpaceBoneMatrices` / `SetBoneLocalPose` / `FBoneInfo::LocalBindPose` 등 의도된 유지 위치 외 0건.

**디버거 확인 (선택):** 한 프레임 stepping으로 `OutputLocalPose[0]`가 `FTransform`임을, `LocalBonePoseMatrices[0]`가 같은 본의 행렬임을 확인.

### Out of scope / 다음 단계

- **A2/A3 (Blend):** `BlendTransform` 추가 + `Blend2`/`BlendN` 노드. A1b 검증 통과 후 진행.
- **B0~B4 (StateMachine):** `Ctx.DeltaTime`/`OwningInstance` 도입과 함께 StateMachine 단계에서.
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §A2 — `FAnimGraphNode_Blend2` 도입

### Context

A1b로 노드 평가 출력이 `TArray<FTransform>`이 되어 본별 TRS 합성이 자연스러워졌다. **A2의 목적은 두 자식 sub-graph 포즈를 단일 `Alpha`로 본별 TRS 보간하는 `FAnimGraphNode_Blend2`를 도입**하는 것이다. 이는 (a) Blend 산술의 표준 경계를 정립하고, (b) 향후 `FAnimGraphNode_StateMachine` transition 합성이 같은 산술을 재사용할 토대를 만든다.

v1 백업 §3.1/§3.3/§3.4의 설계를 그대로 적용한다. BlendN(누적 Slerp 가중평균)은 별도 단계 A3로 분리 — 본 단계는 단일 `Alpha` 케이스만.

### Scope

**포함:**
- [AnimPoseUtils.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimPoseUtils.h)에 `BlendTransform(const FTransform&, const FTransform&, float Alpha) -> FTransform` inline free function 추가.
- [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h)에 `FAnimGraphNode_Blend2` 구조체 정의 추가 (필드: `ChildA`/`ChildB` `unique_ptr` + `Alpha` float + 내부 `ScratchA`/`ScratchB`).
- [AnimGraph.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp)에 `Blend2::Evaluate` 구현 추가.

**제외 (별도 단계):**
- A3 `FAnimGraphNode_BlendN` (누적 Slerp 가중평균)
- 별도 헤더 `AnimGraph_BlendNodes.h` 분리 — A3 도입 시 비대해지면 그때 분리. A2 단독으로는 `AnimGraph.h`에 합치는 게 변경 면적 최소(vcxproj 무변경).
- B0 `Ctx.DeltaTime`/`OwningInstance` 추가 — StateMachine 단계.
- Blend graph를 사용자에게 노출하는 에디터/Lua API — 범위 외 (graph는 코드로 직접 구성하는 전제).

### Critical files

1. [KraftonEngine/Source/Engine/Asset/Animation/Core/AnimPoseUtils.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimPoseUtils.h) — `BlendTransform` 추가
2. [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h) — `FAnimGraphNode_Blend2` 정의 추가
3. [AnimGraph.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp) — `Blend2::Evaluate` 구현 추가

[KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj)는 **무변경** (신규 cpp 없음).

### Reused APIs (변경 없이 활용)

- `FVector::Lerp` / `FQuat::Slerp` — 이미 `SequencePlayer::Evaluate`에서 사용 중. 시그니처 검증 완료.
- `FTransform(const FVector&, const FQuat&, const FVector&)` 생성자 — `Math/Transform.h:13`.
- `FTransform::Location` / `Rotation` / `Scale` public 멤버 — `Math/Transform.h:8-10`. 직접 접근 가능.
- `AnimPoseUtils::FillBindPoseTransforms` — A1b에서 도입됨. 자식이 `nullptr`이면 bind pose로 채우는 안전망.
- `USkeleton::GetBones()` — `Skeleton.h:40`. 본 개수 조회.
- `std::unique_ptr<FAnimGraphNode_Base>` 패턴 — 기존 `AnimGraph::Root`(`AnimGraph.h:73`)와 동일 관습.

### 파일별 변경

#### 1. AnimPoseUtils.h — `BlendTransform` 추가

기존 `namespace AnimPoseUtils` 안에 inline 함수 1개 추가:

```cpp
/**
 * 두 FTransform을 alpha(0..1)로 본별 TRS 보간한다.
 * - 위치/스케일: FVector::Lerp
 * - 회전: FQuat::Slerp (최단경로·정규화 포함)
 * Alpha는 호출자가 [0,1]로 클램프했다고 가정 — 노드 측에서 미리 처리.
 */
inline FTransform BlendTransform(const FTransform &A, const FTransform &B, float Alpha)
{
    return FTransform(
        FVector::Lerp(A.Location, B.Location, Alpha),
        FQuat::Slerp (A.Rotation, B.Rotation, Alpha),
        FVector::Lerp(A.Scale,    B.Scale,    Alpha)
    );
}
```

#### 2. AnimGraph.h — `FAnimGraphNode_Blend2` 정의 추가

`FAnimGraphNode_SequencePlayer` 정의 직후에 다음 블록 추가:

```cpp
/**
 * 두 자식 sub-graph의 포즈를 단일 alpha로 본별 TRS 보간하는 노드.
 * - ChildA / ChildB가 nullptr이면 bind pose로 안전 대체.
 * - Alpha는 [0,1]로 클램프하여 사용 (음수/외삽 차단).
 * - 스크래치는 노드 멤버로 보유 — 매 평가 할당 회피.
 */
struct FAnimGraphNode_Blend2 : FAnimGraphNode_Base
{
    std::unique_ptr<FAnimGraphNode_Base> ChildA;
    std::unique_ptr<FAnimGraphNode_Base> ChildB;
    float                                Alpha = 0.0f;

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

  private:
    TArray<FTransform> ScratchA;
    TArray<FTransform> ScratchB;
};
```

#### 3. AnimGraph.cpp — `Blend2::Evaluate` 구현 추가

파일 끝에 다음 추가 (v1 §3.4 의사코드 그대로):

```cpp
void FAnimGraphNode_Blend2::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    const size_t N = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
    if (N == 0)
    {
        OutLocalPose.clear();
        return;
    }

    OutLocalPose.resize(N);
    ScratchA.resize(N);
    ScratchB.resize(N);

    if (ChildA) ChildA->Evaluate(Ctx, ScratchA);
    else        AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ScratchA);

    if (ChildB) ChildB->Evaluate(Ctx, ScratchB);
    else        AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ScratchB);

    float A = Alpha;
    if (A < 0.0f) A = 0.0f;
    if (A > 1.0f) A = 1.0f;

    for (size_t i = 0; i < N; ++i)
    {
        OutLocalPose[i] = AnimPoseUtils::BlendTransform(ScratchA[i], ScratchB[i], A);
    }
}
```

include 정리: `AnimGraph.cpp`는 이미 `AnimPoseUtils.h` 와 `Skeleton.h` 를 포함하므로 추가 include 불필요.

### 실행 순서

1. AnimPoseUtils.h — `BlendTransform` 추가
2. AnimGraph.h — `FAnimGraphNode_Blend2` 정의 추가
3. AnimGraph.cpp — `Blend2::Evaluate` 구현 추가
4. 풀 빌드 (KraftonEngine 프로젝트)

### Verification

**컴파일:** `KraftonEngine.sln` 또는 `KraftonEngine\KraftonEngine.vcxproj` 풀 rebuild. 오류 0건, 경고 0건 기대.

**SingleNode 회귀:** Blend2 graph는 아직 어디서도 인스턴스화되지 않으므로 SingleNode 시퀀스 재생은 무영향. 사용자 측 시각 동치 확인 — A1b 직후와 동일한 결과 기대.

**Blend2 자체 동작 검증 (이번 단계 범위 외, 향후 단계):** 사용자가 graph를 구성해 alpha=0/0.5/1 비주얼 확인. 본 단계에서는 인스턴스화 경로가 없으므로 컴파일 통과 + SingleNode 회귀 없음으로 충분.

**static 검증:** Grep으로 `FAnimGraphNode_Blend2`가 정의(AnimGraph.h)와 구현(AnimGraph.cpp) 각 1건, 호출처 0건임을 확인.

### Out of scope / 다음 단계

- **A3:** `FAnimGraphNode_BlendN` — 누적 Slerp 가중평균. AnimGraph.h가 비대해지면 이 시점에 `AnimGraph_BlendNodes.h/cpp` 분리 검토(vcxproj 등록 포함).
- **B0~B4:** StateMachine.
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §A3 — `FAnimGraphNode_BlendN` 도입 (누적 Slerp 가중평균)

### Context

A2의 `FAnimGraphNode_Blend2`는 두 자식과 단일 `Alpha`만 다룬다. 실전에서는 (a) 방향 키 4개로 8방향 stride blend, (b) 표정 등 다축 가중평균 같이 자식 N개 + 가중치 벡터가 필요한 케이스가 흔하다. **A3의 목적은 N개 자식 sub-graph 포즈를 가중치 벡터로 본별 합성하는 `FAnimGraphNode_BlendN`을 도입**하는 것이다.

회전 합성은 v1 §3.5의 결정대로 **누적 Slerp 근사**를 채택한다. 근거: log/exp 가중평균은 비용·구현 부담이 크고, 실시간 캐릭터 애니메이션에서 누적 Slerp가 표준 패턴, `FQuat::Slerp` 재사용 가능. 위치·스케일은 정밀 가중평균(Σ w·v / Σ w).

사용자 지시에 따라 **헤더는 분리하지 않고** 기존 `AnimGraph.h/cpp`에 추가한다 — vcxproj 무변경 유지.

### Scope

**포함:**
- [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h)에 `FAnimGraphNode_BlendN` 구조체 정의 추가 (필드: `Children` `TArray<unique_ptr<...>>` + `Weights` `TArray<float>` + 내부 `ChildScratches` `TArray<TArray<FTransform>>`).
- [AnimGraph.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp)에 `BlendN::Evaluate` 구현 추가 — v1 §3.5 누적 Slerp + §3.6 경계 케이스 그대로.

**제외:**
- `FAnimGraphNode_Blend2` 또는 `BlendTransform` 변경 없음 (A2 결과 그대로).
- 헤더 분리(`AnimGraph_BlendNodes.h`) — 사용자 명시. vcxproj 무변경.
- BlendN 인스턴스화 경로(에디터/Lua API) — 범위 외. graph는 코드로 직접 구성 전제.
- 추가 산술 헬퍼 — 누적 알고리즘은 `BlendN::Evaluate` 본문에 inline 작성(다른 노드 공유 없음, 분리 가치 X).

### Critical files

1. [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h) — `FAnimGraphNode_BlendN` 정의 추가
2. [AnimGraph.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp) — `BlendN::Evaluate` 구현 추가

[KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj)는 **무변경**.

### Reused APIs (변경 없이 활용)

- `FQuat::Slerp` — 이미 SequencePlayer/BlendTransform에서 사용 중.
- `FVector` 산술: 기본 생성자 `(0,0,0)` (`Vector.h:33`), `operator+=(const FVector&)` (`Vector.h:78`), `operator*(float)` (`Vector.h:75`), `operator/(float)` (`Vector.h:76`).
- `AnimPoseUtils::FillBindPoseTransforms` — 전체 fallback(자식 0개) 및 자식 nullptr 안전망.
- `AnimPoseUtils::BindPoseToTransform` — 본 단위 fallback (Σ w ≤ 0 케이스).
- `USkeleton::GetBones()` — 본 개수 + `FBoneInfo::LocalBindPose` 접근.
- `std::max` — 음수 weight 클램프 (이미 `<algorithm>` 포함).

### 파일별 변경

#### 1. AnimGraph.h — `FAnimGraphNode_BlendN` 정의 추가

`FAnimGraphNode_Blend2` 정의 직후에 다음 블록 추가:

```cpp
/**
 * N개 자식 sub-graph의 가중 합성 노드.
 * - 위치/스케일: 정밀 가중평균 (Σ wᵢ·vᵢ / Σ wᵢ).
 * - 회전: 누적 Slerp 근사 — RotAcc = Slerp(RotAcc, Cᵢ.Rotation, wᵢ / Σ_{j≤i} wⱼ).
 * - Children[c]가 nullptr이면 bind pose로 자식 포즈 대체.
 * - Weights.size() != Children.size() — 부족 인덱스는 0, 초과 인덱스는 무시.
 * - Σ wᵢ ≤ 0 → 본별 bind pose fallback (음수 외삽 차단).
 */
struct FAnimGraphNode_BlendN : FAnimGraphNode_Base
{
    TArray<std::unique_ptr<FAnimGraphNode_Base>> Children;
    TArray<float>                                Weights;

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

  private:
    TArray<TArray<FTransform>> ChildScratches; // size == Children.size()
};
```

#### 2. AnimGraph.cpp — `BlendN::Evaluate` 구현 추가

파일 끝에 다음 추가 (v1 §3.5 의사코드 그대로 코드화):

```cpp
void FAnimGraphNode_BlendN::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    const size_t N           = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
    const size_t NumChildren = Children.size();
    if (N == 0 || NumChildren == 0)
    {
        AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);
        return;
    }

    OutLocalPose.resize(N);
    if (ChildScratches.size() != NumChildren)
    {
        ChildScratches.resize(NumChildren);
    }

    // 1) 자식 평가 (nullptr이면 bind pose 안전망)
    for (size_t c = 0; c < NumChildren; ++c)
    {
        ChildScratches[c].resize(N);
        if (Children[c]) Children[c]->Evaluate(Ctx, ChildScratches[c]);
        else             AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ChildScratches[c]);
    }

    const TArray<FBoneInfo> &Bones = Ctx.Skeleton->GetBones();

    // 2) 본별 누적
    for (size_t i = 0; i < N; ++i)
    {
        const float W0 = (0 < Weights.size()) ? std::max(Weights[0], 0.0f) : 0.0f;

        float   SumW     = W0;
        FVector PosAcc   = ChildScratches[0][i].Location * W0;
        FVector ScaleAcc = ChildScratches[0][i].Scale    * W0;
        FQuat   RotAcc   = ChildScratches[0][i].Rotation;

        for (size_t c = 1; c < NumChildren; ++c)
        {
            const float w = (c < Weights.size()) ? std::max(Weights[c], 0.0f) : 0.0f;
            if (w <= 0.0f) continue;

            SumW     += w;
            PosAcc   += ChildScratches[c][i].Location * w;
            ScaleAcc += ChildScratches[c][i].Scale    * w;

            const float SlerpAlpha = w / SumW;
            RotAcc = FQuat::Slerp(RotAcc, ChildScratches[c][i].Rotation, SlerpAlpha);
        }

        if (SumW <= 0.0f)
        {
            OutLocalPose[i] = AnimPoseUtils::BindPoseToTransform(Bones[i].LocalBindPose);
        }
        else
        {
            const float Inv = 1.0f / SumW;
            OutLocalPose[i] = FTransform(PosAcc * Inv, RotAcc, ScaleAcc * Inv);
        }
    }
}
```

**알고리즘 정합성 노트:**
- 첫 자식 weight가 0 또는 음수면 `SumW = 0`으로 시작하지만, 이후 첫 양수 weight 자식 `c`가 등장하면 `SlerpAlpha = w / w = 1.0`이라 `RotAcc`가 그 자식 회전으로 완전 대체됨 — 정상.
- 모든 자식 weight가 0 또는 음수면 `SumW ≤ 0` 유지 → 본별 bind pose fallback 진입.
- `RotAcc` 초기값(첫 자식 회전)은 `SumW = 0` 케이스에서 SlerpAlpha 분모가 0이 되어 nan을 만들 위험이 있지만, 그 분기는 `w <= 0 → continue`로 보호됨. 첫 양수 weight 자식이 곧바로 RotAcc를 덮으므로 부동소수 안전.

include: `AnimGraph.cpp`는 이미 `AnimPoseUtils.h`, `Skeleton.h`, `Math/Quat.h`, `<algorithm>`을 포함 — 추가 include 불필요.

### 실행 순서

1. AnimGraph.h — `FAnimGraphNode_BlendN` 정의 추가 (`Blend2` 정의 직후)
2. AnimGraph.cpp — `BlendN::Evaluate` 구현 추가 (파일 끝)
3. KraftonEngine 프로젝트 풀 rebuild

### Verification

**컴파일:** `KraftonEngine\KraftonEngine.vcxproj` 풀 rebuild. 오류 0건 기대, 경고는 기존 무관 4건 유지.

**SingleNode 회귀:** BlendN graph는 어디서도 인스턴스화되지 않으므로 SingleNode 시퀀스 재생은 무영향. 사용자 시각 동치 확인 — A2 직후와 동일.

**static 검증:** Grep으로 다음 확인.
- `FAnimGraphNode_BlendN`: 정의 1건(AnimGraph.h), 구현 1건(AnimGraph.cpp), 호출처 0건
- `FQuat::Slerp` 추가 호출처 1건(BlendN::Evaluate 내부)
- `BindPoseToTransform` 호출처 1건(BlendN의 본별 fallback)

**BlendN 자체 동작 검증 (이번 단계 범위 외, 향후 단계):** 사용자가 3-자식 graph 구성해 weight 변경 비주얼 확인 (예: weight=[1,0,0] → child0 단독, weight=[0,0,0] → bind pose, weight=[1,1,0] → child0/child1 균등).

### Out of scope / 다음 단계

- **B0:** `Ctx.DeltaTime`/`OwningInstance` 필드 추가 + 두 호출자 보강. StateMachine 토대.
- **B1~B4:** StateMachine 데이터 구조 / 노드 / 인스턴스 / 컴포넌트 분기.
- 헤더 분리(`AnimGraph_BlendNodes.h/.cpp`): 현재까지 AnimGraph.h가 SequencePlayer + Blend2 + BlendN + AnimGraph 클래스로 비대해지고 있음. StateMachine 노드(`FAnimGraphNode_StateMachine`)까지 들어가면 분리 권고. **단, 사용자 명시로 본 단계는 유지.**
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §B0 — `Ctx.DeltaTime`/`OwningInstance` 도입

### Context

A3까지로 Blend 노드 세트(Blend2/BlendN)는 완성됐다. 다음 단계는 StateMachine인데, StateMachine 노드는 (a) 상태별 체류 시간을 누적하기 위해 **프레임 dt**가 필요하고, (b) `BoolVariable` 조건을 평가하기 위해 인스턴스 BoolVariables 맵에 접근해야 한다(v2 §4.3/§4.6). 두 정보 모두 stateless 노드가 알 수 없으므로 `FAnimEvalContext`가 운반해야 한다.

**B0의 목적은 StateMachine 노드 구현 직전에 평가 계약(Ctx)을 한 번에 확장하는 것**이다. 이번 단계에서 StateMachine 노드는 도입하지 않는다 — 그 노드는 B1에서 **사용자 지시대로 별도 헤더로** 들어간다. B0는 토대만 준비.

**SingleNode 무영향 보증:** `FAnimGraphNode_SequencePlayer::Evaluate`는 `Ctx.Skeleton`/`Ctx.TimeSeconds`만 읽으므로 두 신규 필드는 사용 안 함. 호출자가 신규 필드를 채워도 산출 결과 비트 동치.

### Scope

**포함:**
- [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h)의 `FAnimEvalContext`에 다음 두 필드 추가:
  - `float DeltaTime = 0.0f;` — 현재 프레임 dt (paused일 때는 호출자가 0으로 세팅).
  - `class UAnimInstance *OwningInstance = nullptr;` — 평가를 트리거한 인스턴스의 raw ref. StateMachine 노드가 `dynamic_cast<UAnimStateMachineInstance*>`해 BoolVariables 조회. forward decl로 충분(헤더 사이클 회피).
- [AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h)에 `float LastDeltaTime = 0.0f;` 멤버 추가.
- [AnimInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp):
  - `UAnimInstance::Update(float DeltaTime)` 진입 직후 `LastDeltaTime = bPaused ? 0.0f : DeltaTime;` 저장 (early-return 분기보다 먼저 — Length<=0 케이스에서도 갱신 보장).
  - `UAnimInstance::EvaluateGraph`의 `FAnimEvalContext` 세팅에 `Ctx.DeltaTime = LastDeltaTime;` 와 `Ctx.OwningInstance = this;` 추가.
- [AnimSingleNodeInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp):
  - `UAnimSingleNodeInstance::EvaluateGraph`의 `Ctx` 세팅에 `Ctx.DeltaTime = LastDeltaTime;` 와 `Ctx.OwningInstance = this;` 추가.

**제외 (별도 단계):**
- `FAnimGraphNode_StateMachine` / `UAnimStateMachineInstance` — B1/B2/B3에서.
- 별도 헤더 분리(`AnimGraph_StateMachine.h/.cpp`) — B1에서 첫 도입과 동시에 신규 헤더로 작성.
- 컴포넌트 측 AnimationMode 분기 — B4.

### Critical files

1. [AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h) — `FAnimEvalContext` 확장 + `class UAnimInstance;` forward decl
2. [AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h) — `LastDeltaTime` 멤버
3. [AnimInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) — `Update`에서 저장 + `EvaluateGraph`에서 세팅
4. [AnimSingleNodeInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp) — `EvaluateGraph`에서 세팅

[KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj)는 **무변경**.

### Reused APIs (변경 없이 활용)

- `UAnimInstance::bPaused` (`AnimInstance.h:91`) — `Update`의 `LastDeltaTime` 저장 분기에 사용.
- 기존 `FAnimEvalContext::Skeleton`/`TimeSeconds` (`AnimGraph.h:27-28`) — 함께 세팅되는 패턴 유지.

### 파일별 변경

#### 1. AnimGraph.h — `FAnimEvalContext` 확장

- 상단 forward decl 블록에 `class UAnimInstance;` 추가 (현재 `USkeleton`/`UAnimDataModel`/`UAnimSequence` 옆).
- `FAnimEvalContext` 본문:
  ```cpp
  struct FAnimEvalContext
  {
      const USkeleton  *Skeleton       = nullptr;
      float             TimeSeconds    = 0.0f;
      float             DeltaTime      = 0.0f;   // §B0: paused일 때 호출자가 0으로 세팅
      UAnimInstance    *OwningInstance = nullptr; // §B0: StateMachine이 dynamic_cast해 변수 조회
  };
  ```
  주석도 갱신: "Ctx는 평가 시점에 노드들이 공통으로 참조해야 할 최소값만 운반한다 (TimeSeconds는 자식 시간으로 패치될 수 있다 — StateMachine 참조)." 류로 보강.

#### 2. AnimInstance.h — `LastDeltaTime` 멤버

`UAnimInstance` private/protected 멤버 영역에 다음 추가 (시간 관련 멤버 근처):
```cpp
float LastDeltaTime = 0.0f; // Update에서 저장, EvaluateGraph가 Ctx.DeltaTime으로 전달 (paused면 0)
```

#### 3. AnimInstance.cpp

- `UAnimInstance::Update` 진입부:
  ```cpp
  void UAnimInstance::Update(float DeltaTime)
  {
      LastDeltaTime = bPaused ? 0.0f : DeltaTime; // §B0
      TriggeredNotifiesThisFrame.clear();
      ...
  }
  ```
- `UAnimInstance::EvaluateGraph`의 `Ctx` 세팅:
  ```cpp
  FAnimEvalContext Ctx;
  Ctx.Skeleton       = Skeleton;
  Ctx.TimeSeconds    = CurrentTime;
  Ctx.DeltaTime      = LastDeltaTime; // §B0
  Ctx.OwningInstance = this;          // §B0
  ```

#### 4. AnimSingleNodeInstance.cpp

`UAnimSingleNodeInstance::EvaluateGraph`의 `Ctx` 세팅:
```cpp
FAnimEvalContext Ctx;
Ctx.Skeleton       = Skeleton;
Ctx.TimeSeconds    = CurrentTime;
Ctx.DeltaTime      = LastDeltaTime; // §B0 (base 멤버 접근)
Ctx.OwningInstance = this;          // §B0
```

### 실행 순서

1. AnimGraph.h — forward decl + `FAnimEvalContext` 2필드 추가
2. AnimInstance.h — `LastDeltaTime` 멤버 추가
3. AnimInstance.cpp — Update 진입부 + EvaluateGraph 세팅
4. AnimSingleNodeInstance.cpp — EvaluateGraph 세팅
5. KraftonEngine 프로젝트 풀 rebuild

### Verification

**컴파일:** `KraftonEngine\KraftonEngine.vcxproj` 풀 rebuild. 오류 0건 기대.

**SingleNode 회귀:** `SequencePlayer::Evaluate`는 `Ctx.Skeleton`/`Ctx.TimeSeconds`만 사용 — `DeltaTime`/`OwningInstance`는 미사용. 따라서 산출 비트 동치. 사용자 시각 동치 확인 — A3 직후와 동일 기대.

**static 검증:** Grep으로 다음 확인.
- `FAnimEvalContext` 정의에 `DeltaTime` 1건, `OwningInstance` 1건
- `LastDeltaTime` 멤버 선언 1건 + 저장 1건(Update 진입부) + 읽기 2건(두 EvaluateGraph)
- `Ctx.DeltaTime`, `Ctx.OwningInstance` 사용은 두 EvaluateGraph 세팅 + 노드 read 0건 (B1 이후 등장)

**디버거 확인 (선택):** SingleNode 컴포넌트에 break — `Update` 후 `LastDeltaTime`이 dt 또는 0, `EvaluateGraph` 진입 시 `Ctx.OwningInstance == this`.

### Out of scope / 다음 단계

- **B1:** `FAnimState`/`FAnimTransitionCondition`/`FAnimTransition` 데이터 구조 + `FAnimGraphNode_StateMachine` 정의 — **사용자 지시대로 별도 헤더 `Asset/Animation/Core/AnimGraph_StateMachine.h/.cpp` 도입**. vcxproj `ClCompile`/`ClInclude` 등록 필요.
- **B2:** StateMachine 노드 `Evaluate` 구현 (TimeElapsed 조건만 우선).
- **B3:** `UAnimStateMachineInstance` + `BoolVariables` + `BoolVariable` 조건 활성화.
- **B4:** `USkeletalMeshComponent::AnimationMode` 분기 + `EAnimationMode::AnimationStateMachine` + `CreateObject<UAnimStateMachineInstance>`.
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §B1 — StateMachine 데이터 구조 + 노드 정의 (별도 헤더 분리)

### Context

B0로 평가 계약(`FAnimEvalContext`)이 dt/OwningInstance를 운반하도록 확장됐다. **B1의 목적은 StateMachine 노드의 정적 표현(상태·전이·조건 + 노드 정의)을 코드에 도입하는 것**이다. 평가 알고리즘(상태 시간 누적, 조건 발화, transition 합성)은 B2 책임 — 본 단계에서는 **stub만 두고 컴파일 통과**까지가 목표.

**별도 헤더 분리 결정 (사용자 명시):** `AnimGraph.h`가 이미 SequencePlayer + Blend2 + BlendN + `AnimGraph` 클래스로 비대해진 상태. StateMachine 노드는 추가 데이터 타입(3개 struct + 1개 enum)까지 동반하므로 분리 가치가 명확. 신규 파일은 `Asset/Animation/Core/AnimGraph_StateMachine.h/.cpp`.

**v2 §4.1 데이터 구조 스케치를 그대로 적용**한다 (그 안에서 v1 §4.1과 동일).

### Scope

**포함:**
- 신규 헤더 [AnimGraph_StateMachine.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h):
  - `FAnimState` 구조체
  - `EAnimTransitionConditionKind` enum (`TimeElapsed`/`BoolVariable`/`OnNotify`/`Custom` — 뒤 둘은 자리만, B2에서 false 반환)
  - `FAnimTransitionCondition` 구조체
  - `FAnimTransition` 구조체
  - `FAnimGraphNode_StateMachine` 구조체 (Evaluate override 선언)
- 신규 cpp [AnimGraph_StateMachine.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp):
  - `FAnimGraphNode_StateMachine::Evaluate` **stub**: `AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);` 한 줄. (인스턴스화 0건이므로 실행은 안 됨. B2에서 본 알고리즘으로 교체.)
- [KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj)에 신규 파일 2개 등록:
  - `<ClInclude Include="Source\Engine\Asset\Animation\Core\AnimGraph_StateMachine.h" />` — 알파벳 순으로 `AnimGraph.h` 다음, `AnimInstance.h` 앞.
  - `<ClCompile Include="Source\Engine\Asset\Animation\Core\AnimGraph_StateMachine.cpp" />` — 알파벳 순으로 `AnimGraph.cpp` 다음, `AnimInstance.cpp` 앞.

**제외 (별도 단계):**
- `Evaluate` 본 알고리즘 — B2 (시간 누적 + 조건 발화 + transition 합성).
- `UAnimStateMachineInstance` (인스턴스 클래스) — B3.
- 컴포넌트 AnimationMode 분기 — B4.
- 에디터/Lua API — 범위 외.

### Critical files

1. [AnimGraph_StateMachine.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h) — 신규
2. [AnimGraph_StateMachine.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp) — 신규 (Evaluate stub만)
3. [KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj) — `ClInclude`/`ClCompile` 2줄 추가

### Reused APIs (변경 없이 활용)

- `FAnimGraphNode_Base` (`AnimGraph.h:34`) — 상속 베이스.
- `FAnimEvalContext` (`AnimGraph.h`) — B0 확장 그대로.
- `FName` (`Object/FName.h`) — 상태·변수·노티파이 이름.
- `int32`/`uint8` typedef (`Core/CoreTypes.h:15,18`).
- `AnimPoseUtils::FillBindPoseTransforms` (`AnimPoseUtils.h`) — stub fallback.
- `std::unique_ptr<FAnimGraphNode_Base>` 패턴 — `AnimGraph.h`의 Blend2와 동일.

### 파일별 변경

#### 1. AnimGraph_StateMachine.h (신규)

```cpp
/**
 * AnimGraph 스테이트 머신 노드와 그 부속 데이터 구조.
 *
 * B1 범위: 정적 표현(상태·전이·조건 + 노드 정의)만 도입.
 * 평가 알고리즘은 B2 (FAnimGraphNode_StateMachine::Evaluate 본문).
 *
 * 인스턴스 컨테이너(UAnimStateMachineInstance)와 BoolVariables 평가는 B3.
 */

#pragma once

#include "Asset/Animation/Core/AnimGraph.h"
#include "Core/CoreTypes.h"
#include "Object/FName.h"

#include <memory>

/**
 * StateMachine의 한 상태. Sub는 보통 SequencePlayer 또는 Blend 노드 트리.
 * - bResetTimeOnEnter: 상태 진입 시 StateLocalTimes[idx]를 0으로 리셋할지.
 * - bLooping + SubLengthHint: 상태 시간을 fmod로 wrap할지 (0이면 clamp 동작).
 */
struct FAnimState
{
    FName                                Name;
    std::unique_ptr<FAnimGraphNode_Base> Sub;
    bool                                 bResetTimeOnEnter = true;
    bool                                 bLooping          = true;
    float                                SubLengthHint     = 0.0f;
};

enum class EAnimTransitionConditionKind : uint8
{
    TimeElapsed,    // 활성 상태의 누적 체류 시간 >= TimeThreshold
    BoolVariable,   // OwningInstance(UAnimStateMachineInstance)의 BoolVariables[VarName] == bExpectedValue
    OnNotify,       // 파트 B 대기 — B2에서 평가 false
    Custom          // 파트 B 대기 — B2에서 평가 false
};

struct FAnimTransitionCondition
{
    EAnimTransitionConditionKind Kind          = EAnimTransitionConditionKind::TimeElapsed;
    float                        TimeThreshold = 0.0f;
    FName                        VarName;
    bool                         bExpectedValue = true;
    FName                        NotifyName;
};

struct FAnimTransition
{
    int32                            FromStateIndex = -1;
    int32                            ToStateIndex   = -1;
    float                            BlendDuration  = 0.2f;
    TArray<FAnimTransitionCondition> Conditions; // AND 결합
};

/**
 * 스테이트 머신 노드. State / Transition은 외부(에디터·Lua)가 구성해 set한다.
 * 평가 시 자체 시간 누적 + 조건 발화 + transition 진행 중 BlendTransform 합성.
 */
struct FAnimGraphNode_StateMachine : FAnimGraphNode_Base
{
    TArray<FAnimState>      States;
    TArray<FAnimTransition> Transitions;
    int32                   InitialStateIndex = 0;

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

  private:
    int32              ActiveStateIndex      = -1;
    int32              ActiveTransitionIndex = -1;
    float              TransitionElapsed     = 0.0f;
    TArray<float>      StateLocalTimes; // size == States.size()
    TArray<FTransform> ScratchFrom;
    TArray<FTransform> ScratchTo;
};
```

#### 2. AnimGraph_StateMachine.cpp (신규, stub)

```cpp
#include "Asset/Animation/Core/AnimGraph_StateMachine.h"

#include "Asset/Animation/Core/AnimPoseUtils.h"

void FAnimGraphNode_StateMachine::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    // B1 stub — 본 평가 알고리즘은 B2에서. 인스턴스화 경로가 없어 실제 호출되지 않으나
    // 컴파일 검증과 호출 시 안전망 차원에서 bind pose로 채운다.
    AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);
}
```

#### 3. KraftonEngine.vcxproj — 두 줄 추가

알파벳 순서 유지. 추가 위치 예시:

`ClCompile` ItemGroup 안 (현재 line 361-364 부근):
```xml
<ClCompile Include="Source\Engine\Asset\Animation\Core\AnimGraph.cpp" />
<ClCompile Include="Source\Engine\Asset\Animation\Core\AnimGraph_StateMachine.cpp" />  <!-- 신규 -->
<ClCompile Include="Source\Engine\Asset\Animation\Core\AnimInstance.cpp" />
```

`ClInclude` ItemGroup 안 (현재 line 649 부근):
```xml
<ClInclude Include="Source\Engine\Asset\Animation\Core\AnimGraph.h" />
<ClInclude Include="Source\Engine\Asset\Animation\Core\AnimGraph_StateMachine.h" />  <!-- 신규 -->
<ClInclude Include="Source\Engine\Asset\Animation\Core\AnimInstance.h" />
```

### 실행 순서

1. AnimGraph_StateMachine.h — 데이터 구조 + 노드 정의 작성
2. AnimGraph_StateMachine.cpp — Evaluate stub 작성
3. KraftonEngine.vcxproj — ClInclude / ClCompile 두 줄 추가
4. KraftonEngine 프로젝트 풀 rebuild

### Verification

**컴파일:** `KraftonEngine\KraftonEngine.vcxproj` 풀 rebuild. 오류 0건 기대. 신규 cpp가 빌드 로그에 등장하면 vcxproj 등록 정상.

**SingleNode 회귀:** StateMachine 노드는 어디서도 인스턴스화되지 않으므로 SingleNode 시퀀스 재생은 무영향. 사용자 시각 동치 확인 — B0 직후와 동일.

**static 검증:** Grep으로 다음 확인.
- `FAnimGraphNode_StateMachine`: 정의 1건(AnimGraph_StateMachine.h), 구현 1건(AnimGraph_StateMachine.cpp), 호출처 0건
- `EAnimTransitionConditionKind`: 정의 1건, 호출처 0건
- `FAnimState`/`FAnimTransition`/`FAnimTransitionCondition`: 각각 정의 1건, 호출처 0건
- vcxproj에 `AnimGraph_StateMachine.cpp` 1건, `AnimGraph_StateMachine.h` 1건

### Out of scope / 다음 단계

- **B2:** `FAnimGraphNode_StateMachine::Evaluate` 본문 — v2 §4.6 의사코드. 시간 누적 + 조건 발화(TimeElapsed) + transition 진행 + BlendTransform 합성. `BoolVariable` 조건은 인스턴스가 없으면 false 반환 (B3에서 활성화).
- **B3:** `UAnimStateMachineInstance` 신규 클래스 (`DECLARE_CLASS`/`IMPLEMENT_CLASS`) + `BoolVariables` TMap + `BoolVariable` 조건 평가 활성화.
- **B4:** `USkeletalMeshComponent::AnimationMode` 분기로 StateMachine 인스턴스 생성 경로 추가.
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §B2 — `FAnimGraphNode_StateMachine::Evaluate` 본 알고리즘

### Context

B1까지로 StateMachine 정적 표현(States/Transitions/Conditions)과 노드 클래스 stub이 자리잡았다. **B2의 목적은 노드의 평가 알고리즘을 채워 넣는 것** — 시간 누적, transition 발화/진행, 자식 평가, transition 중 `BlendTransform` 합성까지 한 번에 완성한다. 결과: 코드로 직접 graph를 구성하면 StateMachine이 SingleNode 인스턴스 위에서도 동작할 수 있는 상태가 된다(다만 컴포넌트 인스턴스 교체는 B4).

v2 §4.6 의사코드를 그대로 코드화한다. `BoolVariable`/`OnNotify`/`Custom` 조건은 plan 흐름대로 **본 단계에서는 false 반환** — B3에서 base 가상 훅 + 인스턴스 override + 분기 갱신으로 활성화.

### Scope

**포함:**
- [AnimGraph_StateMachine.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h)의 `FAnimGraphNode_StateMachine`에 private 멤버 함수 2개 선언 추가:
  - `void ApplyLoopOrClamp(int32 StateIdx);`
  - `static bool EvaluateConditions(class UAnimInstance *Owning, const TArray<FAnimTransitionCondition> &Conds, float ActiveStateTime);`
- [AnimGraph_StateMachine.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp):
  - `Evaluate` stub을 v2 §4.6 본 알고리즘으로 교체
  - `ApplyLoopOrClamp` / `EvaluateConditions` 정의 추가
  - include: `Asset/Animation/Core/AnimPoseUtils.h` 유지(BlendTransform/FillBindPoseTransforms 호출), `Asset/Animation/Core/Skeleton.h` 추가(GetBones), `<algorithm>` + `<cmath>` 추가(std::clamp / std::fmod).

**제외 (별도 단계):**
- `UAnimInstance`에 `GetBoolVariable` 가상 훅 추가 — B3. 본 단계의 `EvaluateConditions`는 `BoolVariable` 분기에서 그냥 false 반환.
- `UAnimStateMachineInstance` — B3.
- 컴포넌트 분기 — B4.
- `BlendDuration > Δt` 케이스에서 transition 발화 직후 즉시 합성 — v2 의사코드 그대로 따르므로 정상 처리 (TransitionElapsed=0에서 Alpha 계산 후 ScratchFrom 우세).

### Critical files

1. [AnimGraph_StateMachine.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h) — private 헬퍼 2개 선언 추가
2. [AnimGraph_StateMachine.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp) — Evaluate 본 알고리즘 + 헬퍼 정의

vcxproj는 **무변경** (B1에서 이미 등록).

### Reused APIs (변경 없이 활용)

- `AnimPoseUtils::FillBindPoseTransforms` / `AnimPoseUtils::BlendTransform` (`AnimPoseUtils.h`) — fallback 및 transition 합성.
- `USkeleton::GetBones()` (`Skeleton.h:40`) — 본 개수.
- `FAnimEvalContext::Skeleton`/`TimeSeconds`/`DeltaTime`/`OwningInstance` (B0 확장) — 모두 사용.
- `std::clamp` (C++17), `std::fmod` — 시간 wrap/clamp.

### 파일별 변경

#### 1. AnimGraph_StateMachine.h — private 헬퍼 선언 추가

`FAnimGraphNode_StateMachine` 정의의 private 영역에 추가 (Evaluate override 선언 뒤, 멤버 필드 앞 또는 뒤):

```cpp
struct FAnimGraphNode_StateMachine : FAnimGraphNode_Base
{
    // ... 기존 public 필드 ...

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

  private:
    void ApplyLoopOrClamp(int32 StateIdx);
    static bool EvaluateConditions(class UAnimInstance *Owning,
                                   const TArray<FAnimTransitionCondition> &Conds,
                                   float ActiveStateTime);

    // ... 기존 private 필드 ...
};
```

`class UAnimInstance` 토큰은 forward param decl — 헤더에 별도 forward decl 없어도 컴파일 가능. 가독성 위해 파일 상단에 `class UAnimInstance;` 추가도 OK (선택).

#### 2. AnimGraph_StateMachine.cpp — Evaluate 본문 + 헬퍼 정의

기존 stub 한 줄을 v2 §4.6 의사코드 코드화로 교체. 전체 본문:

```cpp
#include "Asset/Animation/Core/AnimGraph_StateMachine.h"

#include "Asset/Animation/Core/AnimPoseUtils.h"
#include "Asset/Animation/Core/Skeleton.h"

#include <algorithm>
#include <cmath>

void FAnimGraphNode_StateMachine::ApplyLoopOrClamp(int32 StateIdx)
{
    const FAnimState &S = States[StateIdx];
    if (!S.bLooping)        return; // wrap 안 함 — sub-graph가 자체 clamp(SequencePlayer는 마지막 프레임)
    if (S.SubLengthHint <= 0.0f) return; // 길이 미지정 — wrap 불가
    float &T = StateLocalTimes[StateIdx];
    T = std::fmod(T, S.SubLengthHint);
    if (T < 0.0f) T += S.SubLengthHint;
}

bool FAnimGraphNode_StateMachine::EvaluateConditions(UAnimInstance * /*Owning*/,
                                                    const TArray<FAnimTransitionCondition> &Conds,
                                                    float ActiveStateTime)
{
    if (Conds.empty()) return false; // 빈 조건 transition은 발화 안 함 (안전)

    for (const FAnimTransitionCondition &C : Conds)
    {
        switch (C.Kind)
        {
        case EAnimTransitionConditionKind::TimeElapsed:
            if (ActiveStateTime < C.TimeThreshold) return false;
            break;
        case EAnimTransitionConditionKind::BoolVariable:
        case EAnimTransitionConditionKind::OnNotify:
        case EAnimTransitionConditionKind::Custom:
            return false; // B3+ 활성화 대기 — 본 단계에서는 발화 차단
        }
    }
    return true; // AND 결합 — 모든 조건 만족
}

void FAnimGraphNode_StateMachine::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    const size_t N = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
    if (States.empty() || N == 0)
    {
        OutLocalPose.clear();
        return;
    }

    // 0) 첫 평가 — initial state로 진입 + StateLocalTimes 초기화
    if (ActiveStateIndex < 0)
    {
        const int32 InitClamped = std::clamp(InitialStateIndex, 0, static_cast<int32>(States.size()) - 1);
        ActiveStateIndex = InitClamped;
        StateLocalTimes.assign(States.size(), 0.0f);
    }

    // 1) 시간 누적 (paused면 Ctx.DeltaTime이 호출자에서 이미 0)
    StateLocalTimes[ActiveStateIndex] += Ctx.DeltaTime;
    ApplyLoopOrClamp(ActiveStateIndex);
    if (ActiveTransitionIndex >= 0)
    {
        const FAnimTransition &T = Transitions[ActiveTransitionIndex];
        TransitionElapsed += Ctx.DeltaTime;
        StateLocalTimes[T.ToStateIndex] += Ctx.DeltaTime;
        ApplyLoopOrClamp(T.ToStateIndex);
    }

    // 2) 진행 중 transition 완료 판정
    if (ActiveTransitionIndex >= 0 &&
        TransitionElapsed >= Transitions[ActiveTransitionIndex].BlendDuration)
    {
        const int32 NewActive = Transitions[ActiveTransitionIndex].ToStateIndex;
        ActiveStateIndex = NewActive;
        if (States[NewActive].bResetTimeOnEnter)
        {
            StateLocalTimes[NewActive] = 0.0f;
        }
        ActiveTransitionIndex = -1;
        TransitionElapsed     = 0.0f;
    }

    // 3) 진행 중 transition 없으면 발화 조건 검사
    if (ActiveTransitionIndex < 0)
    {
        for (int32 i = 0; i < static_cast<int32>(Transitions.size()); ++i)
        {
            const FAnimTransition &T = Transitions[i];
            if (T.FromStateIndex != ActiveStateIndex) continue;
            if (T.ToStateIndex < 0 || T.ToStateIndex >= static_cast<int32>(States.size())) continue;
            if (EvaluateConditions(Ctx.OwningInstance, T.Conditions, StateLocalTimes[ActiveStateIndex]))
            {
                ActiveTransitionIndex = i;
                TransitionElapsed     = 0.0f;
                if (States[T.ToStateIndex].bResetTimeOnEnter)
                {
                    StateLocalTimes[T.ToStateIndex] = 0.0f;
                }
                break;
            }
        }
    }

    // 4) 자식 평가 + 합성
    auto MakeChildCtx = [&](int32 StateIdx) {
        FAnimEvalContext C = Ctx;
        C.TimeSeconds = StateLocalTimes[StateIdx];
        return C;
    };

    if (ActiveTransitionIndex < 0)
    {
        // 단일 상태 평가
        FAnimEvalContext Cx = MakeChildCtx(ActiveStateIndex);
        if (States[ActiveStateIndex].Sub)
        {
            States[ActiveStateIndex].Sub->Evaluate(Cx, OutLocalPose);
        }
        else
        {
            AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);
        }
    }
    else
    {
        // From/To 두 상태 평가 후 BlendTransform 합성
        ScratchFrom.resize(N);
        ScratchTo.resize(N);
        OutLocalPose.resize(N);

        const FAnimTransition &T = Transitions[ActiveTransitionIndex];
        FAnimEvalContext CxFrom = MakeChildCtx(T.FromStateIndex);
        FAnimEvalContext CxTo   = MakeChildCtx(T.ToStateIndex);

        if (States[T.FromStateIndex].Sub) States[T.FromStateIndex].Sub->Evaluate(CxFrom, ScratchFrom);
        else                              AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ScratchFrom);

        if (States[T.ToStateIndex].Sub)   States[T.ToStateIndex].Sub->Evaluate(CxTo, ScratchTo);
        else                              AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ScratchTo);

        const float Alpha = (T.BlendDuration > 0.0f)
            ? std::clamp(TransitionElapsed / T.BlendDuration, 0.0f, 1.0f)
            : 1.0f;

        for (size_t i = 0; i < N; ++i)
        {
            OutLocalPose[i] = AnimPoseUtils::BlendTransform(ScratchFrom[i], ScratchTo[i], Alpha);
        }
    }
}
```

**알고리즘 정합성 노트:**
- 조건 검사에서 `T.ToStateIndex` 범위 가드 추가 — 의사코드에 없던 안전망. 의도된 transition만 활성화.
- `EvaluateConditions`가 `Owning` 인자를 받지만 본 단계에서 미사용(주석 `/*Owning*/`로 unused warning 회피). B3에서 base 가상 훅 추가 시 사용.
- `BlendDuration <= 0` 케이스에서 Alpha=1.0 → 즉시 To 상태 포즈로 점프. 다음 프레임의 §2 완료 판정에서 transition 정리됨.

### 실행 순서

1. AnimGraph_StateMachine.h — private 헬퍼 2개 선언 추가
2. AnimGraph_StateMachine.cpp — include 추가, stub 제거, 본 Evaluate + 두 헬퍼 정의
3. KraftonEngine 프로젝트 풀 rebuild

### Verification

**컴파일:** `KraftonEngine\KraftonEngine.vcxproj` 풀 rebuild. 오류 0건 기대.

**SingleNode 회귀:** StateMachine 노드는 어디서도 인스턴스화되지 않으므로 SingleNode 시퀀스 재생은 무영향. 사용자 시각 동치 확인 — B1 직후와 동일.

**StateMachine 자체 동작 검증 (본 단계 범위 외, 사용자 향후 테스트 픽스처):**
- 2-state graph 핸드코딩: State A (sequence X) → State B (sequence Y), TimeElapsed 1.0s 조건.
- 1초 후 transition 시작, BlendDuration 동안 alpha 0→1 시각 확인.
- 단, 인스턴스화 경로(B4 미완) 때문에 본 단계에서는 컴파일 통과 + SingleNode 회귀 없음으로 충분.

**static 검증:** Grep으로 다음 확인.
- `FAnimGraphNode_StateMachine::Evaluate`: cpp에 정의 1건
- `ApplyLoopOrClamp` / `EvaluateConditions`: 각 정의 1건 + 호출처(`Evaluate` 내부) 적절히 등장
- `AnimPoseUtils::BlendTransform`: cpp에서 호출 1건 (transition 합성)
- `AnimPoseUtils::FillBindPoseTransforms`: cpp에서 호출 3건 (단일 fallback + transition From/To fallback)

### Out of scope / 다음 단계

- **B3:** `UAnimStateMachineInstance` 신규 클래스 (Object 시스템 등록) + `BoolVariables` TMap + `SetStateMachineGraph` (외부에서 graph 주입). `UAnimInstance`에 `virtual bool GetBoolVariable(const FName&, bool) const`(기본 false 반환) 추가, `UAnimStateMachineInstance`가 override. `EvaluateConditions`의 `BoolVariable` 분기를 활성화 — 본 base 훅 호출.
- **B4:** `USkeletalMeshComponent::AnimationMode` 분기 + StateMachine 인스턴스 생성 경로.
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §B3 — `UAnimStateMachineInstance` + `BoolVariable` 조건 활성화

### Context

B2까지로 StateMachine 노드는 동작 가능하나 (a) 인스턴스 컨테이너가 없어 컴포넌트가 사용할 수 없고, (b) `BoolVariable` 조건은 false 반환 차단 상태다. **B3의 목적은 (1) `UAnimStateMachineInstance` 신규 클래스로 StateMachine 인스턴스 경로를 마련하고, (2) `BoolVariables` 저장소를 보유, (3) `UAnimInstance` base에 가상 훅 1개를 추가해 노드가 인스턴스 종류에 무관하게 변수를 조회하도록 만드는 것**이다. 컴포넌트 측 분기는 B4 책임.

**저장소 위치 결정 (v2 §4.2):** `BoolVariables`는 **`UAnimStateMachineInstance`에 둔다** — base에 두면 `UAnimSingleNodeInstance`에서 사용되지 않는 죽은 멤버. 노드는 base 가상 훅(`GetBoolVariable`)으로 다형 조회.

**graph 주입 모델 (v2 §4.2):** `SetStateMachineGraph(unique_ptr<FAnimGraphNode_StateMachine>)` 메서드가 base `AnimGraphPtr`(`UAnimInstance::AnimGraphPtr`)에 root로 set한다 → `UAnimInstance::EvaluateGraph`가 그대로 작동(별도 override 불필요). SingleNode와 달리 graph 트리를 우회하지 않음.

**SubLengthHint 자동 도출 (v2 §4.4):** `SetStateMachineGraph` 호출 시점에 각 상태의 `Sub`가 `FAnimGraphNode_SequencePlayer`로 dynamic_cast되면 `Sequence->GetPlayLength()`로 `SubLengthHint` 자동 채움(사용자 명시 우선). RTTI 활성화는 코드베이스 grep으로 확인됨.

### Scope

**포함:**
- [AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h):
  - `class FName;` forward decl 또는 기존 include 유지 (이미 `Object/FName.h` 있음).
  - `virtual bool GetBoolVariable(const FName &Name, bool Default = false) const { return Default; }` protected 가상 훅 추가.
- 신규 헤더 [AnimStateMachineInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h):
  - `UAnimStateMachineInstance : public UAnimInstance` 정의
  - `DECLARE_CLASS` + 기본 생성자/소멸자
  - `SetStateMachineGraph(unique_ptr<FAnimGraphNode_StateMachine>)`
  - `SetBoolVariable(const FName&, bool)` / `GetBoolVariable(const FName&, bool) const override` / `GetBoolVariables() const`
  - 멤버: `TMap<FName, bool, FName::Hash> BoolVariables;`
- 신규 cpp [AnimStateMachineInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp):
  - `IMPLEMENT_CLASS(UAnimStateMachineInstance, UAnimInstance)`
  - `SetStateMachineGraph` 구현 — `AnimGraph` 컨테이너 생성/Reset + SubLengthHint 자동 도출 루프 + `AnimGraph::SetRoot`.
  - `GetBoolVariable` override 구현 — `BoolVariables.find` 조회.
- [AnimGraph_StateMachine.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp):
  - include 추가: `Asset/Animation/Core/AnimInstance.h` (가상 훅 호출용).
  - `EvaluateConditions`의 `BoolVariable` case 활성화: `Owning ? (Owning->GetBoolVariable(C.VarName, false) == C.bExpectedValue) : false` 형태로 AND 결합.
  - `Owning` 인자가 사용되므로 `/*Owning*/` 주석 제거.
- [KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj): `ClCompile`/`ClInclude` 1줄씩 추가 (`AnimSingleNodeInstance` 다음, `Skeleton` 앞).

**제외 (별도 단계):**
- 컴포넌트 측 `AnimationMode` 분기 + `CreateObject<UAnimStateMachineInstance>` — B4.
- `OnNotify` / `Custom` 조건 — 파트 B 대기.
- `BoolVariables` Lua 바인딩 — 파트 B 대기. 본 단계는 시그니처만 노출.

### Critical files

1. [AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h) — `GetBoolVariable` 가상 훅 추가
2. [AnimStateMachineInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h) — 신규
3. [AnimStateMachineInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp) — 신규
4. [AnimGraph_StateMachine.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp) — `BoolVariable` 분기 활성화
5. [KraftonEngine.vcxproj](KraftonEngine/KraftonEngine.vcxproj) — 2줄 추가

### Reused APIs (변경 없이 활용)

- `TMap` = `std::unordered_map` (`CoreTypes.h:42`).
- `FName::Hash` nested struct (`Object/FName.h:18-21`) — `std::hash<FName>` 부재이므로 명시 필수.
- `FName::operator==` (`Object/FName.h:14`).
- `DECLARE_CLASS` / `IMPLEMENT_CLASS` 매크로 — `UAnimSingleNodeInstance`와 동일 패턴.
- `UAnimInstance::AnimGraphPtr` (protected, `AnimInstance.h:88` 부근) — derived 접근.
- `AnimGraph::SetRoot(unique_ptr<FAnimGraphNode_Base>)` (`AnimGraph.h`) — graph root 주입.
- `FAnimGraphNode_SequencePlayer::Sequence` public 필드 + `UAnimSequence::GetPlayLength()`.

### 파일별 변경

#### 1. AnimInstance.h — `GetBoolVariable` 가상 훅

`UAnimInstance` protected 영역에 추가 (다른 가상 훅 `GetEffectivePlayLength`/`GetActiveNotifies` 옆):

```cpp
/**
 * 파생이 외부에서 set한 bool 변수를 노출. 기본은 Default 그대로 반환.
 * StateMachine 노드가 BoolVariable transition 조건 평가에 사용.
 */
virtual bool GetBoolVariable(const FName &Name, bool Default = false) const { return Default; }
```

#### 2. AnimStateMachineInstance.h (신규)

```cpp
/**
 * 스테이트 머신 그래프를 보유하는 UAnimInstance 파생.
 *
 * - SetStateMachineGraph로 외부가 graph를 코드로 구성해 주입한다(소유권 이전).
 * - 평가 시 base UAnimInstance::EvaluateGraph가 AnimGraphPtr->Evaluate 호출 — 별도 우회 없음.
 * - BoolVariables는 본 클래스에 보관. base GetBoolVariable 훅을 override해 노드가 조회.
 */

#pragma once

#include "Asset/Animation/Core/AnimGraph_StateMachine.h"
#include "Asset/Animation/Core/AnimInstance.h"
#include "Core/CoreTypes.h"
#include "Object/FName.h"

#include <memory>

class UAnimStateMachineInstance : public UAnimInstance
{
  public:
    DECLARE_CLASS(UAnimStateMachineInstance, UAnimInstance)

    UAnimStateMachineInstance();
    ~UAnimStateMachineInstance() override = default;

    /**
     * 외부(에디터·테스트 픽스처·Lua)가 graph를 코드로 구성해 주입한다.
     * - 각 FAnimState의 Sub가 FAnimGraphNode_SequencePlayer면 SubLengthHint 자동 도출(0인 경우만).
     * - 이후 base AnimGraphPtr의 root로 set — 소유권 이전.
     */
    void SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine> InRoot);

    void SetBoolVariable(const FName &Name, bool Value) { BoolVariables[Name] = Value; }
    bool GetBoolVariable(const FName &Name, bool Default = false) const override;
    const TMap<FName, bool, FName::Hash> &GetBoolVariables() const { return BoolVariables; }

  private:
    TMap<FName, bool, FName::Hash> BoolVariables;
};
```

#### 3. AnimStateMachineInstance.cpp (신규)

```cpp
#include "Asset/Animation/Core/AnimStateMachineInstance.h"

#include "Asset/Animation/Core/AnimGraph.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(UAnimStateMachineInstance, UAnimInstance)

UAnimStateMachineInstance::UAnimStateMachineInstance() = default;

void UAnimStateMachineInstance::SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine> InRoot)
{
    if (!AnimGraphPtr)
    {
        AnimGraphPtr = std::make_unique<AnimGraph>();
    }

    if (InRoot)
    {
        // SubLengthHint 자동 도출 — Sub가 SequencePlayer면 Sequence->GetPlayLength().
        // 사용자가 SubLengthHint를 명시(>0) 한 경우는 우선.
        for (FAnimState &S : InRoot->States)
        {
            if (S.SubLengthHint > 0.0f) continue;
            if (auto *SeqPlayer = dynamic_cast<FAnimGraphNode_SequencePlayer *>(S.Sub.get()))
            {
                if (SeqPlayer->Sequence)
                {
                    S.SubLengthHint = SeqPlayer->Sequence->GetPlayLength();
                }
            }
        }
    }

    AnimGraphPtr->SetRoot(std::move(InRoot));
}

bool UAnimStateMachineInstance::GetBoolVariable(const FName &Name, bool Default) const
{
    const auto It = BoolVariables.find(Name);
    return (It != BoolVariables.end()) ? It->second : Default;
}
```

#### 4. AnimGraph_StateMachine.cpp — `BoolVariable` 분기 활성화

include 추가:
```cpp
#include "Asset/Animation/Core/AnimInstance.h"
```

`EvaluateConditions` 시그니처에서 `Owning` 주석 제거 + 본문 분기 갱신:

```cpp
bool FAnimGraphNode_StateMachine::EvaluateConditions(UAnimInstance *Owning,
                                                    const TArray<FAnimTransitionCondition> &Conds,
                                                    float ActiveStateTime)
{
    if (Conds.empty()) return false;

    for (const FAnimTransitionCondition &C : Conds)
    {
        switch (C.Kind)
        {
        case EAnimTransitionConditionKind::TimeElapsed:
            if (ActiveStateTime < C.TimeThreshold) return false;
            break;
        case EAnimTransitionConditionKind::BoolVariable:
            if (!Owning) return false;
            if (Owning->GetBoolVariable(C.VarName, false) != C.bExpectedValue) return false;
            break;
        case EAnimTransitionConditionKind::OnNotify:
        case EAnimTransitionConditionKind::Custom:
            return false; // 파트 B 대기
        }
    }
    return true;
}
```

#### 5. KraftonEngine.vcxproj — 2줄 추가

`ClCompile` ItemGroup, `AnimSingleNodeInstance.cpp` 다음:
```xml
<ClCompile Include="Source\Engine\Asset\Animation\Core\AnimSingleNodeInstance.cpp" />
<ClCompile Include="Source\Engine\Asset\Animation\Core\AnimStateMachineInstance.cpp" />  <!-- 신규 -->
<ClCompile Include="Source\Engine\Asset\Animation\Core\Skeleton.cpp" />
```

`ClInclude` ItemGroup, `AnimSingleNodeInstance.h` 다음:
```xml
<ClInclude Include="Source\Engine\Asset\Animation\Core\AnimSingleNodeInstance.h" />
<ClInclude Include="Source\Engine\Asset\Animation\Core\AnimStateMachineInstance.h" />  <!-- 신규 -->
<ClInclude Include="Source\Engine\Asset\Animation\Core\AnimationTypes.h" />
```

### 실행 순서

1. AnimInstance.h — `GetBoolVariable` 가상 훅 1줄 추가
2. AnimStateMachineInstance.h — 신규 작성
3. AnimStateMachineInstance.cpp — 신규 작성
4. AnimGraph_StateMachine.cpp — include 1줄 + `EvaluateConditions` BoolVariable 분기 갱신
5. KraftonEngine.vcxproj — ClCompile / ClInclude 1줄씩 추가
6. KraftonEngine 프로젝트 풀 rebuild

### Verification

**컴파일:** `KraftonEngine\KraftonEngine.vcxproj` 풀 rebuild. 오류 0건 기대.

**SingleNode 회귀:** `GetBoolVariable` 가상 훅은 base 기본값(Default 그대로)이라 SequencePlayer/SingleNode 흐름 무영향. `UAnimStateMachineInstance`는 어디서도 인스턴스화되지 않으므로 (B4 이전) 컴포넌트 동작 무영향. 사용자 시각 동치 확인 — B2 직후와 동일.

**static 검증:**
- `UAnimStateMachineInstance`: 정의 1건(헤더), `IMPLEMENT_CLASS` 1건(cpp), 호출처 0건 (B4 이전).
- `GetBoolVariable`: base 가상 훅 1건 + override 1건. `EvaluateConditions`에서 호출 1건.
- `SetStateMachineGraph`: 정의 1건, 호출처 0건 (B4 이전 또는 사용자 픽스처).
- vcxproj에 신규 cpp/h 각 1건.

**StateMachine 자체 동작 검증 (선택, 사용자 향후 테스트):** 테스트 코드로 `UAnimStateMachineInstance` 인스턴스 직접 생성 → 2-state graph 주입 → `SetBoolVariable`로 transition 발화 확인. 다만 컴포넌트 통합(B4)이 없으면 SkeletalMeshComponent 측 인스턴스 교체 경로가 없음.

### Out of scope / 다음 단계

- **B4:** `USkeletalMeshComponent::AnimationMode` enum 분기 + `EAnimationMode::AnimationStateMachine` 추가 + 컴포넌트의 `CreateObject<UAnimSingleNodeInstance>` 호출 3곳(`SkeletalMeshComponent.cpp:44, 66, 106`)을 모드 분기로 감싸 StateMachine 인스턴스 경로 추가. 런타임 모드 변경 시 기존 인스턴스 해제 이슈는 별도 작업으로 분리 권장.
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §B4 — `EAnimationMode::AnimationStateMachine` 컴포넌트 분기

### Context

B3까지로 `UAnimStateMachineInstance`가 완성됐지만 컴포넌트 측이 항상 `UAnimSingleNodeInstance`만 생성한다([SkeletalMeshComponent.cpp:44, 66, 106](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:44)). 외부가 StateMachine을 사용하려면 컴포넌트가 모드에 따라 다른 인스턴스를 만들어야 한다.

**B4의 목적:** (1) `EAnimationMode` enum에 `AnimationStateMachine` 추가, (2) 인스턴스 생성 로직을 헬퍼(`EnsureAnimInstance`)로 추출해 3곳의 중복을 단일 분기 지점으로 묶음.

**SingleNode 회귀 보증:** `AnimationMode` 기본값은 `AnimationSingleNode`([SkeletalMeshComponent.h:97](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:97))이므로 기존 컴포넌트는 그대로 SingleNode 경로 사용. 외부가 명시적으로 `SetAnimationMode(EAnimationMode::AnimationStateMachine)`을 호출하기 전에는 동작 변경 없음.

**StateMachine 인스턴스 활성화 흐름 (외부 책임):**
1. `SetAnimationMode(EAnimationMode::AnimationStateMachine)` — 모드 set
2. `PlayAnimation`/`SetAnimation`/`EvaluateAnimationPose` 중 하나가 호출되면서 `EnsureAnimInstance`가 `UAnimStateMachineInstance`를 생성
   - 또는 외부가 직접 `GetAnimInstance()` 같은 접근자를 통해 `UAnimStateMachineInstance::SetStateMachineGraph(...)` 호출 (현 코드에는 명시적 getter는 없음 — 향후 필요시 추가)
3. graph 주입 후 매 프레임 `EvaluateGraph`로 StateMachine 평가

본 단계에서는 인스턴스 생성 경로만 마련. graph 주입 API는 컴포넌트가 노출하지 않으며, 외부가 `AnimInstance` 멤버에 접근하기 위해서는 추가 작업(별도 단계)이 필요할 수 있음 — 본 plan은 인스턴스 생성까지만.

### Scope

**포함:**
- [SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h):
  - `EAnimationMode` enum에 `AnimationStateMachine` 멤버 추가.
  - 헤더 주석(line 12)의 `Todo Graph 포함한, 개별 instance에 대한 처리...` 갱신: enum이 두 모드를 다루도록 명시.
  - private 헬퍼 `void EnsureAnimInstance();` 선언 추가.
- [SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp):
  - `#include "Asset/Animation/Core/AnimStateMachineInstance.h"` 추가.
  - 신규 정의 `USkeletalMeshComponent::EnsureAnimInstance()`:
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
  - 3곳의 `if (!AnimInstance) { AnimInstance = ...; InitializeAnimation(...); }` 블록을 `EnsureAnimInstance();` 호출로 치환:
    - `PlayAnimation` (`SkeletalMeshComponent.cpp:41-46` 부근)
    - `SetAnimation` (`:64-68` 부근)
    - `EvaluateAnimationPose` (`:104-108` 부근)
  - line 43의 "현재 enum에는 AnimationSingleNode 한 가지만 존재" 주석 제거 (사실과 어긋남).

**제외 (별도 작업으로 분리):**
- 컴포넌트 측 `GetAnimInstance()` getter 또는 StateMachine graph 주입 API. 외부가 `SetStateMachineGraph` 호출하려면 인스턴스 접근이 필요한데, 현재 헤더에는 그런 getter가 없음. 사용자가 필요로 하면 별도 단계.
- 런타임 중 `AnimationMode` 변경 시 기존 인스턴스 교체 — v2 §6 #7대로 본 plan 범위 외. `SetAnimationMode`는 변수만 갱신하고 다음 `EnsureAnimInstance` 호출 시점에 기존 인스턴스가 nullptr이면 새 타입으로 생성. 이미 생성된 인스턴스는 그대로.
- `PlayAnimation` / `SetAnimation`의 StateMachine 모드 의미 — 현재 두 메서드는 SingleNode 전제. StateMachine 모드에서는 `Cast<UAnimSingleNodeInstance>`가 nullptr 반환해 묵음 처리 → 안전. 의미 재설계는 범위 외.

### Critical files

1. [SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h) — enum 멤버 + 헬퍼 선언
2. [SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) — include + 헬퍼 정의 + 3곳 치환

vcxproj는 **무변경**.

### Reused APIs (변경 없이 활용)

- `UObjectManager::Get().CreateObject<T>(this)` — 기존 패턴(`SkeletalMeshComponent.cpp:44, 66, 106`)과 동일.
- `UAnimInstance::InitializeAnimation(USkeleton*)` (`AnimInstance.h:32`) — 두 파생 모두 base 구현 또는 override 사용.
- `ResolveSkeletonFromMesh(SkeletalMesh)` 익명 namespace 헬퍼 (`SkeletalMeshComponent.cpp:20-23`).

### 파일별 변경

#### 1. SkeletalMeshComponent.h

- **line 13-16 `EAnimationMode`:**
  ```cpp
  enum class EAnimationMode
  {
      AnimationSingleNode,
      AnimationStateMachine
  };
  ```
- **line 12 주석 갱신:** "AnimationSingleNode/AnimationStateMachine 분기. 향후 신규 모드 시 EnsureAnimInstance switch에 case 추가." 류로 단순화.
- **private 영역에 헬퍼 선언 추가** (e.g. `TickComponent` 선언 근처):
  ```cpp
  /**
   * AnimationMode에 맞춰 AnimInstance를 lazy 생성한다.
   * 이미 생성됐으면 no-op. 런타임 모드 변경 후 자동 재생성은 미지원 — 별도 처리 필요.
   */
  void EnsureAnimInstance();
  ```

#### 2. SkeletalMeshComponent.cpp

- **상단 include 추가:**
  ```cpp
  #include "Asset/Animation/Core/AnimStateMachineInstance.h"
  ```
- **신규 정의 추가** (`PlayAnimation` 앞 또는 익명 namespace 직후가 자연스러움):
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
- **`PlayAnimation` (line 37-58)의 `if (!AnimInstance)` 블록(line 41-46)** → 단일 호출:
  ```cpp
  EnsureAnimInstance();
  ```
  주석 (line 43) 제거.
- **`SetAnimation` (line 60-74)의 `if (!AnimInstance)` 블록(line 64-68)** → `EnsureAnimInstance();`.
- **`EvaluateAnimationPose` (line 96-)의 `if (!AnimInstance)` 블록(line 104-108)** → `EnsureAnimInstance();`.

### 실행 순서

1. SkeletalMeshComponent.h — enum 멤버 + 헬퍼 선언 + 주석
2. SkeletalMeshComponent.cpp — include + `EnsureAnimInstance` 정의 + 3곳 치환
3. KraftonEngine 프로젝트 풀 rebuild

### Verification

**컴파일:** `KraftonEngine\KraftonEngine.vcxproj` 풀 rebuild. 오류 0건 기대.

**SingleNode 회귀 (기본 모드):** `AnimationMode = AnimationSingleNode` 기본값이므로 사용자가 Mixamo 시퀀스 재생 시 종전과 동일하게 `UAnimSingleNodeInstance` 생성. 시각 동치 확인 — B3 직후와 동일 기대.

**StateMachine 모드 동작 (선택, 사용자 향후 테스트 픽스처):**
- `SetAnimationMode(EAnimationMode::AnimationStateMachine)` 호출 후 `PlayAnimation(...)` 호출 → `EnsureAnimInstance`가 `UAnimStateMachineInstance` 생성 확인.
- 디버거로 `dynamic_cast<UAnimStateMachineInstance*>(AnimInstance) != nullptr` 확인.

**static 검증:**
- Grep `CreateObject<UAnimSingleNodeInstance>` — `EnsureAnimInstance` 내부 1건만 남음 (이전 3건이 1건으로 통합).
- Grep `CreateObject<UAnimStateMachineInstance>` — `EnsureAnimInstance` 내부 1건.
- Grep `EnsureAnimInstance` — 정의 1건 + 호출 3건.
- Grep `EAnimationMode::AnimationStateMachine` — `EnsureAnimInstance` switch 1건.

### Out of scope / 다음 단계

- 컴포넌트 측 `GetAnimInstance()` getter 또는 `SetStateMachineGraph` 위임 API — 외부 graph 주입 경로가 필요해질 때 별도 작업.
- 런타임 `AnimationMode` 변경 시 기존 인스턴스 해제/교체 — v2 §6 #7. 별도 버그 수정.
- 에디터 UI에서 `AnimationMode` 노출 — 범위 외.
- 커밋: 본 단계 종료 후 별도 지시가 있어야 커밋.

---

## §파트 A 완결 요약

A1a→B4까지 v2 플랜 §5.1의 모든 단계를 완료. 결과:
- **노드 인터페이스 확립:** `FAnimGraphNode_Base::Evaluate(Ctx, TArray<FTransform>&)` — 단일 시그니처.
- **변환 경계 일원화:** `FTransform → FMatrix`는 `USkinnedMeshComponent::ApplyEvaluatedPose` 한 지점.
- **노드 라이브러리:** SequencePlayer / Blend2 / BlendN / StateMachine.
- **인스턴스:** `UAnimSingleNodeInstance` / `UAnimStateMachineInstance` + 컴포넌트 모드 분기.
- **조건 지원 (B 단계 일부):** TimeElapsed / BoolVariable. OnNotify / Custom은 파트 B 대기.

파트 B 진입 가능 상태 — Notify dispatch / Lua 바인딩 / `BoolVariables` Lua 노출이 다음 큰 묶음.