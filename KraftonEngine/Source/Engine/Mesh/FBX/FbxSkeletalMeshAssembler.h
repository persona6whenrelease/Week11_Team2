/**
 * 스키닝 메시 파트를 스켈레탈 메시 에셋으로 조립하는 클래스를 선언한다.
 *
 * 파서는 FBX 원본 노드별로 부분 데이터를 만들고, 이 조립기는 스켈레톤 단위로 그 결과를 묶어 엔진의
 * FSkeletalMesh 배열을 만든다. 메타 정보의 skeleton id와 결과 에셋 인덱스 사이의 매핑도 이 단계에서
 * 확정된다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "FBXImportTypes.h"
#include "Mesh/SkeletalMeshAsset.h"

/**
 * FBX 스키닝 파트들을 스켈레톤 단위의 FSkeletalMesh로 병합하는 조립기이다.
 *
 * 파싱 단계에서 분리된 부분 메시들을 같은 skeleton id 기준으로 묶고, 정점/인덱스/섹션 범위와 본
 * 매핑을 최종 에셋 기준으로 다시 정리한다.
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
