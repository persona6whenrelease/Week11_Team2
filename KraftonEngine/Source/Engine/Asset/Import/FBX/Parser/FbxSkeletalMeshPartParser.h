/**
 * FBX 스켈레탈 메시 파트를 파싱하는 타입을 선언한다.
 *
 * 스킨 클러스터가 있는 메시와 본에 rigid하게 붙은 메시를 모두 스켈레탈 메시 파트로 변환한다. 최종 병합은
 * Assembler가 담당하므로, 이 파서는 파트 단위의 정점, 가중치, 바인드 포즈 기준을 정확히 만드는 데 집중한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Types/FBXImportTypes.h"

/**
 * FBX의 skinned mesh와 rigid attached mesh를 스켈레탈 메시 파트로 변환하는 파서이다.
 */
class FFbxSkeletalMeshPartParser final
{
  public:
    explicit FFbxSkeletalMeshPartParser(const FFbxImportMeta &InImportMeta)
        : ImportMeta(InImportMeta)
    {
    }

    

    /**
     * 메타 정보와 FBX 노드를 입력으로 받아 해당 파서가 담당하는 메시 데이터를 생성한다.
     */
    bool Parse(TArray<FFbxSkinnedMeshPart> &OutSkinnedMeshParts) const;

  private:
    /**
     * 원본 포맷 데이터를 엔진 임포트 중간 구조로 분석한다.
     */
    bool ParseSkinnedMeshPart(int32 MeshId, FFbxSkinnedMeshPart &OutPart) const;
    /**
     * 원본 포맷 데이터를 엔진 임포트 중간 구조로 분석한다.
     */
    bool ParseRigidAttachedMeshPart(int32 MeshId, FFbxSkinnedMeshPart &OutPart) const;

  private:
    const FFbxImportMeta &ImportMeta;
};
