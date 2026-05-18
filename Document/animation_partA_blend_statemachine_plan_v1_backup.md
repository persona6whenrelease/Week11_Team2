# 파트 A 구현 계획 — Animation Blend / State Machine

## 0. 이 문서의 범위와 전제

본 문서는 **설계 계획서**이다. 헤더 인터페이스 스케치와 의사코드, 영향 범위, 구현 순서를 담는다. 실제 `.h`/`.cpp` 코드 작성은 후속 구현 프롬프트가 담당한다.

**전제 (스캔 + 사용자 결정으로 확정):**
- `FAnimGraphNode_Base::Evaluate` 출력 파라미터를 `TArray<FMatrix>&` → `TArray<FTransform>&` 로 전환.
- StateMachine 컨테이너로 신규 `UAnimStateMachineInstance` 도입. `UAnimSingleNodeInstance`는 건드리지 않는다.
- Blend 산술 = 위치·스케일 `FVector::Lerp` + 회전 `FQuat::Slerp`. LocalMatrix element-wise 보간은 사용하지 않는다.
- 본 문서는 Blend·StateMachine만 다룬다. Notify dispatch·Lua 바인딩은 파트 B.

**근거 문서:** `Document/animation_partA_infra_scan_result.md`

---

## 1. 보강 확인 결과

설계 결정에 직결되는 항목을 코드로 재확인한 결과. 모두 직접 파일을 열어 확인했다.

### 1.1 `ApplyEvaluatedPose` 와 다운스트림 FK / 스킨

- **`USkinnedMeshComponent::ApplyEvaluatedPose(const TArray<FMatrix>& EvaluatedLocalPose)`** — `Component/SkinnedMeshComponent.h:60`, `Component/SkinnedMeshComponent.cpp:440-470`. AnimInstance가 산출한 로컬 포즈를 받아 `LocalBonePoseMatrices`(`TArray<FMatrix>`, `SkinnedMeshComponent.h:75`)에 복사한다. **override 마스크(`BoneOverrideMask`, `SkinnedMeshComponent.h:77`)가 켜진 본은 덮어쓰지 않는다**(`SkinnedMeshComponent.cpp:455-463`). 이후 `RebuildMeshSpaceBoneMatrices` → `SkinVerticesToReferencePose` → `EnsureRuntimeResources` → `MarkWorldBoundsDirty` 순으로 호출.
- **`USkinnedMeshComponent::RebuildMeshSpaceBoneMatrices()`** — `SkinnedMeshComponent.cpp:344-375`. `LocalBonePoseMatrices`의 `FMatrix`를 부모 `MeshSpaceBoneMatrices[ParentIndex]`와 행렬곱으로 누적(`SkinnedMeshComponent.cpp:371-373`). **FK 누적이 `FMatrix` 곱셈으로 직접 수행됨**.
- **`USkinnedMeshComponent::SkinVerticesToReferencePose()`** — `SkinnedMeshComponent.cpp:377-438`. `InverseBindPose * MeshSpaceBoneMatrices[BoneIndex]`(`SkinnedMeshComponent.cpp:416`)로 스킨 행렬을 만들고 정점을 변환. **스킨도 `FMatrix` 곱셈 전용**.
- **호출 경로:** `USkeletalMeshComponent::TickComponent`(`SkeletalMeshComponent.cpp:129-145`)와 `RefreshAnimationPose`(`SkeletalMeshComponent.cpp:147-156`)가 `AnimInstance->GetOutputLocalPose()`(`AnimInstance.h:44`)를 받아 `ApplyEvaluatedPose`에 그대로 전달.
- **수동 본 편집 API:** `USkinnedMeshComponent::SetBoneLocalPose(int32, const FMatrix&)`(`SkinnedMeshComponent.h:29`) / `SetBoneLocalPoseByName(FString, const FMatrix&)`(`SkinnedMeshComponent.h:30`). 입력이 `FMatrix`로 고정.

**파트 A 설계 함의 (사실 진술):** FK·스킨 코드는 `FMatrix` 곱셈을 직접 쓰므로 `FTransform`으로 옮기는 것은 파트 A 범위를 넘는다. 따라서 **`FTransform` → `FMatrix` 변환 경계는 `ApplyEvaluatedPose` 진입 지점**이 자연스럽다(자세한 설계는 §2 참조). 또한 `BoneOverrideMask`가 `FMatrix`를 보존하는 의미를 유지하려면, 시그니처 전환 후에도 override-on 본의 `FMatrix` 값을 그대로 유지하고, 평가 결과(`FTransform`)는 override-off 본에만 `ToMatrix()` 적용 후 `LocalBonePoseMatrices`에 덮어쓰는 형태가 되어야 한다.

### 1.2 인스턴스 생성 경로

- 컴포넌트 3개 진입점 모두 `UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this)` 호출 — `SkeletalMeshComponent.cpp:44, 66, 106`. Outer가 컴포넌트.
- 첫 진입 시 한 번 생성하고 이후 재사용. **`AnimationMode` 변경 시 인스턴스 교체 로직은 현재 없음**(SingleNode 한 종류만 있으므로).
- 소멸자(`SkeletalMeshComponent.cpp:26-33`)가 `UObjectManager::Get().DestroyObject(AnimInstance)` 호출.

**파트 A 설계 함의:** `UAnimStateMachineInstance`를 끼우려면 (a) 컴포넌트가 `AnimationMode`로 분기해 `CreateObject<...>` 타입을 선택하거나, (b) 외부(예: 에디터/Lua)에서 `SetAnimInstance(UAnimInstance*)` 류 API로 주입받는 구조로 가야 한다. 본 계획서는 **(a) 분기 도입**을 권고한다(§4 참조).

### 1.3 `UObjectManager::CreateObject<T>` 시그니처

- `Object/Object.h:120-133` (Phase 1 보강 보고서 인용): `template<typename T> T* CreateObject(UObject* InOuter = nullptr)`. `static_assert(std::is_base_of<UObject, T>::value)`. 내부에서 `new T()` + `SetOuter` + 자동 `FName` 부여.
- T 요건: **`DECLARE_CLASS(T, Parent)` (헤더) + `IMPLEMENT_CLASS(T, Parent)` (cpp) 매크로 등록 필수**. 기본 생성자 필요.
- `UAnimSingleNodeInstance`가 이미 이 패턴(`AnimSingleNodeInstance.h:17`, `AnimSingleNodeInstance.cpp:7`)을 따른다.

