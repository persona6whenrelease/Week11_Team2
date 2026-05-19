#include "Asset/Animation/Core/AnimInstance.h"

#include "Asset/Animation/Core/AnimPoseUtils.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Object/ObjectFactory.h"

#include <cmath>

REGISTER_FACTORY(UAnimInstance)

UAnimInstance::UAnimInstance() = default;
UAnimInstance::~UAnimInstance() = default;

void UAnimInstance::InitializeAnimation(USkeleton *InSkeleton)
{
    Skeleton = InSkeleton;
    ResetTime();
    TriggeredNotifiesThisFrame.clear();

    FillBindPose();
}

void UAnimInstance::Update(float DeltaTime)
{
    LastDeltaTime = bPaused ? 0.0f : DeltaTime;
    TriggeredNotifiesThisFrame.clear();

    const float Length = GetEffectivePlayLength();
    if (Length <= 0.0f)
    {
        PreviousTime = CurrentTime = 0.0f;
        return;
    }

    if (bPaused)
    {
        // Paused 상태에서도 PreviousTime을 정렬해 두면 외부에서 시간을 강제 변경한 직후
        // Notify 판정에 잘못된 prev/curr 간격이 잡히지 않는다.
        PreviousTime = CurrentTime;
        return;
    }

    PreviousTime = CurrentTime;
    float NewTime = CurrentTime + DeltaTime * PlaybackSpeed;

    if (bLooping)
    {
        float Wrapped = std::fmod(NewTime, Length);
        if (Wrapped < 0.0f)
        {
            Wrapped += Length;
        }
        CurrentTime = Wrapped;
    }
    else
    {
        if (NewTime >= Length)
        {
            CurrentTime = Length;
            bPaused = true;
        }
        else if (NewTime < 0.0f)
        {
            CurrentTime = 0.0f;
            bPaused = true;
        }
        else
        {
            CurrentTime = NewTime;
        }
    }

    // Notify 판정 (dispatch는 파트 3) — IsTriggeredBetween이 prev > curr 루프 wrap 케이스를 처리한다.
    const TArray<FAnimNotifyEvent> *Notifies = GetActiveNotifies();
    if (Notifies)
    {
        for (const FAnimNotifyEvent &Notify : *Notifies)
        {
            if (Notify.IsTriggeredBetween(PreviousTime, CurrentTime, Length))
            {
                TriggeredNotifiesThisFrame.push_back(Notify.NotifyName);
            }
        }
    }
}

void UAnimInstance::EvaluateGraph()
{
    if (!Skeleton || !AnimGraphPtr)
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
    Ctx.Skeleton       = Skeleton;
    Ctx.TimeSeconds    = CurrentTime;
    Ctx.DeltaTime      = LastDeltaTime;
    Ctx.OwningInstance = this;

    AnimGraphPtr->Evaluate(Ctx, OutputLocalPose);
}

void UAnimInstance::FillBindPose()
{
    AnimPoseUtils::FillBindPoseTransforms(Skeleton, OutputLocalPose);
}
