#pragma once

#include "Asset/Animation/Core/AnimationTypes.h"
#include "Asset/AssetFileHeader.h"
#include "Object/Object.h"

struct FSkeleton
{
    FString              PathFileName;
    TArray<FBoneInfo>    Bones;
    TMap<FString, int32> BoneNameToIndex;

    void Serialize(FArchive &Ar);
    void RebuildBoneNameToIndex();

    friend FArchive &operator<<(FArchive &Ar, FSkeleton &Skeleton)
    {
        Skeleton.Serialize(Ar);
        return Ar;
    }
};

class USkeleton : public UObject
{
  public:
    DECLARE_CLASS(USkeleton, UObject)

    static constexpr uint32 AssetVersion = 2;

    USkeleton() = default;
    ~USkeleton() override;

    void Serialize(FArchive &Ar) override;

    const FString &GetAssetPathFileName() const;
    void           SetSkeletonAsset(FSkeleton *InSkeleton);
    FSkeleton     *GetSkeletonAsset() const { return SkeletonAsset; }

    void                     SetBones(TArray<FBoneInfo> &&InBones);
    const TArray<FBoneInfo> &GetBones() const;
    TArray<FBoneInfo>       &GetMutableBones();

    int32 FindBoneIndexByName(const FString &BoneName) const;

  private:
    FSkeleton *SkeletonAsset = nullptr;
};
