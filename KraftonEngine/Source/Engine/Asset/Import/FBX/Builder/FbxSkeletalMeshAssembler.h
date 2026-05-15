/**
 * ?ㅽ궎??硫붿떆 ?뚰듃瑜??ㅼ펷?덊깉 硫붿떆 ?먯뀑?쇰줈 議곕┰?섎뒗 ?대옒?ㅻ? ?좎뼵?쒕떎.
 *
 * ?뚯꽌??FBX ?먮낯 ?몃뱶蹂꾨줈 遺遺??곗씠?곕? 留뚮뱾怨? ??議곕┰湲곕뒗 ?ㅼ펷?덊넠 ?⑥쐞濡?洹?寃곌낵瑜?臾띠뼱 ?붿쭊?? * FSkeletalMesh 諛곗뿴??留뚮뱺?? 硫뷀? ?뺣낫??skeleton id? 寃곌낵 ?먯뀑 ?몃뜳???ъ씠??留ㅽ븨?????④퀎?먯꽌
 * ?뺤젙?쒕떎.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Types/FBXImportTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"

/**
 * FBX ?ㅽ궎???뚰듃?ㅼ쓣 ?ㅼ펷?덊넠 ?⑥쐞??FSkeletalMesh濡?蹂묓빀?섎뒗 議곕┰湲곗씠??
 *
 * ?뚯떛 ?④퀎?먯꽌 遺꾨━??遺遺?硫붿떆?ㅼ쓣 媛숈? skeleton id 湲곗??쇰줈 臾띔퀬, ?뺤젏/?몃뜳???뱀뀡 踰붿쐞? 蹂? * 留ㅽ븨??理쒖쥌 ?먯뀑 湲곗??쇰줈 ?ㅼ떆 ?뺣━?쒕떎.
 */
class FFbxSkeletalMeshAssembler final
{
  public:
    explicit FFbxSkeletalMeshAssembler(const FFbxImportMeta &InImportMeta)
        : ImportMeta(InImportMeta)
    {
    }

    bool Assemble(const TArray<FFbxSkinnedMeshPart> &SkinnedMeshParts,
                  TArray<FSkeletalMesh>             &OutSkeletalMeshAssets,
                  TMap<int32, int32>                &OutSkeletonIdToSkeletalMeshAssetIndex) const;

  private:
    bool BuildSkeletalMeshFromParts(const FFbxSkeletonMeta                    &SkeletonMeta,
                                    const TArray<const FFbxSkinnedMeshPart *> &Parts,
                                    FSkeletalMesh                             &OutMesh) const;

    bool ValidateSkinnedMeshPartForAttach(const FFbxSkeletonMeta    &SkeletonMeta,
                                          const FFbxSkinnedMeshPart &Part) const;

  private:
    const FFbxImportMeta &ImportMeta;
};
