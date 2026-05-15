/**
 * FBX mesh node 중 StaticMesh로 사용할 수 있는 데이터를 파싱한다.
 *
 * 각 mesh node의 bind transform을 기준으로 지오메트리를 엔진 좌표계에 맞춰 변환하고, 결과
 * StaticMesh를 FBX 씬 내부 mesh id와 연결한다. 스키닝 정보가 필요한 부분은 별도 파서가 처리하므로
 * 이 구현부는 정적 지오메트리 변환에 집중한다.
 */

#include "Asset/Import/FBX/Parser/FbxStaticMeshParser.h"

#include "Core/Log.h"
#include "Asset/Import/FBX/Core/FBXUtil.h"
#include "Asset/Import/FBX/Builder/FbxMeshGeometryBuilder.h"

#include <fbxsdk.h>

bool FFbxStaticMeshParser::Parse(TArray<FStaticMesh> &OutStaticMeshes,
                                 TMap<int32, int32>  &OutMeshIdToStaticMeshAssetIndex) const
{
    OutStaticMeshes.clear();
    OutMeshIdToStaticMeshAssetIndex.clear();

    for (const FFbxMeshMeta &MeshMeta : ImportMeta.Meshes)
    {
        if (!MeshMeta.Mesh || !MeshMeta.Node)
        {
            continue;
        }

        FStaticMesh StaticMesh;
        StaticMesh.PathFileName =
            ImportMeta.SourceFilePath + "#Mesh_" + std::to_string(MeshMeta.MeshId);

        const FMatrix MeshBindGlobal =
            FBXUtil::ConvertFbxMatrix(MeshMeta.Node->EvaluateGlobalTransform());
        const FMatrix MeshToAssetBindMatrix =
            FbxMeshGeometryBuilder::BuildMeshToAssetBindMatrix(MeshMeta.Node, MeshBindGlobal);

        if (!FbxMeshGeometryBuilder::BuildStaticMeshGeometry(MeshMeta, MeshToAssetBindMatrix,
                                                             StaticMesh))
        {
            UE_LOG("[FBXImporter] Failed to parse static mesh geometry. MeshId=%d Node=%s",
                   MeshMeta.MeshId, MeshMeta.SourceNodePath.c_str());
            continue;
        }

        const int32 AssetIndex = static_cast<int32>(OutStaticMeshes.size());
        OutStaticMeshes.push_back(std::move(StaticMesh));
        OutMeshIdToStaticMeshAssetIndex[MeshMeta.MeshId] = AssetIndex;
    }

    UE_LOG("[FBXImporter] Parsed static meshes. Total=%u",
           static_cast<uint32>(OutStaticMeshes.size()));
    return true;
}
