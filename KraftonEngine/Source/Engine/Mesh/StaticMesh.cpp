/**
 * UStaticMesh의 렌더 리소스 생성과 LOD 접근 동작을 구현한다.
 *
 * StaticMeshAsset의 정점/인덱스/섹션 데이터를 실제 D3D 버퍼로 올리고, 머티리얼 슬롯과 LOD 데이터를
 * 렌더링 단계에서 사용할 수 있게 정리한다. 원본 임포트 데이터와 GPU 리소스 사이의 변환을 담당하는
 * 런타임 객체 구현부이다.
 */

#include "Mesh/StaticMesh.h"
#include "Object/ObjectFactory.h"
#include "Serialization/WindowsArchive.h"
#include "Mesh/ObjImporter.h"
#include "Texture/Texture2D.h"
#include "Engine/Profiling/MemoryStats.h"
#include "Mesh/MeshSimplifier.h"

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
