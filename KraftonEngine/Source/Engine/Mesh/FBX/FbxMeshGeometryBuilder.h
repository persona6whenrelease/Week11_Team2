/**
 * FBX 메시 지오메트리 변환 빌더를 선언한다.
 *
 * FBX SDK의 mesh 표현은 polygon corner, control point, layer element가 분리되어 있으므로 렌더러가
 * 바로 사용할 수 없다. 이 빌더는 노드 transform과 bind pose 기준 행렬을 반영하여 StaticMesh 또는
 * 스키닝용 부분 메시 데이터로 변환하는 책임을 가진다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "FBXImportTypes.h"
#include "Mesh/StaticMeshAsset.h"

#include <functional>

namespace FbxMeshGeometryBuilder
{
    FMatrix BuildGeometricTransform(FbxNode *Node);
    FMatrix BuildMeshToAssetBindMatrix(FbxNode *MeshNode, const FMatrix &MeshBindGlobal);

    bool BuildSkeletalMeshPartGeometry(
        const FFbxMeshMeta &MeshMeta, const FMatrix &MeshToAssetBindMatrix,
        const std::function<void(int32, FSkeletalVertex &)> &AssignWeights,
        FFbxSkinnedMeshPart                                 &OutPart);

    /**
     * FBX mesh의 지오메트리를 FStaticMesh 형식으로 빌드한다.
     *
     * 노드 기준 변환 행렬을 적용하고 polygon 데이터를 삼각형 리스트, 정점 배열, 섹션 정보로
     * 재구성한다.
     */
    bool BuildStaticMeshGeometry(const FFbxMeshMeta &MeshMeta, const FMatrix &MeshToAssetBindMatrix,
                                 FStaticMesh &OutMesh);
} // namespace FbxMeshGeometryBuilder
