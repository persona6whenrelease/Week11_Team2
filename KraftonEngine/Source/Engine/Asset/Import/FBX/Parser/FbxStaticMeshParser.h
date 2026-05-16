/**
 * FBX 메타 정보에서 정적 메시로 분류된 노드를 파싱하는 타입을 선언한다.
 *
 * 스킨이 없는 FBX mesh node를 대상으로 지오메트리 빌더를 호출하고, 엔진의 FStaticMesh 중간 데이터로
 * 변환한다. 스켈레톤이나 애니메이션과 독립적인 배경 오브젝트 임포트 경로이다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"

/**
 * FBX 메타 정보에서 정적 메시로 분류된 노드를 FStaticMesh로 변환하는 파서이다.
 */
class FFbxStaticMeshParser final
{
  public:
    explicit FFbxStaticMeshParser(const FFbxImportMeta &InImportMeta) : ImportMeta(InImportMeta) {}

    

    bool Parse(TArray<FStaticMesh> &OutStaticMeshes,
               TMap<int32, int32>  &OutMeshIdToStaticMeshAssetIndex) const;

  private:
    const FFbxImportMeta &ImportMeta;
};