**파트 A 설계 함의:** `UAnimStateMachineInstance`도 동일 패턴(`DECLARE_CLASS` + `IMPLEMENT_CLASS` + 기본 생성자)으로 만들고 `UObjectManager::Get().CreateObject<...>(this)`로 생성한다. `FAnimGraphNode_*` 류 노드는 `UObject` 파생이 아니므로(현 `FAnimGraphNode_Base`도 `UObject` 아님, `AnimGraph.h:34-38`) 일반 `std::make_unique<...>`로 만든다.

### 1.4 `USkeleton` 본 트리 API

- `Asset/Animation/Core/Skeleton.h:23-47`. `GetBones()` → `const TArray<FBoneInfo>&`(line 40). `FindBoneIndexByName(const FString&)` → `int32`(line 43). 본 개수 전용 헬퍼·트리 탐색 헬퍼는 없음.
- `FBoneInfo::ParentIndex`(`AnimationTypes.h:46`)를 직접 읽어 트리 순회 — `SkinnedMeshComponent.cpp:370` 사용 예 확인.

**파트 A 설계 함의:** Blend·StateMachine이 본 단위 루프를 돌 때 `Skeleton->GetBones().size()`로 본 개수를, `Bones[i].ParentIndex`로 부모를 직접 본다. 신규 헬퍼는 필요 없다.

### 1.5 `FTransform` 의 보간/합성 API

- `Math/Transform.h:6-29`, `Math/Transform.cpp:3-12`.
- 보유: 4개 생성자(기본/TRS/FRotator/FVector-Euler), `GetRotator`, `SetRotation`(FRotator·FQuat 2종), `ToMatrix()`.
- **부재:** `FTransform::Identity` 상수 없음, `FTransform::Lerp`/`Blend` 없음, `operator*`(합성) 없음.

**파트 A 설계 함의:** Blend는 본별로 `FVector::Lerp` + `FQuat::Slerp` + `FVector::Lerp` 조합을 직접 호출해 새 `FTransform`을 구성해야 한다. 그 조합을 한 곳에 모으는 **공용 합성 free function**을 도입하는 것이 자연스럽다(§3에서 인터페이스 제시). `FTransform::Identity` 부재는 bind pose fallback을 만들 때 `FBoneInfo::LocalBindPose`를 `FMatrix` → `FTransform`으로 변환해 채우는 형태로 우회 가능(단 행렬→TRS 분해 비용이 발생, 한 번만 수행하므로 무시 가능).

### 1.6 `[미확인]` 남은 부분

- `FTransform::operator*` 전체 부재 여부는 헤더만 확인했고 외부 friend 함수 형태로 어디 있는지는 grep 미수행. 본 계획에서는 "없다" 전제로 진행한다. 구현 단계에서 발견되면 추가 이용을 검토.
- 런타임 중 `AnimationMode` 변경 시 기존 `AnimInstance` 누수 가능성(스캔 보고서 항목 2). 본 파트 A 범위 밖이며, 별도 작업으로 분리 권장.

---

## 2. 공통 기반 변경 — `Evaluate` 시그니처 전환

### 2.1 변경 대상 시그니처

```cpp
// 기존 (AnimGraph.h:37)
virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose) = 0;

// 변경 후
virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) = 0;
```

### 2.2 전체 영향 범위 (파일·라인 단위)

| # | 파일 | 라인 | 변경 요지 |
|---|---|---|---|
| 1 | `Asset/Animation/Core/AnimGraph.h:37` | base `Evaluate` 시그니처 `TArray<FTransform>&`로 변경 |
| 2 | `Asset/Animation/Core/AnimGraph.h:47` | `FAnimGraphNode_SequencePlayer::Evaluate` override 시그니처 일치 |
| 3 | `Asset/Animation/Core/AnimGraph.h:59` | `AnimGraph::Evaluate` 시그니처 일치 |
| 4 | `Asset/Animation/Core/AnimGraph.cpp:19-32` | `FillBindPose` 헬퍼가 `TArray<FTransform>` 채우도록. 각 본 `Bones[i].LocalBindPose`(FMatrix)에서 `FTransform`으로 분해(아래 2.5 참조) |
| 5 | `Asset/Animation/Core/AnimGraph.cpp:35-43` | `AnimGraph::Evaluate` body 시그니처 일치 |
| 6 | `Asset/Animation/Core/AnimGraph.cpp:45-128` | `FAnimGraphNode_SequencePlayer::Evaluate` body — 마지막 `FTransform(P,R,S).ToMatrix()` (line 126) 호출을 제거하고 **`OutLocalPose[BoneIdx] = FTransform(P, R, S);` 로 직접 대입** |
| 7 | `Asset/Animation/Core/AnimInstance.h:44` | `GetOutputLocalPose()` 반환형 `const TArray<FTransform>&` |
| 8 | `Asset/Animation/Core/AnimInstance.h:96` | `OutputLocalPose` 멤버 타입 `TArray<FTransform>` |
| 9 | `Asset/Animation/Core/AnimInstance.h:92` | `FillBindPose()` 메서드 — `TArray<FTransform>` 채움 |
| 10 | `Asset/Animation/Core/AnimInstance.cpp:89-110` | `UAnimInstance::EvaluateGraph`의 `OutputLocalPose.resize(...)` 등 `FMatrix` 의존 코드 갱신 (단 `FTransform`은 기본 생성자에서 Identity 효과 — 별도 채움 불필요할 수도 있음, 코드 단계에서 결정) |
| 11 | `Asset/Animation/Core/AnimInstance.cpp:131-145` | `FillBindPose` 구현이 `FBoneInfo::LocalBindPose` (FMatrix) → `FTransform` 변환 |
| 12 | `Asset/Animation/Core/AnimSingleNodeInstance.cpp:29-51` | `UAnimSingleNodeInstance::EvaluateGraph` 본문 — `OutputLocalPose`가 `TArray<FTransform>`이 됨에 맞춰 `resize` 및 `SequencePlayer.Evaluate(...)` 호출은 그대로(시그니처 자동 일치) |
| 13 | `Component/SkeletalMeshComponent.cpp:141, 155` | `ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose())` 호출은 시그니처 변경으로 컴파일 에러 — §2.3 참조 |
| 14 | `Component/SkinnedMeshComponent.h:60` | `ApplyEvaluatedPose` 시그니처를 `const TArray<FTransform>&`로 변경 |
| 15 | `Component/SkinnedMeshComponent.cpp:440-470` | `ApplyEvaluatedPose` 본문에서 `FTransform` → `FMatrix` 변환 후 `LocalBonePoseMatrices`에 기록 (§2.3) |

