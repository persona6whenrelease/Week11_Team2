/**
 * FBX 스키닝 메시 부분 파서를 선언한다.
 *
 * FBX 원본 mesh와 skin 정보를 읽어 FFbxSkinnedMeshPart 배열을 만든다. 스켈레톤 조립 단계가 본
 * 계층과 메시 파트를 안정적으로 결합할 수 있도록, 각 파트의 본 인덱스와 섹션 정보를 엔진 기준으로
 * 정리한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "FBXImportTypes.h"

/**
 * FBX mesh node에서 스키닝 가능한 부분 메시 데이터를 읽어오는 파서이다.
 *
 * skin cluster의 weight를 엔진 정점 형식으로 정규화하고, 스킨이 없는 rigid mesh도 본에 붙은 메시
 * 파트로 표현할 수 있게 변환한다.
 */
class FFbxSkeletalMeshPartParser final
{
  public:
    explicit FFbxSkeletalMeshPartParser(const FFbxImportMeta &InImportMeta)
        : ImportMeta(InImportMeta)
    {
    }

    /**
     * 수집된 FBX 메타 정보를 엔진 에셋 데이터로 변환한다.
     *
     * 출력 배열과 ID 매핑은 함수 시작 시 초기화되며, 실패한 일부 항목은 로그를 남기고 건너뛰는
     * 방식으로 전체 임포트 흐름이 가능한 한 계속 진행되도록 한다.
     */
    bool Parse(TArray<FFbxSkinnedMeshPart> &OutSkinnedMeshParts) const;

  private:
    bool ParseSkinnedMeshPart(int32 MeshId, FFbxSkinnedMeshPart &OutPart) const;
    bool ParseRigidAttachedMeshPart(int32 MeshId, FFbxSkinnedMeshPart &OutPart) const;

  private:
    const FFbxImportMeta &ImportMeta;
};
