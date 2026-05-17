#include "Asset/Animation/Core/AnimSingleNodeInstance.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(UAnimSingleNodeInstance, UAnimInstance)

UAnimSingleNodeInstance::UAnimSingleNodeInstance()
{
    AnimGraphPtr = std::make_unique<AnimGraph>();
    AnimGraphPtr->SetRoot(std::make_unique<FAnimGraphNode_SequencePlayer>());
}

void UAnimSingleNodeInstance::SetAnimation(UAnimSequence *InSequence)
{
    CurrentSequence = InSequence;
    RebuildTrackToBoneIndex();
    ResetTime();
}

void UAnimSingleNodeInstance::InitializeAnimation(USkeleton *InSkeleton)
{
    UAnimInstance::InitializeAnimation(InSkeleton);
    // base가 RebuildTrackToBoneIndex를 이미 호출하지만, 시퀀스가 이미 set 된 상태에서
    // 스켈레톤이 늦게 들어오는 경우를 위해 안전망 차원에서 한 번 더 호출.
    if (CurrentSequence)
    {
        RebuildTrackToBoneIndex();
    }
}

float UAnimSingleNodeInstance::GetEffectivePlayLength() const
{
    return CurrentSequence ? CurrentSequence->GetPlayLength() : 0.0f;
}

const TArray<FAnimNotifyEvent> *UAnimSingleNodeInstance::GetActiveNotifies() const
{
    return CurrentSequence ? &CurrentSequence->GetNotifies() : nullptr;
}

const UAnimDataModel *UAnimSingleNodeInstance::GetActiveDataModel() const
{
    return CurrentSequence ? CurrentSequence->GetDataModel() : nullptr;
}