### 2.3 `FTransform → FMatrix` 변환 경계 — `ApplyEvaluatedPose` 진입 지점에 한정

**결정:** 변환은 **`USkinnedMeshComponent::ApplyEvaluatedPose` 한 곳**에서만 일어난다. 그 외 어느 지점에서도 행렬↔TRS 왕복이 없다.

**근거:**
- 다운스트림(FK 누적, 스킨 행렬 합성)은 `FMatrix` 곱셈에 강하게 의존한다(§1.1). 이걸 `FTransform` 기반으로 재설계하는 것은 파트 A 범위 밖.
- 업스트림(노드 평가)은 TRS 단위 보간이 정확해야 한다(Blend 정확도).
- 두 영역의 경계가 정확히 `ApplyEvaluatedPose` 진입 지점. 여기서 한 번만 변환하면 왕복 없음.

**변경된 `ApplyEvaluatedPose` 의사코드:**

```cpp
void USkinnedMeshComponent::ApplyEvaluatedPose(const TArray<FTransform>& EvaluatedLocalPose)
{
    if (EvaluatedLocalPose.empty())
        return;

    const size_t N = EvaluatedLocalPose.size();
    if (LocalBonePoseMatrices.size() != N)
    {
        LocalBonePoseMatrices.resize(N, FMatrix::Identity);
        BoneOverrideMask.assign(N, false);
        // 첫 동기화: override 마스크는 모두 false, 전부 평가 결과로 채움
        for (size_t i = 0; i < N; ++i)
            LocalBonePoseMatrices[i] = EvaluatedLocalPose[i].ToMatrix();
    }
    else
    {
        const size_t MaskSize = BoneOverrideMask.size();
        for (size_t i = 0; i < N; ++i)
        {
            const bool bOverridden = (i < MaskSize) ? BoneOverrideMask[i] : false;
            if (!bOverridden)
                LocalBonePoseMatrices[i] = EvaluatedLocalPose[i].ToMatrix(); // ★ 변환 경계
        }
    }

    RebuildMeshSpaceBoneMatrices();
    SkinVerticesToReferencePose();
    EnsureRuntimeResources();
    MarkWorldBoundsDirty();
}
```

**`SetBoneLocalPose(int32, const FMatrix&)` API는 시그니처 유지**한다. 사용자 수동 편집은 본질적으로 행렬 표현이며, override 마스크가 켜진 본은 위 루프에서 건너뛰므로 일관성이 유지된다.

### 2.4 SequencePlayer 출력 변경

`AnimGraph.cpp:122-126` 현재:
```cpp
const FVector P = FVector::Lerp(Raw.PosKeys[FrameA],   Raw.PosKeys[FrameB],   Blend);
const FQuat   R = FQuat::Slerp (Raw.RotKeys[FrameA],   Raw.RotKeys[FrameB],   Blend);
const FVector S = FVector::Lerp(Raw.ScaleKeys[FrameA], Raw.ScaleKeys[FrameB], Blend);
OutLocalPose[BoneIdx] = FTransform(P, R, S).ToMatrix();
```

변경 후:
```cpp
const FVector P = FVector::Lerp(Raw.PosKeys[FrameA],   Raw.PosKeys[FrameB],   Blend);
const FQuat   R = FQuat::Slerp (Raw.RotKeys[FrameA],   Raw.RotKeys[FrameB],   Blend);
const FVector S = FVector::Lerp(Raw.ScaleKeys[FrameA], Raw.ScaleKeys[FrameB], Blend);
OutLocalPose[BoneIdx] = FTransform(P, R, S);
```

`.ToMatrix()` 호출 한 번이 사라진다. 그 외 알고리즘(프레임 인덱스 계산, fallback, 캐시 검증)은 동일.

### 2.5 bind pose fallback — `FMatrix` → `FTransform` 변환

`AnimGraph.cpp:19-32`의 `FillBindPose` 헬퍼와 `AnimInstance.cpp:131-145`의 `UAnimInstance::FillBindPose`가 `FBoneInfo::LocalBindPose`(`FMatrix`)를 그대로 `OutLocalPose`(현 `TArray<FMatrix>`)에 대입했다. 시그니처 전환 후에는 `FMatrix`를 `FTransform`으로 분해해야 한다.

**분해 방법 (스캔 §3 결과 활용):**
```cpp
FTransform BindPoseToTransform(const FMatrix& M)
{
    return FTransform(M.GetLocation(), FQuat::FromMatrix(M), M.GetScale());
}
```
- `FMatrix::GetLocation()` — `Matrix.h:109`
- `FQuat::FromMatrix(const FMatrix&)` — `Quat.h:129`, `Quat.cpp:67-107`
- `FMatrix::GetScale()` — `Matrix.h:110`

본 한 개당 분해 1회. **로딩 시점에 1회만 수행되면 충분**(매 프레임이 아님 — `FillBindPose`는 평가 진입 전 1회 호출되며 SequencePlayer 케이스에서는 곧바로 트랙별로 덮어쓰여진다). 따라서 분해 비용은 무시 가능. 단 `Asset/Animation/Core/Skeleton.h`의 `FBoneInfo::LocalBindPose`를 **로딩 시점에 미리 분해한 `FTransform` 캐시**로 들고 있게 하는 최적화도 가능하나, 파트 A 범위 밖. 본 계획에서는 매 호출 분해 방식으로 둔다.

### 2.6 `UAnimSingleNodeInstance` 기존 동작 보존 입증

`UAnimSingleNodeInstance::EvaluateGraph`(`AnimSingleNodeInstance.cpp:29-51`)는 자체 보유 `SequencePlayer.Evaluate(Ctx, OutputLocalPose)`를 호출한다. 시그니처 전환 후:
- SequencePlayer가 같은 TRS 산출 + Slerp를 수행하고 `FTransform`을 `OutputLocalPose`에 적재.
- 컴포넌트 측 `ApplyEvaluatedPose`가 그 `FTransform[i]`을 `.ToMatrix()` 호출로 `FMatrix`로 환원(§2.3).
- `FTransform::ToMatrix()`(`Transform.cpp:3-12`) 내부 합성: `MakeScaleMatrix(S) * Rotation.ToMatrix() * MakeTranslationMatrix(P)`. **기존 `FTransform(P,R,S).ToMatrix()` 호출과 비트 단위로 동치** — 변환 경로가 단지 컴포넌트 진입점으로 이동했을 뿐.

