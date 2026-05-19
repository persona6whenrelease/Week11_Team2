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

	// GPU Skinning: SkinCacheSRV가 유효하고 컴포넌트가 GPU 모드일 때만 true.
	bool IsGPUSkinned() const override { return bUseGPUSkinning && SkinCacheSRV != nullptr; }
	ID3D11ShaderResourceView* GetSkinCacheSRV() const override { return SkinCacheSRV; }

private:
	USkinnedMeshComponent* GetSkinnedMeshComponent() const;
	void RebuildSectionDraws();

	bool bBoneWeightHeatmapActive = false;

	// 매 프레임 UpdatePerViewport에서 컴포넌트로부터 캐싱
	bool bUseGPUSkinning = false;
	ID3D11ShaderResourceView* SkinCacheSRV = nullptr;
};
