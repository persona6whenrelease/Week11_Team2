/**
 * 여러 스켈레탈 메시 파트를 하나의 스켈레탈 메시 에셋으로 합치는 조립기를 선언한다.
 *
 * FBX에서는 하나의 캐릭터가 여러 mesh node로 분리되어 있을 수 있다. 이 조립기는 같은 스켈레톤을
 * 공유하는 파트를 검증하고, 정점/인덱스/섹션/머티리얼 슬롯을 하나의 FSkeletalMesh 데이터로 합친다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Types/FBXImportTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"

/**
 * 같은 스켈레톤을 공유하는 여러 스켈레탈 메시 파트를 하나의 메시로 합치는 조립기이다.
 */
class FFbxSkeletalMeshAssembler final
{
  public:
    explicit FFbxSkeletalMeshAssembler(const FFbxImportMeta &InImportMeta)
        : ImportMeta(InImportMeta)
    {
    }

    bool Assemble(const TArray<FFbxSkinnedMeshPart> &SkinnedMeshParts,
                  TArray<FSkeletalMesh>             &OutSkeletalMeshAssets,
                  TMap<int32, int32>                &OutSkeletonIdToSkeletalMeshAssetIndex) const;

  private:
    bool BuildSkeletalMeshFromParts(const FFbxSkeletonMeta                    &SkeletonMeta,
                                    const TArray<const FFbxSkinnedMeshPart *> &Parts,
                                    FSkeletalMesh                             &OutMesh) const;

    bool ValidateSkinnedMeshPartForAttach(const FFbxSkeletonMeta    &SkeletonMeta,
                                          const FFbxSkinnedMeshPart &Part) const;

  private:
    const FFbxImportMeta &ImportMeta;
};
