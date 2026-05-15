/**
 * FBX 癒명떚由ъ뼹 ?꾪룷??蹂댁“ ?⑥닔?ㅼ쓣 ?좎뼵?쒕떎.
 *
 * ?먮낯 FBX 癒명떚由ъ뼹???대쫫, ?됱긽, ?띿뒪泥??뚯씪 寃쎈줈瑜??붿쭊??Material/Texture ?먯뀑 洹쒖튃??留욊쾶
 * 蹂?섑븯??湲곕뒫???쒓났?쒕떎. 硫붿떆 ?뚯꽌??吏?ㅻ찓?몃━ ?앹꽦??吏묒쨷?섍퀬, 癒명떚由ъ뼹 寃쎈줈 蹂듦뎄? uasset
 * ?앹꽦 ?뺤콉? ???좏떥由ы떚 怨꾩링?쇰줈 遺꾨━?쒕떎.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"

namespace FbxMaterialImportUtils
{
    FString NormalizeMaterialSlotName(const FString &SlotName);
    void    BuildStaticMaterials(FFbxImportMeta &ImportMeta, const FStaticMesh &Mesh,
                                 TArray<FMeshMaterial> &OutMaterials);
    void    BuildSkeletalMaterials(FFbxImportMeta &ImportMeta, const FSkeletalMesh &Mesh,
                                   TArray<FMeshMaterial> &OutMaterials);
} // namespace FbxMaterialImportUtils
