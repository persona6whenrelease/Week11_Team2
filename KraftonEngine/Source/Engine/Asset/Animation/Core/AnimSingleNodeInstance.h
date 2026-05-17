/**
 * 단일 UAnimSequence 재생을 담당하는 가장 단순한 UAnimInstance 구체 클래스.
 *
 * Root 노드 슬롯에 FAnimGraphNode_SequencePlayer 한 개를 두고, 시퀀스 set 시점에 트랙 -> 본 인덱스
 * 캐시만 재빌드한다. 블렌딩과 스테이트 머신은 파트 3에서 별도 인스턴스 또는 노드로 추가된다.
 */

#pragma once

#include "Asset/Animation/Core/AnimInstance.h"

class UAnimSequence;

class UAnimSingleNodeInstance : public UAnimInstance
{
  public:
    DECLARE_CLASS(UAnimSingleNodeInstance, UAnimInstance)

    UAnimSingleNodeInstance();
    ~UAnimSingleNodeInstance() override = default;

    void           SetAnimation(UAnimSequence *InSequence);
    UAnimSequence *GetAnimation() const { return CurrentSequence; }

    void InitializeAnimation(USkeleton *InSkeleton) override;
    void EvaluateGraph() override;

  protected:
    float                                 GetEffectivePlayLength() const override;
    const TArray<FAnimNotifyEvent>       *GetActiveNotifies() const override;
    const UAnimDataModel                 *GetActiveDataModel() const override;

  private:
    UAnimSequence                *CurrentSequence = nullptr; // ref, not owned
    FAnimGraphNode_SequencePlayer SequencePlayer;            // 단일 시퀀스 샘플링 — graph 트리 우회를 위해 값 보유
};