**결론:** SingleNode 시퀀스 재생의 최종 `LocalBonePoseMatrices`는 변환 전후 비트 동치. 기존 동작 깨지지 않는다.

---

## 3. Blend 노드 설계

### 3.1 헤더 인터페이스 스케치

```cpp
// Asset/Animation/Core/AnimGraph_BlendNodes.h (신규 헤더 권고; 기존 AnimGraph.h를 비대화하지 않기 위함)
#pragma once
#include "Asset/Animation/Core/AnimGraph.h"

/**
 * 두 자식 sub-graph의 포즈를 단일 alpha로 본별 TRS 보간한다.
 */
struct FAnimGraphNode_Blend2 : FAnimGraphNode_Base
{
    std::unique_ptr<FAnimGraphNode_Base> ChildA;
    std::unique_ptr<FAnimGraphNode_Base> ChildB;
    float Alpha = 0.0f; // 0 = ChildA만, 1 = ChildB만, 그 사이 = 보간

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

  private:
    TArray<FTransform> ScratchA; // 평가 간 재사용
    TArray<FTransform> ScratchB;
};

/**
 * N개 자식 sub-graph의 가중 평균. 회전은 누적 Slerp 근사 사용 (§3.5).
 */
struct FAnimGraphNode_BlendN : FAnimGraphNode_Base
{
    TArray<std::unique_ptr<FAnimGraphNode_Base>> Children;
    TArray<float>                                Weights; // size == Children.size()

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

  private:
    TArray<TArray<FTransform>> ChildScratches; // size == Children.size()
};
```

**자식 소유:** `std::unique_ptr<FAnimGraphNode_Base>` — 기존 `AnimGraph::Root` 관습(`AnimGraph.h:62`)과 일치.

### 3.2 스크래치 버퍼 위치 — 노드 멤버 권고

세 가지 안 비교:

| 안 | 장점 | 단점 |
|---|---|---|
| (a) 노드 멤버 보유 (`ScratchA`/`ScratchB` 등) | 할당이 첫 평가 1회만. 캐시 친화적. `FAnimGraphNode_Base::Evaluate`가 non-const라 시그니처상 허용 | 노드가 상태를 가짐 → 직렬화·다중 인스턴스 시 신경 써야 함 |
| (b) 매 평가 지역 변수로 `TArray<FTransform>` 할당 | 노드는 진정 stateless 유지 | 매 프레임 본 개수만큼 할당/해제 — GC 압박 |
| (c) `FAnimEvalContext`에 스크래치 풀 핸들 추가 | 풀 사용으로 전역 최적화 가능 | `FAnimEvalContext` 확장이 호출자 2곳에 전파, 풀 자체를 새로 만들어야 함 — 파트 A 범위 초과 |

**권고: (a) 노드 멤버 보유.** 근거: `FAnimGraphNode_Base::Evaluate`가 이미 non-const(`AnimGraph.h:37`)라 시그니처가 허용하고, 본 개수 단위 `TArray<FTransform>`은 본 200개 기준 약 200×(12+16+12) = 8KB로 부담이 작다. 직렬화 우려는 현재 노드 자체가 직렬화되지 않으므로(코드에서 노드 직렬화 없음) 영향 없음. (c)는 매력적이지만 파트 A에 풀 도입까지 끌어들이는 게 부담.

### 3.3 본별 합성 알고리즘

**공용 합성 유틸 (free function 권고):**

```cpp
// Asset/Animation/Core/AnimPoseUtils.h (신규 헤더 권고)
#pragma once
#include "Math/Transform.h"

/**
 * 두 FTransform을 alpha (0..1) 로 본별 TRS 보간한다.
 * - 위치/스케일: FVector::Lerp
 * - 회전: FQuat::Slerp (최단경로·정규화 포함, Quat.h:80-113)
 */
inline FTransform BlendTransform(const FTransform& A, const FTransform& B, float Alpha)
{
    return FTransform(
        FVector::Lerp(A.Location, B.Location, Alpha),
        FQuat::Slerp(A.Rotation, B.Rotation, Alpha),
        FVector::Lerp(A.Scale,    B.Scale,    Alpha)
    );
}
```

**왜 free function인가:** Blend2 / BlendN / StateMachine transition 합성 세 곳에서 동일 산술을 쓴다. `FTransform`에 멤버를 추가하면 Math 레이어를 건드리게 되어 영향 범위가 커진다. Animation 레이어의 free function이 적절.

### 3.4 Blend2 `Evaluate` 의사코드

```cpp
void FAnimGraphNode_Blend2::Evaluate(const FAnimEvalContext& Ctx, TArray<FTransform>& OutLocalPose)
{
    const size_t N = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
    if (N == 0) { OutLocalPose.clear(); return; }

    OutLocalPose.resize(N);
    ScratchA.resize(N);
    ScratchB.resize(N);

    // 자식 평가 (자식이 nullptr이면 bind pose로 채우는 안전망 — sub-graph 미설정 케이스)
    if (ChildA) ChildA->Evaluate(Ctx, ScratchA); else FillBindPoseTransforms(Ctx.Skeleton, ScratchA);
    if (ChildB) ChildB->Evaluate(Ctx, ScratchB); else FillBindPoseTransforms(Ctx.Skeleton, ScratchB);

    const float A = Clamp(Alpha, 0.0f, 1.0f);

    // 본별 합성
    for (size_t i = 0; i < N; ++i)
        OutLocalPose[i] = BlendTransform(ScratchA[i], ScratchB[i], A);
}
```

`FillBindPoseTransforms`는 §2.5의 분해 헬퍼를 `TArray`에 적용하는 헬퍼. `AnimPoseUtils.h`에 함께 둔다.

### 3.5 BlendN 회전 합성 — 누적 Slerp 근사 (권고)

**문제:** 쿼터니언 가중평균은 닫힌해가 없다(특이성 발생). 일반화된 방법(log/exp 평균, eigenvector 방법)은 비용·복잡도가 모두 높다.

**검토안 비교:**

