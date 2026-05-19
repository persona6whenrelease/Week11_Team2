#pragma once

#include "Component/MeshComponent.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/VertexTypes.h"
#include "SkinnedMeshComponent.generated.h"

class FPrimitiveSceneProxy;

enum class ESkinningGlobalMode : uint8
{
	Component,
	ForceCPU,
	ForceGPU
};

UCLASS()
class USkinnedMeshComponent : public UMeshComponent
{
public:
	GENERATED_BODY()

	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override;

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

	static void SetGlobalSkinningMode(ESkinningGlobalMode NewMode);
	static ESkinningGlobalMode GetGlobalSkinningMode();
	static const char* GetGlobalSkinningModeName();
	bool ShouldUseGPUSkinning() const;
	void RefreshSkinningForCurrentPose();
	
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

	// === GPU Skinning 노출 (Proxy/Renderer가 읽음) ===
	bool IsUsingGPUSkinning() const { return ShouldUseGPUSkinning(); }
	ID3D11ShaderResourceView* GetSkinCacheSRV() const { return bSkinCacheValid ? SkinCacheSRV : nullptr; }
	
protected:
	void CacheLocalBounds();
	void EnsureRuntimeResources();
	void BuildBindPoseRenderVertices();
	void BuildSkinnedSourceVertices(); // GPU 경로용 정점 생성
	void UploadSkinnedVertices();
	void RebuildMeshSpaceBoneMatrices();
	virtual void OnManualBonePoseEdited() {}
	virtual void SkinVerticesToReferencePose();
	// GPU 경로용 데이터 준비.
	virtual bool PrepareGPUSkinningData();
	
	// 시퀀스 평가 결과를 LocalBonePoseMatrics에 반영하되, 
	// override 마스크가 켜진 본은 사용자가 수정한 값을 유지함. Tick 흐름에서 매 프레임 호출됨.
	void ApplyEvaluatedPose(const TArray<FTransform>& EvaluatedLocalPose);
	
	FVector4 ResolveVertexDebugColor(const FSkeletalVertex& SourceVertex) const;
	void ApplyVertexDebugColors();
	
	static float GetBoneWeightForVertex(const FSkeletalVertex& SourceVertex, int32 BoneIndex);
	static FVector4 MakeBoneWeightHeatmapColor(float Weight);

	// GPU Skinning
	void EnsureGPUSkinningBuffers(); // 메쉬 바뀔 때 한 번씩 (Source/Cache 버퍼 생성)
	bool UpdateBonePaletteCB(); // 매 프레임 SkinMatrix 계산해서 CB 업로드
	bool DispatchSkinningCompute(); // 매 프레임 Compute Dispatch + barrier
	void ReleaseGPUSkinningResources(); // 메쉬 교체/소멸 시 해제
	
	bool bBoneWeightHeatmapEnabled = false;
	int32 BoneWeightHeatmapBoneIndex = -1;

	// CPU↔GPU Skinning 토글. true면 GPU(Compute) 경로, false면 기존 CPU 경로.
	// 인스턴스 단위로 갖는 이유: 같은 SkeletalMesh를 쓰더라도 컴포넌트마다 다르게 선택 가능해야 함.
	bool bUseGPUSkinning = true;

	USkeletalMesh* SkeletalMesh = nullptr;
	FString SkeletalMeshPath = "None";

	TArray<FVertexPNCTT> SkinnedVertices; // CPU 경로. 매 프레임 갱신되는 스키닝 결과 
	TArray<FVertexPNCTT_Skinned> SkinnedSourceVertices; // GPU 경로 입력 (bind pose + bone 정보, 한 번만 생성)
	TArray<FMatrix> LocalBonePoseMatrices;
	TArray<FMatrix> MeshSpaceBoneMatrices;
	TArray<bool> BoneOverrideMask; // true = 사용자가 수정.
	FMeshBuffer RuntimeMeshBuffer;

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;
	
	// GPU Skinning: Bone Palette CB (ConstantBuffer.hlsli의 BonePaletteBuffer와 1:1)
	static constexpr int32 MAX_SKINNING_BONES = 256;
	struct FBonePaletteConstants
	{
		FMatrix BoneMatrices[MAX_SKINNING_BONES];
		uint32  NumSkinningVertices = 0;
		uint32  NumSkinningBones = 0;
		uint32  _Pad[2] = {0, 0};
	};
	FConstantBuffer BonePaletteCB; // 매 프레임 Upadte 호출
	FBonePaletteConstants BonePaletteCPUMirror; 
	
	ID3D11Buffer* SkinningSourceBuffer = nullptr; // SkinnedSourceVertices 업로드본
	ID3D11ShaderResourceView* SkinningSourceSRV = nullptr; // 위 buffer의 SRV view
	ID3D11Buffer* SkinCacheBuffer = nullptr; // Compute가 쓴 스키닝 결과
	ID3D11UnorderedAccessView* SkinCacheUAV = nullptr; // Compute 쓰기용
	ID3D11ShaderResourceView* SkinCacheSRV = nullptr; // VS 읽기용
	uint32 SkinningVertexCount = 0; // 현재 메쉬 정점 수 캐시
	bool bSkinCacheValid = false; // true = 현재 포즈로 compute dispatch가 성공한 cache
};
