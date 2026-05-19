#include "Asset/Animation/Core/AnimSingleNodeInstance.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Object/ObjectFactory.h"

REGISTER_FACTORY(UAnimSingleNodeInstance)

UAnimSingleNodeInstance::UAnimSingleNodeInstance() = default;

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
    Ctx.Skeleton         = Skeleton;
    Ctx.DataModel        = GetActiveDataModel();
    Ctx.TrackToBoneIndex = &TrackToBoneIndex;
    Ctx.TimeSeconds      = CurrentTime;

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

const UAnimDataModel *UAnimSingleNodeInstance::GetActiveDataModel() const
{
    return CurrentSequence ? CurrentSequence->GetDataModel() : nullptr;
}