| 안 | 정확도 | 비용 | 순서 의존성 |
|---|---|---|---|
| (i) Blend2 트리로 펼침 (`((C0×w0 + C1×w1) + C2×w2) ...`) | 산술적으로 누적 Slerp 근사와 동치 | 본별 (N-1)회 Slerp | 있음 (가중치 정규화 순서) |
| (ii) 누적 Slerp 근사 (`acc = Slerp(acc, Cᵢ, wᵢ / Σ_{j≤i} wⱼ)`) | (i)와 사실상 동치 | 본별 (N-1)회 Slerp | 있음 |
| (iii) log/exp 가중 평균 | 정확도 높음, 진정 가중평균 | 본별 log·exp + N차 벡터합 + exp; 비용 큼 | 없음 |

**권고: (ii) 누적 Slerp 근사.** 근거: 비용·구현 난이도가 (i)/(iii) 대비 최저, 정확도는 실시간 캐릭터 애니메이션에서 충분히 검증된 표준 패턴, `FQuat::Slerp`(이미 구현, `Quat.h:80-113`) 재사용. 순서 의존성은 호출자가 `Children`/`Weights` 순서를 안정적으로 유지하면 무관.

**알고리즘 의사코드 (본 1개당):**
```cpp
float SumW = 0.0f;
FVector  PosAcc(0,0,0);
FVector  ScaleAcc(0,0,0);
FQuat    RotAcc = ChildPoses[0].Rotation; // 첫 회전으로 초기화
SumW = max(Weights[0], 0); PosAcc = ChildPoses[0].Location * SumW; ScaleAcc = ChildPoses[0].Scale * SumW;

for (i = 1; i < N; ++i)
{
    const float w = max(Weights[i], 0);
    if (w <= 0) continue;
    SumW += w;
    PosAcc   += ChildPoses[i].Location * w;
    ScaleAcc += ChildPoses[i].Scale * w;
    const float SlerpAlpha = w / SumW;
    RotAcc = FQuat::Slerp(RotAcc, ChildPoses[i].Rotation, SlerpAlpha);
}
if (SumW <= 0) { /* bind pose fallback */ }
else { OutLocalPose[bone] = FTransform(PosAcc / SumW, RotAcc, ScaleAcc / SumW); }
```

위치·스케일은 진짜 가중 평균(수학적 정확), 회전만 누적 Slerp 근사.

### 3.6 경계 케이스 처리

- `Weights.size() != Children.size()` → 평가 직전 정규화 단계에서 검출. 권고: 부족한 인덱스는 weight 0으로 간주, 초과 인덱스는 무시. 단순히 0번 자식만 내보내지 않는다(비대칭 처리 회피).
- `Σ Weights ≤ 0` (모두 0 또는 음수) → bind pose fallback(`FillBindPoseTransforms`)으로 안전 출력. SingleNode 우회 경로 일관성 유지.
- 자식 `nullptr` → §3.4 의사코드대로 bind pose로 자식 포즈 대체. Crash 방지 + 디버깅 시 즉시 가시화.
- `Alpha` 또는 weight 음수 → `Clamp(..., 0, ...)` 후 사용. 음수 weight는 외삽을 의미하므로 차단.

---

## 4. State / Transition 데이터 구조 및 `FAnimGraphNode_StateMachine`

### 4.1 데이터 구조 헤더 스케치

```cpp
// Asset/Animation/Core/AnimGraph_StateMachine.h (신규)
#pragma once
#include "Asset/Animation/Core/AnimGraph.h"

struct FAnimState
{
    FName                                Name;
    std::unique_ptr<FAnimGraphNode_Base> Sub; // 보통 SequencePlayer 또는 Blend
    bool                                 bResetTimeOnEnter = true;
    bool                                 bLooping = true;        // §4.4 looping wrap 정책
    float                                SubLengthHint = 0.0f;   // 0 = wrap 없음(clamp); >0 = fmod wrap
};

enum class EAnimTransitionConditionKind : uint8
{
    TimeElapsed,   // 활성 상태의 누적 체류 시간이 임계치 이상
    BoolVariable,  // UAnimStateMachineInstance::BoolVariables[VarName] == bExpectedValue
    OnNotify,      // ★ 자리만 정의, 평가는 파트 B 대기 (§6 참조)
    Custom         // ★ 자리만 정의, 평가는 파트 B 대기
};

struct FAnimTransitionCondition
{
    EAnimTransitionConditionKind Kind = EAnimTransitionConditionKind::TimeElapsed;
    // TimeElapsed
    float TimeThreshold = 0.0f;
    // BoolVariable
    FName VarName;
    bool  bExpectedValue = true;
    // OnNotify (파트 B)
    FName NotifyName;
};

struct FAnimTransition
{
    int32                            FromStateIndex = -1;
    int32                            ToStateIndex   = -1;
    float                            BlendDuration  = 0.2f;
    TArray<FAnimTransitionCondition> Conditions; // AND 결합
};

struct FAnimGraphNode_StateMachine : FAnimGraphNode_Base
{
    TArray<FAnimState>      States;
    TArray<FAnimTransition> Transitions;
    int32                   InitialStateIndex = 0;

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

  private:
    // 런타임 상태
    int32              ActiveStateIndex      = -1;
    int32              ActiveTransitionIndex = -1;     // -1 = 진행 중 transition 없음
    float              TransitionElapsed     = 0.0f;
    TArray<float>      StateLocalTimes;                // index = state index; 각 상태의 로컬 시간 (§4.4)
    // 스크래치 (Blend와 동일 정책 — §3.2)
    TArray<FTransform> ScratchFrom;
    TArray<FTransform> ScratchTo;
};
```

### 4.2 `UAnimStateMachineInstance` 헤더 스케치

```cpp
// Asset/Animation/Core/AnimStateMachineInstance.h (신규)
#pragma once
#include "Asset/Animation/Core/AnimInstance.h"
#include "Asset/Animation/Core/AnimGraph_StateMachine.h"

class UAnimStateMachineInstance : public UAnimInstance
{
  public:
    DECLARE_CLASS(UAnimStateMachineInstance, UAnimInstance)

    UAnimStateMachineInstance();
    ~UAnimStateMachineInstance() override = default;

    /**
     * 외부(에디터/Lua)가 graph를 구성해 주입한다. 소유권 이전.
     * 내부적으로 AnimGraphPtr->SetRoot(...)에 set한다.
     */
    void SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine> InRoot);

    // === BoolVariables (파트 B Lua 바인딩 대상) ===
    void SetBoolVariable(const FName& Name, bool Value) { BoolVariables[Name] = Value; }
    bool GetBoolVariable(const FName& Name, bool Default = false) const;
    const TMap<FName, bool>& GetBoolVariables() const { return BoolVariables; }

    void InitializeAnimation(USkeleton* InSkeleton) override;

  protected:
    float                                 GetEffectivePlayLength() const override; // §4.5
    const TArray<FAnimNotifyEvent>       *GetActiveNotifies() const override;       // 파트 B
    const UAnimDataModel                 *GetActiveDataModel() const override;      // §4.5

  private:
    TMap<FName, bool> BoolVariables;
};
```

