#include "Asset/Animation/Core/AnimSingleNodeInstance.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(UAnimSingleNodeInstance, UAnimInstance)

UAnimSingleNodeInstance::UAnimSingleNodeInstance() = default;

void UAnimSingleNodeInstance::SetAnimation(UAnimSequence *InSequence)
{
    CurrentSequence = InSequence;
    SequencePlayer.SetSequence(Skeleton, CurrentSequence);
    ResetTime();
}

void UAnimSingleNodeInstance::InitializeAnimation(USkeleton *InSkeleton)
{
    UAnimInstance::InitializeAnimation(InSkeleton);
    // 시퀀스가 이미 set 된 상태에서 스켈레톤이 늦게 들어온 경우를 위해 노드에 다시 setting한다.
    if (CurrentSequence)
    {
        SequencePlayer.SetSequence(Skeleton, CurrentSequence);
    }
}

void UAnimSingleNodeInstance::EvaluateGraph()
{
    // graph 트리를 우회해 단일 SequencePlayer를 직접 호출한다. base AnimGraphPtr은 미사용.
    if (!Skeleton)
    {
        OutputLocalPose.clear();
        return;
    }

    const size_t BoneCount = Skeleton->GetBones().size();
    if (OutputLocalPose.size() != BoneCount)
    {
        OutputLocalPose.resize(BoneCount);
    }

    FAnimEvalContext Ctx;
    Ctx.Skeleton    = Skeleton;
    Ctx.TimeSeconds = CurrentTime;

    SequencePlayer.Evaluate(Ctx, OutputLocalPose);
}

float UAnimSingleNodeInstance::GetEffectivePlayLength() const
{
    return CurrentSequence ? CurrentSequence->GetPlayLength() : 0.0f;
}

const TArray<FAnimNotifyEvent> *UAnimSingleNodeInstance::GetActiveNotifies() const
{
    return CurrentSequence ? &CurrentSequence->GetNotifies() : nullptr;
}
