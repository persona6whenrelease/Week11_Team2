/**
 * FBX 정적 메시 노드의 파싱 절차를 구현한다.
 *
 * 메타 파서가 static mesh로 분류한 노드를 순회하고, 각 노드의 FBX mesh 데이터를 StaticMesh 빌더에
 * 전달한다. 변환된 결과는 FBX 씬 에셋의 static mesh 배열에 추가된다.
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
