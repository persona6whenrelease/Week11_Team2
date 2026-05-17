#pragma once

#include "Component/SkinnedMeshComponent.h"
#include "Asset/Animation/Core/AnimSequence.h"

class UAnimationAsset;
class UAnimSequence;
class USkeleton;

enum class EAnimationMode
{
    AnimationSingleNode
};

class USkeletalMeshComponent : public USkinnedMeshComponent
{
  public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

    USkeletalMeshComponent() = default;
    ~USkeletalMeshComponent() override = default;

    /**
     * 단일 애니메이션 에셋을 재생 (AnimationSingleNode만 상정)
     */
    void PlayAnimation(UAnimationAsset *NewAnimToPlay, bool bLooping = true);
    void SetAnimation(UAnimationAsset *NewAnimToPlay);
    UAnimationAsset *GetAnimation() const
    {
        return AnimToPlay;
    }

    void SetAnimationMode(EAnimationMode InAnimationMode)
    {
        AnimationMode = InAnimationMode;
    }
    EAnimationMode GetAnimationMode() const
    {
        return AnimationMode;
    }

    void Play(bool bLooping = true);
    void Stop();

    /**
     * UAnimSequence -> UAnimDataModel -> USkeleton 기준으로 현재 로컬 포즈를 평가한다.
     */
    bool EvaluateAnimationPose(const UAnimSequence *Sequence, float TimeSeconds);

    float GetBakedAnimTime() const
    {
        return BakedAnimTime;
    }
    void SetBakedAnimTime(float InTime)
    {
        BakedAnimTime = InTime;
    }

    bool IsBakedAnimPaused() const
    {
        return bBakedAnimPaused;
    }
    void SetBakedAnimPaused(bool bInPaused)
    {
        bBakedAnimPaused = bInPaused;
    }

    float GetBakedAnimPlaybackSpeed() const
    {
        return BakedAnimPlaybackSpeed;
    }
    void SetBakedAnimPlaybackSpeed(float InSpeed)
    {
        BakedAnimPlaybackSpeed = InSpeed;
    }

  protected:
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction &ThisTickFunction) override;
    void ApplyDebugRandomBoneAnimation(float DeltaTime);
    bool ApplyBakedAnimation(float DeltaTime);

    float DebugBoneAnimTime = 0.0f;
    float BakedAnimTime = 0.0f;
    bool bBakedAnimPaused = true;
    float BakedAnimPlaybackSpeed = 1.0f;
    bool bBakedAnimLooping = true;

    EAnimationMode AnimationMode = EAnimationMode::AnimationSingleNode;
    UAnimationAsset *AnimToPlay = nullptr;
};
