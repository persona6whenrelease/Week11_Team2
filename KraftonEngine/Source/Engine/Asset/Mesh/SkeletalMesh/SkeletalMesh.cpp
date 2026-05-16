/**
 * 스켈레탈 메시 에셋 객체의 직렬화와 섹션 보정 로직을 구현한다.
 *
 * 저장/로드 시 공통 에셋 헤더로 SkeletalMesh 타입과 버전을 검증하고, 메시 본문과 머티리얼 슬롯을 함께
 * 직렬화한다. 머티리얼 배열이 변경된 뒤에는 섹션의 머티리얼 인덱스를 다시 맞춰 렌더링 참조가 어긋나지
 * 않도록 한다.
 */

#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"

#include "Object/ObjectFactory.h"

#include <utility>

IMPLEMENT_CLASS(USkeletalMesh, UObject)

static const FString              EmptySkeletalPath;
static const TArray<FMeshSection> EmptySkeletalSections;

USkeletalMesh::~USkeletalMesh()
{
    delete SkeletalMeshAsset;
    SkeletalMeshAsset = nullptr;
}

void USkeletalMesh::Serialize(FArchive &Ar)
{
    if (Ar.IsLoading() && !SkeletalMeshAsset)
    {
        SkeletalMeshAsset = new FSkeletalMesh();
    }

    if (SkeletalMeshAsset)
    {
        SkeletalMeshAsset->Serialize(Ar);
    }
    Ar << Materials;

    if (Ar.IsLoading())
    {
        RebuildSectionMaterialIndices();
    }
}

const FString &USkeletalMesh::GetAssetPathFileName() const
{
    return SkeletalMeshAsset ? SkeletalMeshAsset->PathFileName : EmptySkeletalPath;
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh *InMesh)
{
    if (SkeletalMeshAsset != InMesh)
    {
        delete SkeletalMeshAsset;
        SkeletalMeshAsset = InMesh;
    }

    if (SkeletalMeshAsset && !SkeletalMeshAsset->bBoundsValid)
    {
        SkeletalMeshAsset->CacheBounds();
    }
    RebuildSectionMaterialIndices();
}

void USkeletalMesh::SetMaterials(TArray<FMeshMaterial> &&InMaterials)
{
    Materials = std::move(InMaterials);
    RebuildSectionMaterialIndices();
}

const TArray<FMeshSection> &USkeletalMesh::GetSections() const
{
    return SkeletalMeshAsset ? SkeletalMeshAsset->Sections : EmptySkeletalSections;
}

void USkeletalMesh::RebuildSectionMaterialIndices()
{
    if (!SkeletalMeshAsset)
    {
        return;
    }

    for (FMeshSection &Section : SkeletalMeshAsset->Sections)
    {
        Section.MaterialIndex = -1;
        for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
        {
            if (Materials[i].MaterialSlotName == Section.MaterialSlotName)
            {
                Section.MaterialIndex = i;
                break;
            }
        }
    }
}
