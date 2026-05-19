/**
 * 정적 메시 UObject의 직렬화, 리소스 생성, 피킹 보조 로직을 구현한다.
 *
 * 공통 에셋 헤더로 StaticMesh 파일을 검증한 뒤 메시 데이터와 머티리얼 슬롯을 저장/로드한다. 로드된 메시에는
 * GPU 버퍼와 LOD 버퍼를 생성하고, 에디터 선택을 위해 triangle BVH 기반 로컬 공간 raycast도 제공한다.
 */

#include "Asset/Mesh/StaticMesh/StaticMesh.h"
#include "Object/ObjectFactory.h"
#include "Serialization/WindowsArchive.h"
#include "Asset/Import/OBJ/ObjImporter.h"
#include "Asset/Texture/Texture2D.h"
#include "Engine/Profiling/MemoryStats.h"
#include "Asset/Mesh/Processing/MeshSimplifier.h"

REGISTER_FACTORY(UStaticMesh)

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
    FAssetFileHeader Header;
    if (Ar.IsSaving())
    {
        Header.AssetType = EAssetType::StaticMesh;
        Header.Version = AssetVersion;
    }

    Ar << Header;
    if (!Header.IsValid(EAssetType::StaticMesh, AssetVersion))
    {
        return;
    }

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
