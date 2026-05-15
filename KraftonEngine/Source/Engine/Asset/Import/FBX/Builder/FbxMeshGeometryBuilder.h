/**
 * FBX 硫붿떆 吏?ㅻ찓?몃━ 蹂??鍮뚮뜑瑜??좎뼵?쒕떎.
 *
 * FBX SDK??mesh ?쒗쁽? polygon corner, control point, layer element媛 遺꾨━?섏뼱 ?덉쑝誘濡??뚮뜑?ш?
 * 諛붾줈 ?ъ슜?????녿떎. ??鍮뚮뜑???몃뱶 transform怨?bind pose 湲곗? ?됰젹??諛섏쁺?섏뿬 StaticMesh ?먮뒗
 * ?ㅽ궎?앹슜 遺遺?硫붿떆 ?곗씠?곕줈 蹂?섑븯??梨낆엫??媛吏꾨떎.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Types/FBXImportTypes.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"

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
     * FBX mesh??吏?ㅻ찓?몃━瑜?FStaticMesh ?뺤떇?쇰줈 鍮뚮뱶?쒕떎.
     *
     * ?몃뱶 湲곗? 蹂???됰젹???곸슜?섍퀬 polygon ?곗씠?곕? ?쇨컖??由ъ뒪?? ?뺤젏 諛곗뿴, ?뱀뀡 ?뺣낫濡?     * ?ш뎄?깊븳??
     */
    bool BuildStaticMeshGeometry(const FFbxMeshMeta &MeshMeta, const FMatrix &MeshToAssetBindMatrix,
                                 FStaticMesh &OutMesh);
} // namespace FbxMeshGeometryBuilder
