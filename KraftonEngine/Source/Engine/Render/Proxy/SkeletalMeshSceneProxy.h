#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"

class USkinnedMeshComponent;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent);

	void UpdateMaterial() override;
	void UpdateMesh() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

	bool WantsBoneWeightHeatmap() const override { return bBoneWeightHeatmapActive; }
	
private:
	USkinnedMeshComponent* GetSkinnedMeshComponent() const;
	void RebuildSectionDraws();
	
	bool bBoneWeightHeatmapActive = false;
};