**`BoolVariables` 저장 위치 결정:** **`UAnimStateMachineInstance`에 둔다 (base `UAnimInstance`가 아님).**

**근거:**
- `UAnimSingleNodeInstance`는 변수 저장소가 불필요. base에 넣으면 죽은 멤버가 됨.
- 파트 B의 Lua 바인딩은 StateMachine 인스턴스를 대상으로 한다(매핑 문서). base를 거치지 않아도 됨.
- 향후 다른 파생(예: `UAnimMontageInstance`)이 필요해질 때 그쪽 인스턴스에 별도 저장소를 둘 수 있게 유연성 확보.

### 4.3 `Ctx.DeltaTime` 도입 결정 — `FAnimEvalContext` 확장

**문제:** StateMachine은 transition 경과 시간과 활성 상태 체류 시간을 누적해야 한다. 현재 `FAnimEvalContext`(`AnimGraph.h:22-29`)에는 dt가 없다.

**검토안 비교:**

| 안 | 변경 범위 | 노드의 시간 자율성 |
|---|---|---|
| (a) `FAnimEvalContext`에 `float DeltaTime = 0.0f` 추가 | 호출자 2곳(`AnimInstance.cpp:103-109`, `AnimSingleNodeInstance.cpp:44-50`) 보강 | 노드가 dt를 자유롭게 사용 |
| (b) StateMachine 노드 자체가 `PreviousTimeSeconds` 멤버를 들고 `Ctx.TimeSeconds - PreviousTimeSeconds`로 dt 계산 | 시그니처·호출자 무변경 | 첫 평가에서 dt가 잘못 계산됨(초기화 케어 필요), `Ctx.TimeSeconds`의 의미가 시간이 아닌 경우(루프 wrap 등) 깨짐 |

**권고: (a) `FAnimEvalContext`에 `DeltaTime` 추가.** 근거: 변경 범위가 작고(필드 1개 + 호출자 2곳), Blend 등 다른 노드가 향후 dt가 필요해질 때(예: 부드러운 weight 추적) 재사용 가능. (b)는 `Ctx.TimeSeconds`가 단조 증가가 아닌 경우(`UAnimInstance::Update`의 loop wrap, `AnimInstance.cpp:48-56`) 음수 dt가 나오는 위험이 있어 견고하지 않다.

**구체 변경:**
- `AnimGraph.h:22-29`에 `float DeltaTime = 0.0f;` 추가.
- `AnimInstance.cpp:103-109`의 Ctx 세팅에 `Ctx.DeltaTime = LastDeltaTime;` 추가. `LastDeltaTime`은 `UAnimInstance::Update(DeltaTime)`에서 멤버에 저장하는 방식.
- `AnimSingleNodeInstance.cpp:44-50`에도 동일하게 추가.
- SingleNode 경로는 SequencePlayer가 dt를 무시하므로 동작 동치성 보장.

### 4.4 상태별 시간 — `StateMachine`이 `Ctx.TimeSeconds`를 패치 (★ 핵심 난점 결론)

**문제:** SequencePlayer는 시간을 `Ctx.TimeSeconds`에서만 읽고 stateless다(`AnimGraph.cpp:91`). StateMachine이 "State A 진입 후 0.5초 경과, State B로 막 진입(0초)"를 동시에 표현해야 하는데, 단일 `Ctx.TimeSeconds`로는 표현 불가.

**검토안 비교:**

| 안 | 파트 2 코드 변경 | 복잡도 | 호환성 |
|---|---|---|---|
| (X) SequencePlayer에 `LocalTime` 멤버 추가 (stateless 포기) | **있음** — 파트 2 SequencePlayer 변경 | 낮음 (노드 자체 갱신) | SingleNode 경로 동작에 영향, 검증 부담 큼 |
| (Y) StateMachine이 `StateLocalTimes[]`를 관리하고 자식 호출 직전 `Ctx`를 복사·패치 (`Ctx.TimeSeconds`를 상태 로컬 시간으로 덮어쓰기) | **없음** | 보통 | SingleNode 무영향, SequencePlayer 무영향 |
| (Z) `FAnimEvalContext`에 `float LocalTimeOverride = -1.0f` 추가, SequencePlayer가 ≥0이면 사용 | **있음** — SequencePlayer 변경 | 낮음 | SingleNode 경로에 새 분기, 의미 모호 |

**권고: (Y) StateMachine이 `Ctx`를 복사·패치.** 근거:
- **파트 2 코드를 건드리지 않는 유일한 안.** 사용자 명시 제약(파트 2 보존)을 정확히 충족.
- `FAnimEvalContext`는 5필드 + dt(§4.3)로 가벼워, 복사 비용이 무시 가능.
- SequencePlayer는 "어디서 온 시간인지" 모르고 그대로 평가하므로 의미 충돌 없음.

**구체 동작:** StateMachine은 매 평가마다:
1. `StateLocalTimes[ActiveStateIndex] += Ctx.DeltaTime` (paused면 추가 안 함)
2. transition 진행 중이면 `StateLocalTimes[ToStateIndex] += Ctx.DeltaTime` 도 함께 누적
3. 자식 평가 시 `FAnimEvalContext ChildCtx = Ctx; ChildCtx.TimeSeconds = StateLocalTimes[i];` 후 `Sub->Evaluate(ChildCtx, ScratchX);`

**Looping wrap 문제:** SequencePlayer 자체가 `Ctx.TimeSeconds * FPS` 로 프레임을 계산하므로(`AnimGraph.cpp:91`) `TimeSeconds`가 시퀀스 길이를 넘으면 마지막 프레임에 clamp된다(`AnimGraph.cpp:93`). 즉 자연스러운 "정지" 동작. 만약 상태 sub-graph를 looping시키려면 StateMachine이 `StateLocalTimes[i] = fmod(StateLocalTimes[i], SubLength)`로 wrap을 수행해야 한다. **단 SubLength를 알려면 sub-graph가 SequencePlayer일 때만 그 `UAnimSequence->GetPlayLength()`를 알 수 있다.** 본 계획에서는 다음 정책을 채택:

