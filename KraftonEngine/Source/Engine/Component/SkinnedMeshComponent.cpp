#include "Component/SkinnedMeshComponent.h"

#include "Asset/Import/MeshManager.h"
#include "Engine/Runtime/Engine.h"
#include "Object/ObjectFactory.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Serialization/Archive.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include "Render/Shader/ShaderManager.h"


IMPLEMENT_CLASS(USkinnedMeshComponent, UMeshComponent)

HIDE_FROM_COMPONENT_LIST(USkinnedMeshComponent)

namespace
{
const TArray<FBoneInfo> *GetSkeletonBones(const USkeletalMesh *SkeletalMesh)
{
    const USkeleton *SkeletonAsset = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
    return SkeletonAsset ? &SkeletonAsset->GetBones() : nullptr;
}
	
float Saturate(float Value)
{
	return (std::max)(0.0f, (std::min)(1.0f, Value));
}
	
	FVector4 LerpColor(const FVector4& A, const FVector4& B, float T)
{
	T = Saturate(T);
	return FVector4(
		A.X + (B.X - A.X) * T,
		A.Y + (B.Y - A.Y) * T,
		A.Z + (B.Z - A.Z) * T,
		A.W + (B.W - A.W) * T
	);
}
} // namespace

USkinnedMeshComponent::~USkinnedMeshComponent()
{
    ReleaseGPUSkinningResources();
}

FPrimitiveSceneProxy *USkinnedMeshComponent::CreateSceneProxy()
{
    EnsureRuntimeResources();
    return new FSkeletalMeshSceneProxy(this);
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh *InMesh)
{
	SkeletalMesh = InMesh;
    if (SkeletalMesh)
    {
        SkeletalMeshPath = SkeletalMesh->GetAssetPathFileName();
        InitializeMaterialSlots(SkeletalMesh->GetMaterials());
    }
    else
    {
        SkeletalMeshPath = "None";
        ClearMaterialSlots();
    }

    SkinnedVertices.clear();
    SkinnedSourceVertices.clear();
    LocalBonePoseMatrices.clear();
    MeshSpaceBoneMatrices.clear();
	BoneOverrideMask.clear();
    RuntimeMeshBuffer.Release();
    ReleaseGPUSkinningResources();

    CacheLocalBounds();
    ResetBonePoseToBindPose();
    BuildSkinnedSourceVertices();
    EnsureRuntimeResources();
    MarkRenderStateDirty();
    MarkWorldBoundsDirty();
}

int32 USkinnedMeshComponent::FindBoneIndexByName(const FString &BoneName) const
{
    if (!SkeletalMesh)
    {
        return -1;
    }

    const USkeleton *SkeletonAsset = SkeletalMesh->GetSkeleton();
    return SkeletonAsset ? SkeletonAsset->FindBoneIndexByName(BoneName) : -1;
}

void USkinnedMeshComponent::ResetBonePoseToBindPose()
{
    LocalBonePoseMatrices.clear();
    MeshSpaceBoneMatrices.clear();
	BoneOverrideMask.clear();
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return;
    }

    const TArray<FBoneInfo> *Bones = GetSkeletonBones(SkeletalMesh);
    if (!Bones)
    {
        return;
    }

    LocalBonePoseMatrices.resize(Bones->size(), FMatrix::Identity);
	BoneOverrideMask.assign(Bones->size(), false);
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones->size()); ++BoneIndex)
    {
        LocalBonePoseMatrices[BoneIndex] = (*Bones)[BoneIndex].LocalBindPose;
    }

    RebuildMeshSpaceBoneMatrices();
    SkinVerticesToReferencePose();
    EnsureRuntimeResources();
    MarkWorldBoundsDirty();
}

bool USkinnedMeshComponent::SetBoneLocalPose(int32 BoneIndex, const FMatrix &LocalPose)
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return false;
    }

    const TArray<FBoneInfo> *Bones = GetSkeletonBones(SkeletalMesh);
    if (!Bones || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones->size()))
    {
        return false;
    }

    if (LocalBonePoseMatrices.size() != Bones->size())
    {
        ResetBonePoseToBindPose();
    }

	if (BoneOverrideMask.size() != Bones->size())
	{
		BoneOverrideMask.assign(Bones->size(), false);
	}
	
	LocalBonePoseMatrices[BoneIndex] = LocalPose;
	BoneOverrideMask[BoneIndex] = true;

    RebuildMeshSpaceBoneMatrices();
    const bool bUsedGPUSkinning = bUseGPUSkinning && PrepareGPUSkinningData();
    if (!bUsedGPUSkinning)
    {
        SkinVerticesToReferencePose();
    }
    EnsureRuntimeResources();
    MarkWorldBoundsDirty();
    return true;
}

