#include "Asset/Animation/Core/AnimInstance.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/AnimationTypes.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Object/ObjectFactory.h"

#include <algorithm>
#include <cmath>

IMPLEMENT_CLASS(UAnimInstance, UObject)

UAnimInstance::UAnimInstance() = default;
UAnimInstance::~UAnimInstance() = default;

void UAnimInstance::InitializeAnimation(USkeleton *InSkeleton)
{
    Skeleton = InSkeleton;
    ResetTime();
    TriggeredNotifiesThisFrame.clear();

    FillBindPose();
    RebuildTrackToBoneIndex();
}

void UAnimInstance::Update(float DeltaTime)
{
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
    Ctx.Skeleton         = Skeleton;
    Ctx.DataModel        = GetActiveDataModel();
    Ctx.TrackToBoneIndex = &TrackToBoneIndex;
    Ctx.TimeSeconds      = CurrentTime;

    AnimGraphPtr->Evaluate(Ctx, OutputLocalPose);
}

void UAnimInstance::RebuildTrackToBoneIndex()
{
    TrackToBoneIndex.clear();

    const UAnimDataModel *Model = GetActiveDataModel();
    if (!Model || !Skeleton)
    {
        return;
    }

    const TArray<FBoneAnimationTrack> &Tracks = Model->GetBoneAnimationTracks();
    TrackToBoneIndex.resize(Tracks.size());
    for (size_t TIdx = 0; TIdx < Tracks.size(); ++TIdx)
    {
        const FString BoneName = Tracks[TIdx].Name.ToString();
        TrackToBoneIndex[TIdx] = Skeleton->FindBoneIndexByName(BoneName);
    }
}

void UAnimInstance::FillBindPose()
{
    if (!Skeleton)
    {
        OutputLocalPose.clear();
        return;
    }

    const TArray<FBoneInfo> &Bones = Skeleton->GetBones();
    OutputLocalPose.resize(Bones.size());
    for (size_t i = 0; i < Bones.size(); ++i)
    {
        OutputLocalPose[i] = Bones[i].LocalBindPose;
    }
}