- `FAnimState`에 `bool bLooping = true;` 추가. 활성 시 `StateLocalTimes[i] = fmod(StateLocalTimes[i], SubLengthHint)` 적용.
- `FAnimState`에 `float SubLengthHint = 0.0f;` 추가(에디터가 채워줌, 0이면 wrap 안 함 → clamp 동작).
- 이렇게 하면 SequencePlayer·Blend 어느 sub-graph도 같은 정책으로 처리되며 노드 타입 introspection이 불필요.

### 4.5 protected 가상 훅 override

- `GetEffectivePlayLength() const`: StateMachine 컨텍스트에서는 의미가 모호하다. 활성 상태의 `SubLengthHint`를 반환하거나, transition 중이면 더 큰 쪽 반환. **본 계획에서는 0을 반환(base 기본값 효과)**해서 `UAnimInstance::Update`(`AnimInstance.cpp:26-87`)의 시간 누적·wrap 로직을 사실상 비활성화한다. 시간 누적은 StateMachine 노드가 §4.4에서 자체 처리하므로 중복을 피한다. **단 `bPaused` 체크가 노드까지 전파돼야 한다 — `Ctx`에 `bPaused`도 함께 실어 보내는 안을 §6에서 미해결로 둔다.**
- `GetActiveNotifies() const`: 파트 B 대기. 본 파트 A에서는 `nullptr` 반환(Notify 발화 없음).
- `GetActiveDataModel() const`: 활성 상태의 sub-graph가 SequencePlayer라면 그 시퀀스의 DataModel을 반환하는 것이 합리적이나, **그 introspection을 노드 타입별로 분기하는 게 부담**. 본 계획에서는 `nullptr` 반환(트랙→본 인덱스 캐시 비활성화)을 권고한다. 대신 **각 SequencePlayer 노드가 자체적으로 트랙→본 캐시를 보유**하는 방향(파트 A 범위 초과 — §6 미해결)으로 향후 확장 가능.

