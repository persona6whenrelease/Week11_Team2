#pragma once

#include "Component/SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override = default;

	float GetBakedAnimTime() const { return BakedAnimTime; }
	void SetBakedAnimTime(float InTime) { BakedAnimTime = InTime; }

	int32 GetBakedAnimClipIndex() const { return BakedAnimClipIndex; }
	void SetBakedAnimClipIndex(int32 InIndex) { BakedAnimClipIndex = InIndex; }

	bool IsBakedAnimPaused() const { return bBakedAnimPaused; }
	void SetBakedAnimPaused(bool bInPaused) { bBakedAnimPaused = bInPaused; }

	float GetBakedAnimPlaybackSpeed() const { return BakedAnimPlaybackSpeed; }
	void SetBakedAnimPlaybackSpeed(float InSpeed) { BakedAnimPlaybackSpeed = InSpeed; }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void ApplyDebugRandomBoneAnimation(float DeltaTime);
	bool ApplyBakedAnimation(float DeltaTime);

	float DebugBoneAnimTime = 0.0f;
	float BakedAnimTime = 0.0f;
	int32 BakedAnimClipIndex = 0;
	bool bBakedAnimPaused = false;
	float BakedAnimPlaybackSpeed = 1.0f;
};
