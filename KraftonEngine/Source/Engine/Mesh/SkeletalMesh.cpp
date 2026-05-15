/**
 * USkeletalMesh의 런타임 객체 동작을 구현한다.
 *
 * 스켈레탈 메시 에셋 데이터로부터 렌더링에 필요한 버퍼를 만들고, 머티리얼 슬롯과 본/애니메이션
 * 정보를 객체 레벨에서 접근할 수 있게 한다. 현재 구조에서는 CPU skinning이나 프리뷰 포즈 편집 쪽이
 * 이 데이터를 참조하므로, 에셋 데이터와 렌더 리소스의 소유 경계를 명확히 유지하는 역할을 한다.
 */

#include "Mesh/SkeletalMesh.h"

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
