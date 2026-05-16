/**
 * 정적 메시 LOD 생성을 위한 간소화 유틸리티를 선언한다.
 *
 * 입력 메시의 정점/인덱스/섹션을 받아 목표 비율에 맞는 축소 메시를 만든다. 원본 에셋과 별개로 계산된
 * 결과를 반환하므로, UStaticMesh는 이를 추가 LOD 버퍼로 보관할 수 있다.
 */

#pragma once

#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"

/**
 * 간소화 결과로 생성된 정점/인덱스/섹션 데이터를 보관하는 구조이다.
 */
struct FSimplifiedMesh
{
    TArray<FNormalVertex>      Vertices;
    TArray<uint32>             Indices;
    TArray<FStaticMeshSection> Sections;
};

/**
 * 입력 정적 메시를 목표 비율에 맞춰 축소하는 LOD 생성 유틸리티이다.
 */
class FMeshSimplifier
{
  public:
    

    static FSimplifiedMesh Simplify(const TArray<FNormalVertex>      &InVertices,
                                    const TArray<uint32>             &InIndices,
                                    const TArray<FStaticMeshSection> &InSections,
                                    float                             TargetRatio);
};