bool USkinnedMeshComponent::SetBoneLocalPoseByName(const FString &BoneName, const FMatrix &LocalPose)
{
    return SetBoneLocalPose(FindBoneIndexByName(BoneName), LocalPose);
}

FMeshBuffer *USkinnedMeshComponent::GetMeshBuffer() const
{
    return RuntimeMeshBuffer.IsValid() ? const_cast<FMeshBuffer *>(&RuntimeMeshBuffer) : nullptr;
}

FMeshDataView USkinnedMeshComponent::GetMeshDataView() const
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return {};
    }

    const FSkeletalMesh *Asset = SkeletalMesh->GetSkeletalMeshAsset();
    FMeshDataView View;
    if (!Asset->Vertices.empty())
    {
        View.VertexData = Asset->Vertices.data();
        View.VertexCount = static_cast<uint32>(Asset->Vertices.size());
        View.Stride = sizeof(FSkeletalVertex);
    }
    if (!Asset->Indices.empty())
    {
        View.IndexData = Asset->Indices.data();
        View.IndexCount = static_cast<uint32>(Asset->Indices.size());
    }
    return View;
}

void USkinnedMeshComponent::UpdateWorldAABB() const
{
    if (!bHasValidBounds)
    {
        UPrimitiveComponent::UpdateWorldAABB();
        return;
    }

    FVector WorldCenter = CachedWorldMatrix.TransformPositionWithW(CachedLocalCenter);

    float Ex = std::abs(CachedWorldMatrix.M[0][0]) * CachedLocalExtent.X +
               std::abs(CachedWorldMatrix.M[1][0]) * CachedLocalExtent.Y +
               std::abs(CachedWorldMatrix.M[2][0]) * CachedLocalExtent.Z;
    float Ey = std::abs(CachedWorldMatrix.M[0][1]) * CachedLocalExtent.X +
               std::abs(CachedWorldMatrix.M[1][1]) * CachedLocalExtent.Y +
               std::abs(CachedWorldMatrix.M[2][1]) * CachedLocalExtent.Z;
    float Ez = std::abs(CachedWorldMatrix.M[0][2]) * CachedLocalExtent.X +
               std::abs(CachedWorldMatrix.M[1][2]) * CachedLocalExtent.Y +
               std::abs(CachedWorldMatrix.M[2][2]) * CachedLocalExtent.Z;

    WorldAABBMinLocation = WorldCenter - FVector(Ex, Ey, Ez);
    WorldAABBMaxLocation = WorldCenter + FVector(Ex, Ey, Ez);
    bWorldAABBDirty = false;
    bHasValidWorldAABB = true;
}

void USkinnedMeshComponent::Serialize(FArchive &Ar)
{
    UMeshComponent::Serialize(Ar);
    Ar << SkeletalMeshPath;
    Ar << MaterialSlots;
}

void USkinnedMeshComponent::PostDuplicate()
{
    UMeshComponent::PostDuplicate();

    if (!SkeletalMeshPath.empty() && SkeletalMeshPath != "None")
    {
        USkeletalMesh *Loaded = FMeshManager::LoadSkeletalMesh(SkeletalMeshPath);
        if (Loaded)
        {
            TArray<FMaterialSlot> SavedSlots = MaterialSlots;
            SetSkeletalMesh(Loaded);

            for (int32 i = 0; i < static_cast<int32>(MaterialSlots.size()) && i < static_cast<int32>(SavedSlots.size());
                 ++i)
            {
                MaterialSlots[i] = SavedSlots[i];
            }
        }
    }

    RestoreOverrideMaterialsFromSlots();
    CacheLocalBounds();
    ResetBonePoseToBindPose();
    BuildBindPoseRenderVertices();
    BuildSkinnedSourceVertices();   // GPU 경로용 정점(본 정보 포함) 빌드
    EnsureRuntimeResources();
    MarkRenderStateDirty();
    MarkWorldBoundsDirty();
}

void USkinnedMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor> &OutProps)
{
    UPrimitiveComponent::GetEditableProperties(OutProps);
    OutProps.push_back({"Skeletal Mesh", EPropertyType::SkeletalMeshRef, &SkeletalMeshPath});
    OutProps.push_back({"Use GPU Skinning", EPropertyType::Bool, &bUseGPUSkinning});
    AppendMaterialSlotProperties(OutProps);
}

void USkinnedMeshComponent::PostEditProperty(const char *PropertyName)
{
    UMeshComponent::PostEditProperty(PropertyName);

    if (std::strcmp(PropertyName, "Skeletal Mesh") == 0)
    {
        if (SkeletalMeshPath.empty() || SkeletalMeshPath == "None")
        {
            SetSkeletalMesh(nullptr);
        }
        else
        {
            USkeletalMesh *Loaded = FMeshManager::LoadSkeletalMesh(SkeletalMeshPath);
            SetSkeletalMesh(Loaded);
        }
        CacheLocalBounds();
        MarkWorldBoundsDirty();
    }
}

void USkinnedMeshComponent::CacheLocalBounds()
{
    bHasValidBounds = false;
    if (!SkeletalMesh)
        return;

    FSkeletalMesh *Asset = SkeletalMesh->GetSkeletalMeshAsset();
    if (!Asset || Asset->Vertices.empty())
        return;

    if (!Asset->bBoundsValid)
    {
        Asset->CacheBounds();
    }

    CachedLocalCenter = Asset->BoundsCenter;
    CachedLocalExtent = Asset->BoundsExtent;
    bHasValidBounds = Asset->bBoundsValid;
}

void USkinnedMeshComponent::EnsureRuntimeResources()
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset() || SkinnedVertices.empty())
    {
        return;
    }

    ID3D11Device *Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
    ID3D11DeviceContext *Context = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext() : nullptr;
    if (!Device || !Context)
    {
        return;
    }

    const FSkeletalMesh *Asset = SkeletalMesh->GetSkeletalMeshAsset();
    if (!RuntimeMeshBuffer.IsValid())
    {
        RuntimeMeshBuffer.CreateDynamic<FVertexPNCTT>(Device, static_cast<uint32>(SkinnedVertices.size()),
                                                      Asset->Indices);
    }

    UploadSkinnedVertices();
}

void USkinnedMeshComponent::BuildBindPoseRenderVertices()
{
    SkinnedVertices.clear();
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return;
    }

    const FSkeletalMesh *Asset = SkeletalMesh->GetSkeletalMeshAsset();
    SkinnedVertices.reserve(Asset->Vertices.size());
    for (const FSkeletalVertex &RawVert : Asset->Vertices)
    {
        FVertexPNCTT RenderVert;
        RenderVert.Position = RawVert.pos;
        RenderVert.Normal = RawVert.normal;
        RenderVert.Color = ResolveVertexDebugColor(RawVert);
        RenderVert.UV = RawVert.tex;
        RenderVert.Tangent = RawVert.tangent;
        SkinnedVertices.push_back(RenderVert);
    }
}

void USkinnedMeshComponent::BuildSkinnedSourceVertices()
{
	SkinnedSourceVertices.clear();
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	const FSkeletalMesh *Asset = SkeletalMesh->GetSkeletalMeshAsset();
	SkinnedSourceVertices.reserve(Asset->Vertices.size());
	for (const FSkeletalVertex &RawVert : Asset->Vertices)
	{
		FVertexPNCTT_Skinned RenderVert;
		RenderVert.Position = RawVert.pos;
		RenderVert.Normal = RawVert.normal;
		RenderVert.Color = ResolveVertexDebugColor(RawVert);
		RenderVert.UV = RawVert.tex;
		RenderVert.Tangent = RawVert.tangent;

		for (int i=0; i < 4; ++i)
		{
			RenderVert.BoneIDs[i] = RawVert.BoneIDs[i];
			RenderVert.BoneWeights[i] = RawVert.BoneWeights[i];
		}

		SkinnedSourceVertices.push_back(RenderVert);
	}
}

void USkinnedMeshComponent::UploadSkinnedVertices()
{
    ID3D11DeviceContext *Context = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext() : nullptr;
    if (Context)
    {
        RuntimeMeshBuffer.UpdateVertices(Context, SkinnedVertices);
    }
}

