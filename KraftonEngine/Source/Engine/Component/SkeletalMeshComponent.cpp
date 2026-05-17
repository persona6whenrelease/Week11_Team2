#include "Component/SkeletalMeshComponent.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Runtime/AnimationRuntime.h"
#include "Asset/Animation/Core/Skeleton.h"

#include <algorithm>
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
    SetBakedAnimationEvaluationEnabled(NewAnimToPlay != nullptr);
    BakedAnimTime = 0.0f;
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset *NewAnimToPlay)
{
    AnimToPlay = NewAnimToPlay;
    BakedAnimTime = 0.0f;
    SetBakedAnimationEvaluationEnabled(NewAnimToPlay != nullptr);
}

void USkeletalMeshComponent::Play(bool bLooping)
{
    bBakedAnimLooping = bLooping;
    bBakedAnimPaused = false;
    SetBakedAnimationEvaluationEnabled(AnimToPlay != nullptr);
}

void USkeletalMeshComponent::Stop()
{
    bBakedAnimPaused = true;
}

bool USkeletalMeshComponent::SetBoneLocalPose(int32 BoneIndex, const FMatrix &LocalPose)
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeleton())
    {
        return false;
    }

    const TArray<FBoneInfo> &Bones = SkeletalMesh->GetSkeleton()->GetBones();
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
    {
        return false;
    }

    if (ManualBonePoseOverrides.size() != Bones.size())
    {
        ManualBonePoseOverrides.resize(Bones.size(), FMatrix::Identity);
        bManualBonePoseOverrides.resize(Bones.size(), false);
    }

    ManualBonePoseOverrides[BoneIndex] = LocalPose;
    bManualBonePoseOverrides[BoneIndex] = true;

    return USkinnedMeshComponent::SetBoneLocalPose(BoneIndex, LocalPose);
}

void USkeletalMeshComponent::ClearManualBonePoseOverrides()
{
    ManualBonePoseOverrides.clear();
    bManualBonePoseOverrides.clear();
}

void USkeletalMeshComponent::SetBakedAnimationEvaluationEnabled(bool bEnabled)
{
    bBakedAnimationEvaluationEnabled = bEnabled;
    if (bEnabled)
    {
        ClearManualBonePoseOverrides();
    }
}

void USkeletalMeshComponent::OnManualBonePoseEdited()
{
    if (AnimToPlay)
    {
        bBakedAnimPaused = true;
        SetBakedAnimationEvaluationEnabled(false);
    }
}

bool USkeletalMeshComponent::ApplyBakedAnimation(float DeltaTime)
{
    UAnimSequence *Sequence = Cast<UAnimSequence>(AnimToPlay);
    if (!bBakedAnimationEvaluationEnabled || !Sequence || !Sequence->IsValidSequence())
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

    ApplyManualBonePoseOverrides();
    RebuildMeshSpaceBoneMatrices();
    SkinVerticesToReferencePose();
    EnsureRuntimeResources();
    MarkWorldBoundsDirty();
    return true;
}

void USkeletalMeshComponent::ApplyManualBonePoseOverrides()
{
    if (ManualBonePoseOverrides.empty() || bManualBonePoseOverrides.empty())
    {
        return;
    }

    const int32 PoseCount = static_cast<int32>(LocalBonePoseMatrices.size());
    const int32 OverrideCount =
        (std::min)(PoseCount, static_cast<int32>(ManualBonePoseOverrides.size()));

    for (int32 BoneIndex = 0; BoneIndex < OverrideCount; ++BoneIndex)
    {
        if (BoneIndex < static_cast<int32>(bManualBonePoseOverrides.size()) &&
            bManualBonePoseOverrides[BoneIndex])
        {
            LocalBonePoseMatrices[BoneIndex] = ManualBonePoseOverrides[BoneIndex];
        }
    }
}

void USkeletalMeshComponent::ApplyDebugRandomBoneAnimation(float DeltaTime)
{
    // TODO
}
