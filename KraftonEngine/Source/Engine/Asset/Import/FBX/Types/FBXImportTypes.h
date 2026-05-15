/**
 * FBX ?ㅼ펷?덊깉 硫붿떆 議곕┰ 怨쇱젙?먯꽌 ?ъ슜?섎뒗 以묎컙 硫붿떆 ?뚰듃 援ъ“瑜??뺤쓽?쒕떎.
 *
 * FBX?먯꽌???섎굹???ㅼ펷?덊넠???щ윭 mesh node媛 skin?쇰줈 ?곌껐?섍굅?? rigid mesh媛 ?뱀젙 蹂몄뿉 遺숈뼱
 * ?덈뒗 ?앹쓽 援ъ꽦??媛?ν븯?? ???뚯씪????낅뱾? 洹몃윴 遺遺?硫붿떆?ㅼ쓣 ?뱀뀡怨??뺤젏 ?곗씠???⑥쐞濡?紐⑥븘
 * 理쒖쥌 FSkeletalMesh濡?蹂묓빀?섍린 ?꾩뿉 ?꾩슂???뺣낫瑜??대뒗??
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"

/** ?ㅽ궎??硫붿떆 ?뚰듃 ?대??먯꽌 癒명떚由ъ뼹蹂??몃뜳??踰붿쐞瑜??섑??대뒗 ?뱀뀡 ?곗씠?곗씠?? */
struct FFbxMeshPartSection
{
    int32   SourceMeshId = -1;
    int32   MaterialSlotIndex = 0;
    int32   SourceMaterialId = -1;
    FString MaterialSlotName = "None";
    int32   FirstIndex = 0;
    int32   IndexCount = 0;
};

/**
 * 理쒖쥌 ?ㅼ펷?덊깉 硫붿떆濡?蹂묓빀?섍린 ?꾩쓽 遺遺?硫붿떆 ?곗씠?곗씠??
 *
 * FBX mesh node ?섎굹?먯꽌 ?살? ?뺤젏, ?몃뜳?? ?뱀뀡, ?곌껐 ?ㅼ펷?덊넠 ?뺣낫瑜??대뒗?? 議곕┰ ?④퀎?먯꽌 媛숈?
 * ?ㅼ펷?덊넠??怨듭쑀?섎뒗 ?뚰듃?ㅼ씠 ?섎굹??FSkeletalMesh濡??⑹퀜吏꾨떎.
 */
struct FFbxSkinnedMeshPart
{
    int32                       MeshId = -1;
    int32                       SkinId = -1;
    int32                       SkeletonId = -1;
    int32                       AttachedBoneId = -1;
    int32                       AttachedSkeletonBoneIndex = -1;
    bool                        bRigidAttached = false;
    bool                        bSkinned = false;
    FString                     SourceNodePath;
    TArray<FSkeletalVertex>     Vertices;
    TArray<uint32>              Indices;
    TArray<FFbxMeshPartSection> Sections;
};
