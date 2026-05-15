/**
 * FBX mesh node 以?StaticMesh濡??ъ슜?????덈뒗 ?곗씠?곕? ?뚯떛?쒕떎.
 *
 * 媛?mesh node??bind transform??湲곗??쇰줈 吏?ㅻ찓?몃━瑜??붿쭊 醫뚰몴怨꾩뿉 留욎떠 蹂?섑븯怨? 寃곌낵
 * StaticMesh瑜?FBX ???대? mesh id? ?곌껐?쒕떎. ?ㅽ궎???뺣낫媛 ?꾩슂??遺遺꾩? 蹂꾨룄 ?뚯꽌媛 泥섎━?섎?濡? * ??援ы쁽遺???뺤쟻 吏?ㅻ찓?몃━ 蹂?섏뿉 吏묒쨷?쒕떎.
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
