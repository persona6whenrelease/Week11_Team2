/**
 * FBX 정적 메시 파서를 선언한다.
 *
 * 수집된 FBX 메타 정보에서 mesh node를 순회하여 FStaticMesh 배열을 만들고, 원본 mesh id가 결과
 * 배열의 어느 인덱스로 변환되었는지 기록한다. 씬 컴포넌트가 나중에 해당 메시를 참조할 때 필요한
 * 연결 정보이다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"

/**
 * FBX mesh node를 정적 메시 에셋 데이터로 변환하는 파서이다.
 *
 * 스키닝이 필요하지 않은 지오메트리를 FStaticMesh 배열로 만들고, FBX 씬 컴포넌트가 참조할 수 있도록
 * 원본 mesh id와 결과 에셋 인덱스의 매핑을 남긴다.
 */
class FFbxStaticMeshParser final
{
  public:
    explicit FFbxStaticMeshParser(const FFbxImportMeta &InImportMeta) : ImportMeta(InImportMeta) {}

    /**
     * 수집된 FBX 메타 정보를 엔진 에셋 데이터로 변환한다.
     *
     * 출력 배열과 ID 매핑은 함수 시작 시 초기화되며, 실패한 일부 항목은 로그를 남기고 건너뛰는
     * 방식으로 전체 임포트 흐름이 가능한 한 계속 진행되도록 한다.
     */
    bool Parse(TArray<FStaticMesh> &OutStaticMeshes,
               TMap<int32, int32>  &OutMeshIdToStaticMeshAssetIndex) const;

  private:
    const FFbxImportMeta &ImportMeta;
};
