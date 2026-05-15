/**
 * StaticMesh를 더 낮은 삼각형 수로 변환하기 위한 간소화 인터페이스를 선언한다.
 *
 * 입력으로 원본 정점, 인덱스, 섹션 정보를 받고 목표 삼각형 비율에 맞춘 새 메시 데이터를 반환한다.
 * 이 타입은 에셋 저장 포맷을 직접 다루지 않고, LOD 생성 단계에서 사용할 수 있는 순수 데이터 변환
 * 결과만 제공한다.
 */

#pragma once

#include "Mesh/StaticMeshAsset.h"

/**
 * 메시 간소화 결과로 생성되는 독립 데이터 묶음이다.
 *
 * 원본 StaticMesh 객체를 직접 수정하지 않고, 새 정점/인덱스/섹션 배열을 반환하기 위해 사용된다.
 * 이후 단계에서 이 데이터를 LOD로 저장하거나 프리뷰 메시로 올릴 수 있다.
 */
struct FSimplifiedMesh
{
    TArray<FNormalVertex>      Vertices;
    TArray<uint32>             Indices;
    TArray<FStaticMeshSection> Sections;
};

/**
 * StaticMesh의 삼각형 수를 줄이는 순수 알고리즘 클래스이다.
 *
 * 에셋 로딩, UObject, GPU 리소스에 의존하지 않고 정점/인덱스 배열만 받아 결과를 만든다. 따라서
 * 임포트 직후 LOD 생성, 에디터의 수동 간소화, 테스트 코드에서 같은 로직을 재사용할 수 있다.
 */
class FMeshSimplifier
{
  public:
    /**
     * 입력 메시를 목표 삼각형 비율에 맞춰 간소화한다.
     *
     * 원본 배열은 수정하지 않고 새 정점/인덱스/섹션 배열을 반환한다. TargetRatio는 남길 삼각형 수의
     * 비율이며, 0.5는 대략 절반 수준의 삼각형 수를 의미한다.
     */
    static FSimplifiedMesh Simplify(const TArray<FNormalVertex>      &InVertices,
                                    const TArray<uint32>             &InIndices,
                                    const TArray<FStaticMeshSection> &InSections,
                                    float                             TargetRatio);
};
