#include "Component/SkeletalMeshComponent.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Runtime/AnimationRuntime.h"
#include "Asset/Animation/Core/Skeleton.h"

#include <cmath>

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction &ThisTickFunction)
{
    USkinnedMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ApplyBakedAnimation(DeltaTime);
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset *NewAnimToPlay, bool bLooping)
{
    SetAnimation(NewAnimToPlay);
    bBakedAnimLooping = bLooping;
    bBakedAnimPaused = false;
    BakedAnimTime = 0.0f;
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset *NewAnimToPlay)
{
    AnimToPlay = NewAnimToPlay;
    BakedAnimTime = 0.0f;
}

void USkeletalMeshComponent::Play(bool bLooping)
{
    bBakedAnimLooping = bLooping;
    bBakedAnimPaused = false;
}

void USkeletalMeshComponent::Stop()
{
    bBakedAnimPaused = true;
}

bool USkeletalMeshComponent::ApplyBakedAnimation(float DeltaTime)
{
    UAnimSequence *Sequence = Cast<UAnimSequence>(AnimToPlay);
    if (!Sequence || !Sequence->IsValidSequence())
    {
        return false;
    }

    const float Duration = Sequence->GetPlayLength();
    if (!bBakedAnimPaused)
    {
        BakedAnimTime += DeltaTime * BakedAnimPlaybackSpeed;
        if (bBakedAnimLooping && Duration > 0.0f)
        {
            BakedAnimTime = std::fmod(BakedAnimTime, Duration);
            if (BakedAnimTime < 0.0f)
            {
                BakedAnimTime += Duration;
            }
        }
        else
        {
            if (BakedAnimTime > Duration)
            {
                BakedAnimTime = Duration;
                bBakedAnimPaused = true;
            }
            if (BakedAnimTime < 0.0f)
            {
                BakedAnimTime = 0.0f;
                bBakedAnimPaused = true;
            }
        }
    }

    return EvaluateAnimationPose(Sequence, BakedAnimTime);
}

bool USkeletalMeshComponent::EvaluateAnimationPose(const UAnimSequence *Sequence, float TimeSeconds)
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeleton() || !Sequence)
    {
        return false;
    }

    const TArray<FBoneInfo> &Bones = SkeletalMesh->GetSkeleton()->GetBones();
    if (!FAnimationRuntime::SampleLocalPose(Sequence, TimeSeconds, Bones, LocalBonePoseMatrices))
    {
        return false;
    }

    RebuildMeshSpaceBoneMatrices();
    SkinVerticesToReferencePose();
    EnsureRuntimeResources();
    MarkWorldBoundsDirty();
    return true;
}

void USkeletalMeshComponent::ApplyDebugRandomBoneAnimation(float DeltaTime)
{
    // TODO
}
