/**
 * 스켈레탈 메시 에셋 객체의 직렬화와 섹션 보정, 스켈레톤 연결 복원을 구현한다.
 *
 * 저장/로드 시 공통 에셋 헤더로 SkeletalMesh 타입과 버전을 검증하고, 메시 본문과 머티리얼 슬롯을
 * 함께 직렬화한다. 로드 이후에는 섹션의 머티리얼 인덱스를 다시 맞추고, 스켈레톤 포인터는 필요 시
 * 외부 FBXSceneAsset에서 다시 찾을 수 있도록 초기화한다.
 */
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"

#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Object/Object.h"

#include <utility>

REGISTER_FACTORY(USkeletalMesh)

namespace
{
    const FString              EmptySkeletalPath;
    const TArray<FMeshSection> EmptySkeletalSections;
} // namespace

USkeletalMesh::~USkeletalMesh()
{
    delete SkeletalMeshAsset;
    SkeletalMeshAsset = nullptr;
    Skeleton = nullptr;
}

void USkeletalMesh::Serialize(FArchive &Ar)
{
    FAssetFileHeader Header;
    if (Ar.IsSaving())
    {
        Header.AssetType = EAssetType::SkeletalMesh;
        Header.Version = AssetVersion;
    }

    Ar << Header;
    if (!Header.IsValid(EAssetType::SkeletalMesh, AssetVersion))
    {
        return;
    }

    if (Ar.IsLoading() && !SkeletalMeshAsset)
    {
        SkeletalMeshAsset = new FSkeletalMesh();
    }

    if (SkeletalMeshAsset)
    {
        SkeletalMeshAsset->Serialize(Ar);
    }
    Ar << Materials;

    TArray<FBoneInfo> Bones;
    if (Ar.IsSaving() && Skeleton)
    {
        Bones = Skeleton->GetBones();
    }
    Ar << Bones;

    if (Ar.IsLoading())
    {
        Skeleton = nullptr;
        if (!Bones.empty())
        {
            USkeleton* NewSkeleton = UObjectManager::Get().CreateObject<USkeleton>();
            if (NewSkeleton)
            {
                NewSkeleton->SetBones(std::move(Bones));
                Skeleton = NewSkeleton;
            }
        }
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
        Skeleton = nullptr;
    }

    if (SkeletalMeshAsset && !SkeletalMeshAsset->bBoundsValid)
    {
        SkeletalMeshAsset->CacheBounds();
    }
    RebuildSectionMaterialIndices();
}

void USkeletalMesh::SetSkeleton(USkeleton *InSkeleton)
{
    Skeleton = InSkeleton;

    if (SkeletalMeshAsset && Skeleton && !Skeleton->GetAssetPathFileName().empty())
    {
        SkeletalMeshAsset->SkeletonAssetPath = Skeleton->GetAssetPathFileName();
    }
}

USkeleton *USkeletalMesh::GetSkeleton()
{
    if (Skeleton && IsAliveObject(Skeleton))
    {
        return Skeleton;
    }

    Skeleton = nullptr;
    if (!SkeletalMeshAsset || SkeletalMeshAsset->SkeletonAssetPath.empty())
    {
        return nullptr;
    }

    UFBXSceneAsset *SceneAsset = GetTypedOuter<UFBXSceneAsset>();
    if (!SceneAsset)
    {
        return nullptr;
    }

    // 저장된 에셋 경로를 기준으로 같은 FBX scene 안의 스켈레톤을 다시 연결한다.
    for (USkeleton *Candidate : SceneAsset->GetSkeletons())
    {
        if (!Candidate)
        {
            continue;
        }

        if (Candidate->GetAssetPathFileName() == SkeletalMeshAsset->SkeletonAssetPath)
        {
            Skeleton = Candidate;
            break;
        }
    }

    return Skeleton;
}

const USkeleton *USkeletalMesh::GetSkeleton() const
{
    return const_cast<USkeletalMesh *>(this)->GetSkeleton();
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
        for (int32 MaterialIndex = 0; MaterialIndex < static_cast<int32>(Materials.size());
             ++MaterialIndex)
        {
            if (Materials[MaterialIndex].MaterialSlotName == Section.MaterialSlotName)
            {
                Section.MaterialIndex = MaterialIndex;
                break;
            }
        }
    }
}
