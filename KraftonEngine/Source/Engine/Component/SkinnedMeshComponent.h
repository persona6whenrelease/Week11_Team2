#pragma once

#include "Component/MeshComponent.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/VertexTypes.h"

class FPrimitiveSceneProxy;

class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override = default;

	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	void UpdateWorldAABB() const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetSkeletalMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	const TArray<FMatrix>& GetLocalBonePoseMatrices() const { return LocalBonePoseMatrices; }

	int32 FindBoneIndexByName(const FString& BoneName) const;
	void ResetBonePoseToBindPose();
	bool SetBoneLocalPose(int32 BoneIndex, const FMatrix& LocalPose);
	bool SetBoneLocalPoseByName(const FString& BoneName, const FMatrix& LocalPose);
	const TArray<FMatrix>& GetMeshSpaceBoneMatrices() const { return MeshSpaceBoneMatrices; }

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	
	// === Animation Bone Transform ===
	// 본을 사용자가 직접 수정한 경우 마킹.
	// 마킹된 본은 다음 애니메이션 평가세어 시퀀스 결과로 덮어쓰여지지 않고 사용자 값을 유지함.
	bool IsBoneOverridden(int32 BondIndex) const;
	void ClearBoneOverride(int32 BondIndex);
	void ClearAllBoneOverrides();
	void MarkBoneOverridden(int32 BondIndex);
	// === Bone Weight Heatmap ===
	void SetBoneWeightHeatmapState(bool bEnabled, int32 BoneIndex);
	bool IsBoneWeightHeatmapEnabled() const { return bBoneWeightHeatmapEnabled; }
	int32 GetBoneWeightHeatmapBoneIndex() const {return BoneWeightHeatmapBoneIndex;}
	
protected:
	void CacheLocalBounds();
	void EnsureRuntimeResources();
	void BuildBindPoseRenderVertices();
	void UploadSkinnedVertices();
	void RebuildMeshSpaceBoneMatrices();
	virtual void OnManualBonePoseEdited() {}
	virtual void SkinVerticesToReferencePose();
	// 시퀀스 평가 결과를 LocalBonePoseMatrics에 반영하되, 
	// override 마스크가 켜진 본은 사용자가 수정한 값을 유지함. Tick 흐름에서 매 프레임 호출됨.
	void ApplyEvaluatedPose(const TArray<FMatrix>& EvaluatedLocalPose);
	
	FVector4 ResolveVertexDebugColor(const FSkeletalVertex& SourceVertex) const;
	void ApplyVertexDebugColors();
	
	static float GetBoneWeightForVertex(const FSkeletalVertex& SourceVertex, int32 BoneIndex);
	static FVector4 MakeBoneWeightHeatmapColor(float Weight);
	
	bool bBoneWeightHeatmapEnabled = false;
	int32 BoneWeightHeatmapBoneIndex = -1;

	USkeletalMesh* SkeletalMesh = nullptr;
	FString SkeletalMeshPath = "None";

	TArray<FVertexPNCTT> SkinnedVertices;
	TArray<FMatrix> LocalBonePoseMatrices;
	TArray<FMatrix> MeshSpaceBoneMatrices;
	TArray<bool> BoneOverrideMask; // true = 사용자가 수정.
	FMeshBuffer RuntimeMeshBuffer;

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;
};
