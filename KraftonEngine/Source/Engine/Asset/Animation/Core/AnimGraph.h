/**
 * 애니메이션 그래프의 평가 진입점과 노드 베이스를 정의한다.
 *
 * 파트 2에서는 단일 시퀀스 샘플링 노드(FAnimGraphNode_SequencePlayer)와 Root 슬롯 한 개만 둔다.
 * 파트 3에서 블렌딩/스테이트머신 노드가 같은 베이스 시그니처로 추가될 수 있도록 인터페이스 경계를 확정한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Transform.h"

#include <memory>

class USkeleton;
class UAnimDataModel;
class UAnimSequence;
class UAnimInstance;

/**
 * AnimGraph 노드가 한 프레임 평가에 필요한 입력값 묶음이다.
 *
 * 시퀀스/DataModel/트랙-본 캐시는 노드가 자체 보유한다(예: FAnimGraphNode_SequencePlayer::SetSequence).
 * Ctx는 평가 시점에 노드들이 공통으로 참조해야 할 최소값만 운반한다.
 * TimeSeconds는 자식 시간으로 패치될 수 있다 (예: StateMachine 상태별 로컬 시간).
 */
struct FAnimEvalContext
{
    const USkeleton *Skeleton       = nullptr;
    float            TimeSeconds    = 0.0f;
    float            DeltaTime      = 0.0f;   // paused일 때 호출자가 0으로 세팅
    UAnimInstance   *OwningInstance = nullptr; // StateMachine 노드가 변수/notify 조회용으로 dynamic_cast
};

/**
 * 모든 AnimGraph 노드의 추상 베이스. 파트 3 확장 시 같은 시그니처로 파생한다.
 */
struct FAnimGraphNode_Base
{
    virtual ~FAnimGraphNode_Base() = default;
    virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) = 0;
};

/**
 * 단일 시퀀스를 시간에 따라 샘플링해 로컬 포즈를 채우는 노드.
 *
 * Option B(TRS 분리 키 + Slerp 회전 보간)로 구현되어 있으며 LocalMatrixKeys는 사용하지 않는다.
 */
struct FAnimGraphNode_SequencePlayer : FAnimGraphNode_Base
{
    /**
     * 평가에 필요한 입력 시퀀스를 set하고 트랙 -> 본 인덱스 캐시를 빌드한다.
     * InSkeleton 또는 InSequence가 nullptr/invalid면 캐시를 비워둔다(평가 시 bind pose).
     * 노드는 두 포인터를 own하지 않는다 — ref hold.
     */
    void SetSequence(const USkeleton *InSkeleton, const UAnimSequence *InSequence);

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

    const UAnimSequence  *Sequence  = nullptr; // ref, not owned
    const UAnimDataModel *DataModel = nullptr; // ref, not owned (Sequence->GetDataModel() 캐시)
    TArray<int32>         TrackToBoneIndex;    // 값 보유 (캐시)
};

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

/**
 * N개 자식 sub-graph의 가중 합성 노드.
 * - 위치/스케일: 정밀 가중평균 (Σ wᵢ·vᵢ / Σ wᵢ).
 * - 회전: 누적 Slerp 근사 — RotAcc = Slerp(RotAcc, Cᵢ.Rotation, wᵢ / Σ_{j≤i} wⱼ).
 * - Children[c]가 nullptr이면 bind pose로 자식 포즈 대체.
 * - Weights.size() != Children.size(): 부족 인덱스는 0, 초과 인덱스는 무시.
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

/**
 * Root 노드 슬롯 한 개만 가진 최소 그래프. 파트 3에서 다중 노드/연결 표현으로 확장 예정.
 */
class AnimGraph
{
  public:
    void SetRoot(std::unique_ptr<FAnimGraphNode_Base> Node) { Root = std::move(Node); }
    FAnimGraphNode_Base *GetRoot() const { return Root.get(); }

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose);

  private:
    std::unique_ptr<FAnimGraphNode_Base> Root;
};