### 4.6 StateMachine `Evaluate` 의사코드

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

    // 1) 시간 누적 (paused는 호출자가 Ctx.DeltaTime=0으로 처리 — §6 #2 권고)
    StateLocalTimes[ActiveStateIndex] += Ctx.DeltaTime;
    ApplyLoopOrClamp(ActiveStateIndex);
    if (ActiveTransitionIndex >= 0)
    {
        const auto& T = Transitions[ActiveTransitionIndex];
        TransitionElapsed += Ctx.DeltaTime;
        StateLocalTimes[T.ToStateIndex] += Ctx.DeltaTime;
        ApplyLoopOrClamp(T.ToStateIndex);
    }

    // 2) 진행 중 transition 완료 판정
    if (ActiveTransitionIndex >= 0 && TransitionElapsed >= Transitions[ActiveTransitionIndex].BlendDuration)
    {
        ActiveStateIndex = Transitions[ActiveTransitionIndex].ToStateIndex;
        if (States[ActiveStateIndex].bResetTimeOnEnter) StateLocalTimes[ActiveStateIndex] = 0.0f;
        ActiveTransitionIndex = -1;
        TransitionElapsed = 0.0f;
    }

    // 3) 진행 중 transition 없으면 발화 조건 검사
    if (ActiveTransitionIndex < 0)
    {
        for (int32 i = 0; i < (int32)Transitions.size(); ++i)
        {
            const auto& T = Transitions[i];
            if (T.FromStateIndex != ActiveStateIndex) continue;
            if (EvaluateConditions(T.Conditions, /*activeStateTime*/ StateLocalTimes[ActiveStateIndex]))
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
        // 단일 상태 평가
        auto Cx = MakeChildCtx(ActiveStateIndex);
        if (States[ActiveStateIndex].Sub) States[ActiveStateIndex].Sub->Evaluate(Cx, OutLocalPose);
        else FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);
    }
    else
    {
        // 두 상태 평가 후 BlendTransform으로 합성 — Blend2와 동일 산술 (공용 유틸 재사용)
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

**`EvaluateConditions`:** `TimeElapsed`(활성 상태 체류 시간 비교)와 `BoolVariable`(`UAnimStateMachineInstance->BoolVariables` 조회) 두 케이스만 이번 단계에서 구현. `OnNotify`/`Custom`은 항상 false 반환(파트 B 대기). 노드가 `UAnimStateMachineInstance*`에 접근하려면 `FAnimEvalContext`에 `class UAnimInstance* OwningInstance = nullptr;` 추가 필요 — §6 #1.

---

## 5. 구현 순서와 의존성

### 5.1 단계별 작업 표

| 단계 | 작업 | 선행 조건 | 검증 마일스톤 | 분리선 |
|---|---|---|---|---|
| **A0** | `BlendTransform` / `FillBindPoseTransforms` 공용 유틸 작성 (`AnimPoseUtils.h`) | — | 단위 테스트(또는 임시 main) | Blend 프롬프트 |
| **A1** | `Evaluate` 시그니처 전환 (§2) — base/SequencePlayer/AnimGraph/UAnimInstance/UAnimSingleNodeInstance/ApplyEvaluatedPose | A0 | **SingleNode 시퀀스 재생이 변환 전후로 비주얼 동치**(육안 확인 + 본별 행렬 dump 비교) | Blend 프롬프트 |
| **A2** | `FAnimGraphNode_Blend2` 구현 (`AnimGraph_BlendNodes.h/.cpp`) | A1 | 두 시퀀스 핸드코딩 graph로 alpha=0/0.5/1 비주얼 확인 | Blend 프롬프트 |
| **A3** | `FAnimGraphNode_BlendN` 구현 (§3.5 누적 Slerp) | A2 | 3개 자식 weight 변경 비주얼 확인. weight=0 fallback 검증 | Blend 프롬프트 |
| **B0** | `FAnimEvalContext::DeltaTime` 추가 + 두 호출자 보강 (§4.3) | A1 | SingleNode 회귀 (동작 동치) | StateMachine 프롬프트 |
| **B1** | `FAnimState`/`FAnimTransitionCondition`/`FAnimTransition` 데이터 구조 (`AnimGraph_StateMachine.h`) | B0 | 컴파일 통과 | StateMachine 프롬프트 |
| **B2** | `FAnimGraphNode_StateMachine` 구현 (§4.6) — TimeElapsed 조건만 우선 | B1, A2(공용 유틸 재사용) | 2-state TimeElapsed transition 비주얼 확인 + transition 중 BlendDuration alpha 확인 | StateMachine 프롬프트 |
| **B3** | `UAnimStateMachineInstance` 구현 + `BoolVariables` 저장소 + `BoolVariable` 조건 평가 활성화 | B2 | 외부 코드(테스트 픽스처)에서 `SetBoolVariable` 호출 → transition 발화 확인 | StateMachine 프롬프트 |
| **B4** | `USkeletalMeshComponent::AnimationMode` 분기 도입 — `EAnimationMode::AnimationStateMachine` 추가 + `CreateObject<UAnimStateMachineInstance>` 경로 (§1.2) | B3 | StateMachine 모드 컴포넌트가 정상 동작, SingleNode 모드 회귀 없음 | StateMachine 프롬프트 |

### 5.2 분리선

- **Blend 구현 프롬프트:** 단계 A0 ~ A3.
- **StateMachine 구현 프롬프트:** 단계 B0 ~ B4. Blend 프롬프트 완료(특히 A2의 `BlendTransform` 유틸과 A1의 시그니처 전환)가 선행.

근거: Blend가 합성 산술을 정립하면 StateMachine transition 합성이 그것을 그대로 재사용한다(§4.6의 4단계). 동시에 시그니처 전환(A1)은 두 프롬프트의 공통 토대라 Blend 프롬프트에 묶어 한 번에 처리한다.

### 5.3 회귀 보장 체크포인트

각 단계 종료 시 다음을 확인:
- A1 종료: `Capoeira.fbm` 류 SingleNode 재생이 시각적으로 변경 없음(이미 작업 트리에 있는 테스트 에셋 활용).
- A2/A3 종료: 새 Blend graph만 영향. SingleNode 회귀 없음(시그니처는 이미 A1에서 전환).
- B2 이후: SingleNode 시퀀스 재생 ↔ StateMachine 모드 전환 시 컴포넌트가 양쪽 모두 정상.

---

## 6. 미해결 결정 사항 / 검증 안 된 가정

| # | 항목 | 영향 섹션·단계 | 비고 |
|---|---|---|---|
| 1 | `FAnimEvalContext`에 `UAnimInstance* OwningInstance` 추가 여부 | §4.6 (BoolVariable 평가), B2/B3 | 추가 안 하면 `BoolVariable` 조건이 인스턴스에 접근 불가 → StateMachine 노드가 별도 콜백 함수 포인터를 받는 방식이 대안. **권고: 추가**. 추가 시 호출자 2곳(`AnimInstance.cpp:103-109`, `AnimSingleNodeInstance.cpp:44-50`) 보강 필요. SingleNode 동작 무영향. |
| 2 | `FAnimEvalContext`에 `bool bPaused` 또는 `bool bEvaluateOnly` 전달 여부 | §4.5 (StateMachine이 시간 누적 자체 처리, `Update`의 paused와 분리) | 전달 안 하면 `Ctx.DeltaTime`을 0으로 세팅하는 책임이 호출자(`UAnimInstance::EvaluateGraph` 직전)에 있음. **권고: 호출자가 `bPaused` 시 `DeltaTime=0` 세팅으로 단순화**. |
| 3 | 각 SequencePlayer 노드의 자체 트랙→본 캐시 보유 여부 | §4.5 (StateMachine은 단일 DataModel을 지정할 수 없음) | 현재 `TrackToBoneIndex`는 `UAnimInstance` 1개만 보유(`AnimInstance.h:97`). StateMachine sub-graph의 SequencePlayer마다 시퀀스가 다르면 1개 캐시로는 부족. **파트 A 범위 초과 — 본 계획에서는 `Ctx.TrackToBoneIndex`를 그대로 두고, 각 SequencePlayer가 매 평가마다 `Ctx.DataModel`의 트랙 이름을 직접 lookup하는 fallback이 필요해질 수 있다는 사실만 기록.** 다음 계획에서 `FAnimGraphNode_SequencePlayer`에 노드별 시퀀스 참조와 캐시를 도입하는 안을 정식 검토. |
| 4 | `OnNotify` 조건 평가 | §4.1 (조건 enum), B2 이후 | **파트 B 대기.** Notify dispatch 인프라와 함께 다음 단계에서 설계. 본 계획서는 enum 자리와 데이터 필드(`NotifyName`)만 둠. |
| 5 | `BoolVariables`의 Lua 바인딩 경로 | §4.2 | **파트 B 대기.** `UAnimStateMachineInstance::SetBoolVariable`/`GetBoolVariable` 시그니처만 미리 노출. |
| 6 | `FAnimGraphNode_*` 직렬화 / 에디터 graph 정의 포맷 | 전반 | 본 계획서 범위 외. 현재는 외부 코드(테스트·로더)가 graph를 코드로 구성해 `SetStateMachineGraph(...)`로 주입하는 것을 전제. |
| 7 | 런타임 중 `AnimationMode` 변경 시 기존 인스턴스 해제 (§1.6) | 단계 B4 | 본 파트 A 범위 외 버그 — 별도 작업으로 분리 권장. B4에서 새 모드 추가 시에도 누수 가능성이 동일하게 존재한다는 점만 명시. |

---

## 7. 자평 — 이 계획서로 충분한가

본 계획서는 (1) 시그니처 전환의 정확한 파일·라인 영향, (2) `FTransform`→`FMatrix` 변환의 유일 경계 지점, (3) Blend2/BlendN의 산술과 스크래치 정책, (4) StateMachine의 상태별 시간이라는 난제에 대한 파트 2 무영향 해결안, (5) Blend → StateMachine 의존 순서까지 구현 프롬프트가 곧바로 코드를 쓸 수 있는 수준으로 정리했다. 본 계획서 작성 중 추가 스캔이 필요했던 항목(`ApplyEvaluatedPose` 내부, `Skeleton.h`, `Transform.h`)은 모두 직접 확인했다. **6절 미해결 7개 중 #1·#2·#3은 구현 단계에서 본 계획서 권고대로 처리하면 추가 결정 없이 진행 가능**하며, #4·#5는 파트 B 대기로 분리됐고, #6·#7은 파트 A 범위 외다. 추가 스캔 없이 구현 프롬프트 분리(Blend / StateMachine)로 곧장 진입 가능한 상태이다.
