#include "Component/SkeletalMeshComponent.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/Skeleton.h"

#include <cmath>

#include <cmath>

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction &ThisTickFunction)
{
    // TODO
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset *NewAnimToPlay, bool bLooping)
{
    // TODO
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset *NewAnimToPlay)
{
    // TODO
}

void USkeletalMeshComponent::Play(bool bLooping)
{
    // TODO
}

void USkeletalMeshComponent::Stop()
{
    // TODO
}

bool USkeletalMeshComponent::ApplyBakedAnimation(float DeltaTime)
{
    // TODO
    return false;
}

bool USkeletalMeshComponent::EvaluateAnimationPose(const UAnimSequence *Sequence, float TimeSeconds)
{
    // TODO
    return false;
}

void USkeletalMeshComponent::ApplyDebugRandomBoneAnimation(float DeltaTime)
{
    // TODO
}