void USkinnedMeshComponent::RebuildMeshSpaceBoneMatrices()
{
    MeshSpaceBoneMatrices.clear();
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return;
    }

    const TArray<FBoneInfo> *Bones = GetSkeletonBones(SkeletalMesh);
    if (!Bones)
    {
        return;
    }

    if (LocalBonePoseMatrices.size() != Bones->size())
    {
        LocalBonePoseMatrices.resize(Bones->size(), FMatrix::Identity);
        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones->size()); ++BoneIndex)
        {
            LocalBonePoseMatrices[BoneIndex] = (*Bones)[BoneIndex].LocalBindPose;
        }
    }

    MeshSpaceBoneMatrices.resize(Bones->size(), FMatrix::Identity);
    for (int32 i = 0; i < static_cast<int32>(Bones->size()); ++i)
    {
        const int32 ParentIndex = (*Bones)[i].ParentIndex;
        MeshSpaceBoneMatrices[i] = (ParentIndex >= 0 && ParentIndex < i)
                                       ? LocalBonePoseMatrices[i] * MeshSpaceBoneMatrices[ParentIndex]
                                       : LocalBonePoseMatrices[i];
    }
}

void USkinnedMeshComponent::SkinVerticesToReferencePose()
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return;
    }

    const FSkeletalMesh *Asset = SkeletalMesh->GetSkeletalMeshAsset();
    const TArray<FBoneInfo> *Bones = GetSkeletonBones(SkeletalMesh);
    if (!Bones)
    {
        return;
    }

    if (SkinnedVertices.size() != Asset->Vertices.size())
    {
        BuildBindPoseRenderVertices();
    }
    if (MeshSpaceBoneMatrices.size() != Bones->size())
    {
        RebuildMeshSpaceBoneMatrices();
    }

    for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(Asset->Vertices.size()); ++VertexIndex)
    {
        const FSkeletalVertex &Source = Asset->Vertices[VertexIndex];
        FVector SkinnedPos(0.0f, 0.0f, 0.0f);
        FVector SkinnedNormal(0.0f, 0.0f, 0.0f);
        float TotalWeight = 0.0f;

        for (int32 Influence = 0; Influence < 4; ++Influence)
        {
            const float Weight = Source.BoneWeights[Influence];
            const uint32 BoneIndex = Source.BoneIDs[Influence];
            if (Weight <= 0.0f || BoneIndex >= MeshSpaceBoneMatrices.size())
            {
                continue;
            }

            const FMatrix SkinMatrix = (*Bones)[BoneIndex].InverseBindPose * MeshSpaceBoneMatrices[BoneIndex];
            SkinnedPos += SkinMatrix.TransformPositionWithW(Source.pos) * Weight;
            SkinnedNormal += SkinMatrix.TransformVector(Source.normal) * Weight;
            TotalWeight += Weight;
        }

        FVertexPNCTT &Dest = SkinnedVertices[VertexIndex];
        if (TotalWeight > 0.0f)
        {
            Dest.Position = SkinnedPos;
            Dest.Normal = SkinnedNormal;
            Dest.Normal.Normalize();
        }
        else
        {
            Dest.Position = Source.pos;
            Dest.Normal = Source.normal;
        }
        Dest.Color = ResolveVertexDebugColor(Source);
        Dest.UV = Source.tex;
        Dest.Tangent = Source.tangent;
    }
}

bool USkinnedMeshComponent::PrepareGPUSkinningData()
{
    bSkinCacheValid = false;
    if (SkinnedSourceVertices.empty())
    {
        BuildSkinnedSourceVertices();
    }

	EnsureGPUSkinningBuffers();   // 메쉬 변경시에만 실제 작업 (idempotent)
	if (!UpdateBonePaletteCB())    // 매 프레임 본 행렬 업로드
	{
		return false;
	}
	return DispatchSkinningCompute();     // Compute 실행 → SkinCache에 결과 저장
}

