/**
 * UStaticMesh???뚮뜑 由ъ냼???앹꽦怨?LOD ?묎렐 ?숈옉??援ы쁽?쒕떎.
 *
 * StaticMeshAsset???뺤젏/?몃뜳???뱀뀡 ?곗씠?곕? ?ㅼ젣 D3D 踰꾪띁濡??щ━怨? 癒명떚由ъ뼹 ?щ’怨?LOD ?곗씠?곕?
 * ?뚮뜑留??④퀎?먯꽌 ?ъ슜?????덇쾶 ?뺣━?쒕떎. ?먮낯 ?꾪룷???곗씠?곗? GPU 由ъ냼???ъ씠??蹂?섏쓣 ?대떦?섎뒗
 * ?고???媛앹껜 援ы쁽遺?대떎.
 */

#include "Asset/Mesh/StaticMesh/StaticMesh.h"
#include "Object/ObjectFactory.h"
#include "Serialization/WindowsArchive.h"
#include "Asset/Import/OBJ/ObjImporter.h"
#include "Asset/Texture/Texture2D.h"
#include "Engine/Profiling/MemoryStats.h"
#include "Asset/Mesh/Processing/MeshSimplifier.h"

IMPLEMENT_CLASS(UStaticMesh, UObject)

static const FString EmptyPath;

UStaticMesh::~UStaticMesh()
{
    if (StaticMeshAsset)
    {
        const uint32 CPUSize =
            static_cast<uint32>(StaticMeshAsset->Vertices.size() * sizeof(FNormalVertex)) +
            static_cast<uint32>(StaticMeshAsset->Indices.size() * sizeof(uint32));

        MemoryStats::SubStaticMeshCPUMemory(CPUSize);
    }
}

void UStaticMesh::Serialize(FArchive &Ar)
{

    if (Ar.IsLoading() && !StaticMeshAsset)
    {
        StaticMeshAsset = new FStaticMesh();
    }

    StaticMeshAsset->Serialize(Ar);

    Ar << StaticMaterials;

    if (Ar.IsLoading())
    {
        for (FStaticMeshSection &Section : StaticMeshAsset->Sections)
        {
            Section.MaterialIndex = -1;
            for (int32 i = 0; i < (int32)StaticMaterials.size(); ++i)
            {
                if (StaticMaterials[i].MaterialSlotName == Section.MaterialSlotName)
                {
                    Section.MaterialIndex = i;
                    break;
                }
            }
        }
    }
}

void UStaticMesh::InitResources(ID3D11Device *InDevice)
{
    if (!InDevice || !StaticMeshAsset)
        return;

    const uint32 CPUSize =
        static_cast<uint32>(StaticMeshAsset->Vertices.size() * sizeof(FNormalVertex)) +
        static_cast<uint32>(StaticMeshAsset->Indices.size() * sizeof(uint32));
    MemoryStats::AddStaticMeshCPUMemory(CPUSize);

    TMeshData<FVertexPNCTT> RenderMeshData;
    RenderMeshData.Vertices.reserve(StaticMeshAsset->Vertices.size());

    for (const FNormalVertex &RawVert : StaticMeshAsset->Vertices)
    {
        FVertexPNCTT RenderVert;
        RenderVert.Position = RawVert.pos;
        RenderVert.Normal = RawVert.normal;
        RenderVert.Color = RawVert.color;
        RenderVert.UV = RawVert.tex;
        RenderVert.Tangent = RawVert.tangent;
        RenderMeshData.Vertices.push_back(RenderVert);
    }
    RenderMeshData.Indices = StaticMeshAsset->Indices;

    StaticMeshAsset->RenderBuffer = std::make_unique<FMeshBuffer>();
    StaticMeshAsset->RenderBuffer->Create(InDevice, RenderMeshData);
}

const FString &UStaticMesh::GetAssetPathFileName() const
{
    if (StaticMeshAsset)
    {
        return StaticMeshAsset->PathFileName;
    }
    return EmptyPath;
}

void UStaticMesh::SetStaticMeshAsset(FStaticMesh *InMesh)
{
    StaticMeshAsset = InMesh;

    if (StaticMeshAsset)
    {
        for (FStaticMeshSection &Section : StaticMeshAsset->Sections)
        {
            Section.MaterialIndex = -1;
            for (int32 i = 0; i < (int32)StaticMaterials.size(); ++i)
            {
                if (StaticMaterials[i].MaterialSlotName == Section.MaterialSlotName)
                {
                    Section.MaterialIndex = i;
                    break;
                }
            }
        }
        EnsureMeshTrianglePickingBVHBuilt();
    }
}

FStaticMesh *UStaticMesh::GetStaticMeshAsset() const { return StaticMeshAsset; }

void UStaticMesh::SetStaticMaterials(TArray<FStaticMaterial> &&InMaterials)
{
    StaticMaterials = InMaterials;
}

const TArray<FStaticMaterial> &UStaticMesh::GetStaticMaterials() const { return StaticMaterials; }

void UStaticMesh::EnsureMeshTrianglePickingBVHBuilt() const
{
    if (!StaticMeshAsset)
    {
        return;
    }

    MeshTrianglePickingBVH.EnsureBuilt(*StaticMeshAsset);
}

bool UStaticMesh::RaycastMeshTrianglesWithBVHLocal(const FVector &LocalOrigin,
                                                   const FVector &LocalDirection,
                                                   FHitResult    &OutHitResult) const
{
    if (!StaticMeshAsset)
    {
        return false;
    }

    EnsureMeshTrianglePickingBVHBuilt();
    return MeshTrianglePickingBVH.RaycastLocal(LocalOrigin, LocalDirection, *StaticMeshAsset,
                                               OutHitResult);
}

FMeshBuffer *UStaticMesh::GetLODMeshBuffer(uint32 LODLevel) const
{
    if (LODLevel == 0 && StaticMeshAsset)
        return StaticMeshAsset->RenderBuffer.get();
    if (LODLevel >= 1 && LODLevel <= 3 && bHasLOD)
        return AdditionalLODs[LODLevel - 1].RenderBuffer.get();
    return StaticMeshAsset ? StaticMeshAsset->RenderBuffer.get() : nullptr;
}

static const TArray<FStaticMeshSection> EmptySections;

const TArray<FStaticMeshSection> &UStaticMesh::GetLODSections(uint32 LODLevel) const
{
    if (LODLevel == 0 && StaticMeshAsset)
        return StaticMeshAsset->Sections;
    if (LODLevel >= 1 && LODLevel <= 3 && bHasLOD)
        return AdditionalLODs[LODLevel - 1].Sections;
    return StaticMeshAsset ? StaticMeshAsset->Sections : EmptySections;
}
