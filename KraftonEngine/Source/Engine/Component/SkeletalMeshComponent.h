#pragma once

#include "Component/SkinnedMeshComponent.h"
#include "Asset/Animation/Core/AnimInstance.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimationAsset;
class UAnimSequence;
class USkeleton;
class UAnimInstance;

// AnimInstance 종류별 분기. 신규 모드 추가 시 USkeletalMeshComponent::EnsureAnimInstance switch에 case도 함께.
enum class EAnimationMode
{
    AnimationSingleNode,
    AnimationStateMachine
};

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
  public:
    GENERATED_BODY()

    USkeletalMeshComponent() = default;
    ~USkeletalMeshComponent() override;

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
    	if (AnimInstance)
    	{
    		AnimInstance->SetEvaluationTime(InTime);
    		RefreshAnimationPose();
    	}
    }

    bool IsBakedAnimPaused() const
    {
        return bBakedAnimPaused;
    }
    void SetBakedAnimPaused(bool bInPaused)
    {
        bBakedAnimPaused = bInPaused;
        if (AnimInstance) AnimInstance->SetPaused(bInPaused);
    }

    float GetBakedAnimPlaybackSpeed() const
    {
        return BakedAnimPlaybackSpeed;
    }
    void SetBakedAnimPlaybackSpeed(float InSpeed)
    {
        BakedAnimPlaybackSpeed = InSpeed;
        if (AnimInstance) AnimInstance->SetPlaybackSpeed(InSpeed);
    }
	void RefreshAnimationPose();

  protected:
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction &ThisTickFunction) override;

    /**
     * AnimationMode에 맞춰 AnimInstance를 lazy 생성한다.
     * 이미 생성됐으면 no-op. 런타임 모드 변경 후 자동 재생성은 미지원 — 별도 처리 필요.
     */
    void EnsureAnimInstance();

    float DebugBoneAnimTime = 0.0f;
    float BakedAnimTime = 0.0f;
    bool bBakedAnimPaused = true;
    float BakedAnimPlaybackSpeed = 1.0f;
    bool bBakedAnimLooping = true;

    EAnimationMode AnimationMode = EAnimationMode::AnimationSingleNode;
    UAnimationAsset *AnimToPlay = nullptr;

    // 파트 2: 시간 누적과 그래프 평가는 AnimInstance에 위임한다. 컴포넌트가 라이프사이클 소유.
    UAnimInstance *AnimInstance = nullptr;
};