void USkinnedMeshComponent::ApplyEvaluatedPose(const TArray<FMatrix>& EvaluatedLocalPose)
{
	if (EvaluatedLocalPose.empty())
	{
		return;
	}
	
	if (LocalBonePoseMatrices.size() != EvaluatedLocalPose.size())
	{
		LocalBonePoseMatrices = EvaluatedLocalPose;
		BoneOverrideMask.assign(EvaluatedLocalPose.size(), false);
	}
	else
	{
		const size_t MaskSize = BoneOverrideMask.size();
		for (size_t i = 0; i < EvaluatedLocalPose.size(); ++i)
		{
			const bool bOverridden = (i < MaskSize) ? BoneOverrideMask[i] : false;
			// User-edited bones keep their current local pose.
			if (!bOverridden)
			{
				LocalBonePoseMatrices[i] = EvaluatedLocalPose[i];
			}
		}
	}
	
	RebuildMeshSpaceBoneMatrices();
	// 컴포넌트 플래그에 따라 Skinning 방식 선택. GPU가 default
	const bool bUsedGPUSkinning = bUseGPUSkinning && PrepareGPUSkinningData();
	if (!bUsedGPUSkinning)
	{
		SkinVerticesToReferencePose(); // 원래대로 매 프레임 CPU Skinning
	}
	EnsureRuntimeResources();
	MarkWorldBoundsDirty();
}

bool USkinnedMeshComponent::IsBoneOverridden(int32 BoneIndex) const
{
	if (BoneIndex < 0 || static_cast<size_t>(BoneIndex) >= BoneOverrideMask.size())
	{
		return false;
	}
	return BoneOverrideMask[BoneIndex];
}

void USkinnedMeshComponent::ClearBoneOverride(int32 BoneIndex)
{
	if (BoneIndex < 0 || static_cast<size_t>(BoneIndex) >= BoneOverrideMask.size())
	{
		return;
	}
	BoneOverrideMask[BoneIndex] = false;
	// 마스크만 끄면 다음 Tick에서 시퀀스 결과가 이 본을 덮으므로
	// 즉시 시각적 효과는 다음 프레임에 반영된다 — 별도 처리 불필요.
}

void USkinnedMeshComponent::ClearAllBoneOverrides()
{
	std::fill(BoneOverrideMask.begin(), BoneOverrideMask.end(), false);
}

void USkinnedMeshComponent::MarkBoneOverridden(int32 BoneIndex)
{
	if (BoneIndex < 0) return;

	const TArray<FBoneInfo> *Bones = GetSkeletonBones(SkeletalMesh);
	if (!Bones || static_cast<size_t>(BoneIndex) >= Bones->size()) return;

	if (BoneOverrideMask.size() != Bones->size())
	{
		BoneOverrideMask.assign(Bones->size(), false);
	}
	BoneOverrideMask[BoneIndex] = true;
}

// === Bone Weight Heatmap ===
void USkinnedMeshComponent::SetBoneWeightHeatmapState(bool bEnabled, int32 BoneIndex)
{
	const int32 NewBoneIndex = bEnabled ? BoneIndex : -1;
	const bool bNewEnabled = bEnabled;
	
	if (bBoneWeightHeatmapEnabled == bNewEnabled && BoneWeightHeatmapBoneIndex == NewBoneIndex)
	{
		return;	
	}
	
	bBoneWeightHeatmapEnabled = bNewEnabled;
	BoneWeightHeatmapBoneIndex = NewBoneIndex;
	
	ApplyVertexDebugColors();
	
	if (RuntimeMeshBuffer.IsValid())
	{
		UploadSkinnedVertices();
	}
}

float USkinnedMeshComponent::GetBoneWeightForVertex(const FSkeletalVertex& SourceVertex, int32 BoneIndex)
{
	if (BoneIndex < 0)
	{
		return 0.0f;
	}

	const uint32 TargetBoneIndex = static_cast<uint32>(BoneIndex);
	for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
	{
		if (SourceVertex.BoneIDs[InfluenceIndex] == TargetBoneIndex)
		{
			return SourceVertex.BoneWeights[InfluenceIndex];
		}
	}

	return 0.0f;
}

FVector4 USkinnedMeshComponent::MakeBoneWeightHeatmapColor(float Weight)
{
	const float W = Saturate(Weight);
	
	const FVector4 ZeroColor(0.95f, 0.20f, 1.00f, 1.0f);
	const FVector4 LowColor (0.05f, 0.20f, 1.00f, 1.0f);
	const FVector4 MidColor (0.00f, 0.90f, 1.00f, 1.0f);
	const FVector4 HighColor(1.00f, 1.00f, 0.05f, 1.0f);
	const FVector4 MaxColor (1.00f, 0.05f, 0.00f, 1.0f);

	if (W < 0.25f) return LerpColor(ZeroColor, LowColor, W / 0.25f);
	if (W < 0.50f) return LerpColor(LowColor, MidColor, (W - 0.25f) / 0.25f);
	if (W < 0.75f) return LerpColor(MidColor, HighColor, (W - 0.50f) / 0.25f);
	return LerpColor(HighColor, MaxColor, (W - 0.75f) / 0.25f);
}

