#include "Component/SkeletalMeshComponent.h"

#include "Asset/Animation/Runtime/AnimationRuntime.h"
#include "Object/ObjectFactory.h"

#include <cmath>

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    USkinnedMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (ApplyBakedAnimation(DeltaTime))
    {
        return;
    }

    // ApplyDebugRandomBoneAnimation(DeltaTime);
}

void USkeletalMeshComponent::PlayAnimation(UAnimSequence* NewAnimToPlay, bool bLooping)
{
    SetAnimation(NewAnimToPlay);
    bBakedAnimLooping = bLooping;
    bBakedAnimPaused = false;
    BakedAnimTime = 0.0f;
}

void USkeletalMeshComponent::SetAnimation(UAnimSequence* NewAnimToPlay)
{
    AnimToPlay = NewAnimToPlay;
    BakedAnimClipIndex = 0;
}

bool USkeletalMeshComponent::ApplyBakedAnimation(float DeltaTime)
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset() || !AnimToPlay || !AnimToPlay->IsValidSequence())
    {
        return false;
    }

    const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
    if (Asset->Bones.empty())
    {
        return false;
    }

    const FAnimationClip& Clip = AnimToPlay->GetAnimationClip();
    if (!bBakedAnimPaused)
    {
        BakedAnimTime += DeltaTime * BakedAnimPlaybackSpeed;
        if (bBakedAnimLooping && Clip.Duration > 0.0f)
        {
            BakedAnimTime = std::fmod(BakedAnimTime, Clip.Duration);
            if (BakedAnimTime < 0.0f)
            {
                BakedAnimTime += Clip.Duration;
            }
        }
        else
        {
            if (BakedAnimTime > Clip.Duration)
            {
                BakedAnimTime = Clip.Duration;
                bBakedAnimPaused = true;
            }
            if (BakedAnimTime < 0.0f)
            {
                BakedAnimTime = 0.0f;
                bBakedAnimPaused = true;
            }
        }
    }

    return EvaluateAnimationPose(Clip, BakedAnimTime);
}

bool USkeletalMeshComponent::EvaluateAnimationPose(const FAnimationClip& Clip, float TimeSeconds)
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return false;
    }

    const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
    if (!FAnimationRuntime::SampleLocalPose(Clip, TimeSeconds, Asset->Bones, LocalBonePoseMatrices))
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
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return;
    }

    const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
    const TArray<FBoneInfo>& Bones = Asset->Bones;
    if (Bones.empty())
    {
        return;
    }

    DebugBoneAnimTime += DeltaTime;
    LocalBonePoseMatrices.resize(Bones.size(), FMatrix::Identity);

    constexpr float MaxAngleRadians = 15.0f * 3.1415926535f / 180.0f;
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
    {
        const FBoneInfo& Bone = Bones[BoneIndex];
        FMatrix AnimatedLocal = Bone.LocalBindPose;

        if (Bone.ParentIndex >= 0)
        {
            const float Phase = static_cast<float>((BoneIndex * 37) % 113) * 0.137f;
            const float Frequency = 0.8f + static_cast<float>((BoneIndex * 17) % 5) * 0.21f;
            const float Angle = sinf(DebugBoneAnimTime * Frequency + Phase) * MaxAngleRadians;

            const FVector Axis(
                static_cast<float>((BoneIndex % 3) == 0),
                static_cast<float>((BoneIndex % 3) == 1),
                static_cast<float>((BoneIndex % 3) == 2));
            AnimatedLocal = FMatrix::MakeRotationAxis(Axis, Angle) * Bone.LocalBindPose;
        }

        LocalBonePoseMatrices[BoneIndex] = AnimatedLocal;
    }

    RebuildMeshSpaceBoneMatrices();
    SkinVerticesToReferencePose();
    EnsureRuntimeResources();
}
