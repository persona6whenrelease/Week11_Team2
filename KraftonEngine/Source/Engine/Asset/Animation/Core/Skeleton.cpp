#include "Asset/Animation/Core/Skeleton.h"

#include "Core/Log.h"

IMPLEMENT_CLASS(USkeleton, UObject)

namespace
{
    const FString           EmptySkeletonPath;
    const TArray<FBoneInfo> EmptyBones;
}

void FSkeleton::Serialize(FArchive &Ar)
{
    Ar << PathFileName;
    Ar << Bones;

    if (Ar.IsLoading())
    {
        RebuildBoneNameToIndex();
    }
}

void FSkeleton::RebuildBoneNameToIndex()
{
    BoneNameToIndex.clear();
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
    {
        BoneNameToIndex[Bones[BoneIndex].Name] = BoneIndex;
    }
}

USkeleton::~USkeleton()
{
    delete SkeletonAsset;
    SkeletonAsset = nullptr;
}

void USkeleton::Serialize(FArchive &Ar)
{
    FAssetFileHeader Header;
    if (Ar.IsSaving())
    {
        Header.AssetType = EAssetType::Skeleton;
        Header.Version = AssetVersion;
    }

    Ar << Header;
    if (!Header.IsValid(EAssetType::Skeleton, AssetVersion))
    {
        UE_LOG("[USkeleton] Invalid asset header. Type=%s Version=%u",
               LexToString(Header.AssetType), Header.Version);
        return;
    }

    if (Ar.IsLoading() && !SkeletonAsset)
    {
        SkeletonAsset = new FSkeleton();
    }

    if (SkeletonAsset)
    {
        SkeletonAsset->Serialize(Ar);
    }
}

const FString &USkeleton::GetAssetPathFileName() const
{
    return SkeletonAsset ? SkeletonAsset->PathFileName : EmptySkeletonPath;
}

void USkeleton::SetSkeletonAsset(FSkeleton *InSkeleton)
{
    if (SkeletonAsset != InSkeleton)
    {
        delete SkeletonAsset;
        SkeletonAsset = InSkeleton;
    }

    if (SkeletonAsset)
    {
        SkeletonAsset->RebuildBoneNameToIndex();
    }
}

void USkeleton::SetBones(TArray<FBoneInfo> &&InBones)
{
    if (!SkeletonAsset)
    {
        SkeletonAsset = new FSkeleton();
    }

    SkeletonAsset->Bones = std::move(InBones);
    SkeletonAsset->RebuildBoneNameToIndex();
}

const TArray<FBoneInfo> &USkeleton::GetBones() const
{
    return SkeletonAsset ? SkeletonAsset->Bones : EmptyBones;
}

TArray<FBoneInfo> &USkeleton::GetMutableBones()
{
    if (!SkeletonAsset)
    {
        SkeletonAsset = new FSkeleton();
    }

    return SkeletonAsset->Bones;
}

int32 USkeleton::FindBoneIndexByName(const FString &BoneName) const
{
    if (!SkeletonAsset)
    {
        return -1;
    }

    const auto BoneIt = SkeletonAsset->BoneNameToIndex.find(BoneName);
    if (BoneIt != SkeletonAsset->BoneNameToIndex.end())
    {
        return BoneIt->second;
    }

    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SkeletonAsset->Bones.size());
         ++BoneIndex)
    {
        if (SkeletonAsset->Bones[BoneIndex].Name == BoneName)
        {
            return BoneIndex;
        }
    }

    return -1;
}