FVector4 USkinnedMeshComponent::ResolveVertexDebugColor(const FSkeletalVertex& SourceVertex) const
{
	if (!bBoneWeightHeatmapEnabled)
	{
		return FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	return MakeBoneWeightHeatmapColor(GetBoneWeightForVertex(SourceVertex, BoneWeightHeatmapBoneIndex));
}

void USkinnedMeshComponent::ApplyVertexDebugColors()
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (SkinnedVertices.size() != Asset->Vertices.size())
	{
		return;
	}

	for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(Asset->Vertices.size()); ++VertexIndex)
	{
		SkinnedVertices[VertexIndex].Color = ResolveVertexDebugColor(Asset->Vertices[VertexIndex]);
	}
}

// === GPU Skinning - Compute pipeline ===
void USkinnedMeshComponent::EnsureGPUSkinningBuffers()
{
    if (SkinnedSourceVertices.empty()) return;

    // 이미 같은 크기로 생성돼 있다면 재사용
    if (SkinningSourceBuffer && SkinningVertexCount == SkinnedSourceVertices.size())
    {
        return;
    }

    ReleaseGPUSkinningResources();  // 정점 수가 바뀐 경우 등 안전하게 재생성
    bSkinCacheValid = false;

    ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
    if (!Device) return;

    SkinningVertexCount = static_cast<uint32>(SkinnedSourceVertices.size());

    // ---- 1) Source StructuredBuffer (Compute가 읽음, 정적) ----
    {
        D3D11_BUFFER_DESC Desc = {};
        Desc.ByteWidth = sizeof(FVertexPNCTT_Skinned) * SkinningVertexCount;
        Desc.Usage = D3D11_USAGE_IMMUTABLE;           // 한 번 만들고 안 바꿈
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        Desc.StructureByteStride = sizeof(FVertexPNCTT_Skinned);

        D3D11_SUBRESOURCE_DATA Init = {};
        Init.pSysMem = SkinnedSourceVertices.data();

        Device->CreateBuffer(&Desc, &Init, &SkinningSourceBuffer);
        if (!SkinningSourceBuffer)
        {
            ReleaseGPUSkinningResources();
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        SRVDesc.Buffer.NumElements = SkinningVertexCount;

        Device->CreateShaderResourceView(SkinningSourceBuffer, &SRVDesc, &SkinningSourceSRV);
        if (!SkinningSourceSRV)
        {
            ReleaseGPUSkinningResources();
            return;
        }
    }

    // ---- 2) Skin Cache StructuredBuffer (Compute 쓰기 + VS 읽기) ----
    {
        // 출력은 FVertexPNCTT 크기 (Position/Normal/Color/UV/Tangent)
        D3D11_BUFFER_DESC Desc = {};
        Desc.ByteWidth = sizeof(FVertexPNCTT) * SkinningVertexCount;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        Desc.StructureByteStride = sizeof(FVertexPNCTT);

        Device->CreateBuffer(&Desc, nullptr, &SkinCacheBuffer);
        if (!SkinCacheBuffer)
        {
            ReleaseGPUSkinningResources();
            return;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
        UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        UAVDesc.Buffer.NumElements = SkinningVertexCount;

        Device->CreateUnorderedAccessView(SkinCacheBuffer, &UAVDesc, &SkinCacheUAV);
        if (!SkinCacheUAV)
        {
            ReleaseGPUSkinningResources();
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        SRVDesc.Buffer.NumElements = SkinningVertexCount;

        Device->CreateShaderResourceView(SkinCacheBuffer, &SRVDesc, &SkinCacheSRV);
        if (!SkinCacheSRV)
        {
            ReleaseGPUSkinningResources();
            return;
        }
    }

    // ---- 3) Bone Palette CB (없다면 생성) ----
    if (!BonePaletteCB.GetBuffer())
    {
        BonePaletteCB.Create(Device, sizeof(FBonePaletteConstants));
    }
}

void USkinnedMeshComponent::ReleaseGPUSkinningResources()
{
    bSkinCacheValid = false;
    if (SkinningSourceSRV)    { SkinningSourceSRV->Release();    SkinningSourceSRV = nullptr; }
    if (SkinningSourceBuffer) { SkinningSourceBuffer->Release(); SkinningSourceBuffer = nullptr; }
    if (SkinCacheSRV)         { SkinCacheSRV->Release();         SkinCacheSRV = nullptr; }
    if (SkinCacheUAV)         { SkinCacheUAV->Release();         SkinCacheUAV = nullptr; }
    if (SkinCacheBuffer)      { SkinCacheBuffer->Release();      SkinCacheBuffer = nullptr; }
    SkinningVertexCount = 0;
    // BonePaletteCB는 정점 수와 무관하므로 유지 (소멸자에서 자동 해제)
}

bool USkinnedMeshComponent::UpdateBonePaletteCB()
{
    const TArray<FBoneInfo>* Bones = GetSkeletonBones(SkeletalMesh);
    if (!Bones || Bones->empty()) return false;
    if (MeshSpaceBoneMatrices.size() != Bones->size()) return false;
    if (!BonePaletteCB.GetBuffer()) return false;

    const uint32 BoneCount = std::min<uint32>(static_cast<uint32>(Bones->size()), MAX_SKINNING_BONES);

    // CPU 경로와 동일한 식: SkinMatrix = InverseBindPose * MeshSpaceBoneMatrix
    // 정점마다 4번 곱하던 걸 본마다 1번으로 줄여서 GPU에 보냄.
    for (uint32 i = 0; i < BoneCount; ++i)
    {
        BonePaletteCPUMirror.BoneMatrices[i] = (*Bones)[i].InverseBindPose * MeshSpaceBoneMatrices[i];
    }
    BonePaletteCPUMirror.NumSkinningVertices = SkinningVertexCount;
    BonePaletteCPUMirror.NumSkinningBones    = BoneCount;

    ID3D11DeviceContext* Context = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext() : nullptr;
    if (!Context)
    {
        return false;
    }

    BonePaletteCB.Update(Context, &BonePaletteCPUMirror, sizeof(FBonePaletteConstants));
    return true;
}

bool USkinnedMeshComponent::DispatchSkinningCompute()
{
    bSkinCacheValid = false;
    if (!SkinningSourceSRV || !SkinCacheUAV) return false;

    // Compute Shader 가져오기 (캐싱: FShaderManager가 소유)
    static FComputeShader* SkinCS = nullptr;
    if (!SkinCS)
    {
        SkinCS = FShaderManager::Get().GetOrCreateCS("Shaders/Geometry/SkinCompute.hlsl", "CSMain");
    }
    if (!SkinCS || !SkinCS->IsValid()) return false;

    ID3D11DeviceContext* Context = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext() : nullptr;
    if (!Context) return false;

    // ---- Bind ----
    SkinCS->Bind(Context);

    ID3D11Buffer* CBs[] = { BonePaletteCB.GetBuffer() };
    Context->CSSetConstantBuffers(4, 1, CBs);                 // b4

    ID3D11ShaderResourceView* SRVs[] = { SkinningSourceSRV };
    Context->CSSetShaderResources(0, 1, SRVs);                 // t0

    ID3D11UnorderedAccessView* UAVs[] = { SkinCacheUAV };
    Context->CSSetUnorderedAccessViews(0, 1, UAVs, nullptr);   // u0

    // ---- Dispatch ----
    // numthreads(64,1,1)이므로 그룹 수 = ceil(N / 64)
    const uint32 GroupCount = (SkinningVertexCount + 63) / 64;
    Context->Dispatch(GroupCount, 1, 1);

    // ---- Unbind (UAV 풀어줘야 다음 단계가 SRV로 읽기 가능) ----
    ID3D11UnorderedAccessView* NullUAV[] = { nullptr };
    Context->CSSetUnorderedAccessViews(0, 1, NullUAV, nullptr);
    ID3D11ShaderResourceView* NullSRV[] = { nullptr };
    Context->CSSetShaderResources(0, 1, NullSRV);
    bSkinCacheValid = true;
    return true;
}
