/**
 * FBX 파싱 결과를 렌더 가능한 메시 지오메트리로 조립하는 함수를 선언한다.
 *
 * 파서는 원본 FBX 노드와 클러스터 정보를 읽는 데 집중하고, 이 빌더는 정점 중복 제거, 삼각형 인덱스 생성,
 * 섹션 구성, 좌표계 변환을 담당한다. StaticMesh와 SkeletalMeshPart가 서로 다른 정점 형식을 사용하므로
 * 각 경로별 빌드 함수를 분리한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Types/FBXImportTypes.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"

#include <functional>

namespace FbxMeshGeometryBuilder
{
    /**
     * FBX 노드의 geometric transform을 엔진 행렬로 변환한다.
     */
    FMatrix BuildGeometricTransform(FbxNode *Node);
    /**
     * FBX mesh bind 기준 공간을 에셋 로컬 공간에 맞추는 변환 행렬을 만든다.
     */
    FMatrix BuildMeshToAssetBindMatrix(FbxNode *MeshNode, const FMatrix &MeshBindGlobal);

    bool BuildSkeletalMeshPartGeometry(
        const FFbxMeshMeta &MeshMeta, const FMatrix &MeshToAssetBindMatrix,
        const std::function<void(int32, FSkeletalVertex &)> &AssignWeights,
        FFbxSkinnedMeshPart                                 &OutPart);

    

    bool BuildStaticMeshGeometry(const FFbxMeshMeta &MeshMeta, const FMatrix &MeshToAssetBindMatrix,
                                 FStaticMesh &OutMesh);
} 
