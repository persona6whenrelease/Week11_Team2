#include "Component/SkinnedMeshComponent.h"

#include "Asset/Import/MeshManager.h"
#include "Engine/Runtime/Engine.h"
#include "Object/ObjectFactory.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Serialization/Archive.h"
#include <algorithm>
#include <cmath>
#include <cstring>


REGISTER_FACTORY(USkinnedMeshComponent)
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
    LocalBonePoseMatrices.clear();
    MeshSpaceBoneMatrices.clear();
	BoneOverrideMask.clear();
    RuntimeMeshBuffer.Release();

    CacheLocalBounds();
    ResetBonePoseToBindPose();
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
    SkinVerticesToReferencePose();
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
    EnsureRuntimeResources();
    MarkRenderStateDirty();
    MarkWorldBoundsDirty();
}

void USkinnedMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor> &OutProps)
{
    UPrimitiveComponent::GetEditableProperties(OutProps);
    static const FPropertyTypeDesc SkeletalMeshObjectRefType{
        EPropertyType::ObjectRef,
        nullptr,
        nullptr,
        0,
        &USkeletalMesh::StaticClassInstance
    };
    FPropertyDescriptor SkeletalMeshProp;
    SkeletalMeshProp.ValuePtr = &SkeletalMeshPath;
    SkeletalMeshProp.SyntheticTypeDesc = &SkeletalMeshObjectRefType;
    SkeletalMeshProp.DynamicName = "Skeletal Mesh";
    OutProps.push_back(std::move(SkeletalMeshProp));
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
	SkinVerticesToReferencePose();
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
