/**
 * 애니메이션 그래프의 평가 진입점과 노드 베이스를 정의한다.
 *
 * 파트 2에서는 단일 시퀀스 샘플링 노드(FAnimGraphNode_SequencePlayer)와 Root 슬롯 한 개만 둔다.
 * 파트 3에서 블렌딩/스테이트머신 노드가 같은 베이스 시그니처로 추가될 수 있도록 인터페이스 경계를 확정한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"

#include <memory>

class USkeleton;
class UAnimDataModel;
class UAnimSequence;

/**
 * AnimGraph 노드가 한 프레임 평가에 필요한 입력값 묶음이다.
 */
struct FAnimEvalContext
{
    const USkeleton      *Skeleton         = nullptr;
    const UAnimDataModel *DataModel        = nullptr; // 단일 시퀀스 경로 단축용
    const UAnimSequence  *Sequence         = nullptr; // 향후 노드가 시퀀스 메타에 접근할 때 사용
    const TArray<int32>  *TrackToBoneIndex = nullptr; // 트랙 idx -> 본 idx
    float                 TimeSeconds      = 0.0f;
};

/**
 * 모든 AnimGraph 노드의 추상 베이스. 파트 3 확장 시 같은 시그니처로 파생한다.
 */
struct FAnimGraphNode_Base
{
    virtual ~FAnimGraphNode_Base() = default;
    virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose) = 0;
};

/**
 * 단일 시퀀스를 시간에 따라 샘플링해 로컬 포즈를 채우는 노드.
 *
 * Option B(TRS 분리 키 + Slerp 회전 보간)로 구현되어 있으며 LocalMatrixKeys는 사용하지 않는다.
 */
struct FAnimGraphNode_SequencePlayer : FAnimGraphNode_Base
{
    void Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose) override;
};

/**
 * Root 노드 슬롯 한 개만 가진 최소 그래프. 파트 3에서 다중 노드/연결 표현으로 확장 예정.
 */
class AnimGraph
{
  public:
    void SetRoot(std::unique_ptr<FAnimGraphNode_Base> Node) { Root = std::move(Node); }
    FAnimGraphNode_Base *GetRoot() const { return Root.get(); }

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose);

  private:
    std::unique_ptr<FAnimGraphNode_Base> Root;
};
