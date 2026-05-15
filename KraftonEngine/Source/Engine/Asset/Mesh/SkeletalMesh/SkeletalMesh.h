/**
 * ?ㅼ펷?덊깉 硫붿떆 UObject ?섑띁瑜??좎뼵?쒕떎.
 *
 * FSkeletalMesh ?먯뀑 ?곗씠?곗? GPU 踰꾪띁, 癒명떚由ъ뼹 ?щ’???곌껐?섏뿬 ?먮뵒?곗? ?뚮뜑?ш? 媛숈? 媛앹껜瑜??듯빐
 * ?ㅼ펷?덊깉 硫붿떆瑜??ㅻ０ ???덇쾶 ?쒕떎. ?뚯씪 濡쒕뱶 寃곌낵濡?留뚮뱾?댁쭊 ?쒖닔 ?곗씠?곌? ?ㅼ젣 ?뚮뜑留?媛?ν븳
 * ?붿쭊 ?ㅻ툕?앺듃濡??щ씪?ㅻ뒗 吏?먯씠??
 */

#pragma once

#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Object/Object.h"

struct ID3D11Device;

/**
 * ?ㅼ펷?덊깉 硫붿떆 ?먯뀑 ?곗씠?곕? ?뚮뜑留?媛?ν븳 UObject濡?媛먯떬 ??낆씠??
 *
 * FSkeletalMesh???뺤젏, ?몃뜳?? 蹂? ?좊땲硫붿씠???뺣낫瑜?蹂닿??섍퀬 ?꾩슂??GPU 由ъ냼?ㅻ? ?앹꽦?쒕떎.
 * 而댄룷?뚰듃???먯뀑 ?먮뵒?곕뒗 ??媛앹껜瑜??듯빐 ?ㅼ펷?덊깉 硫붿떆???щ’, 蹂?怨꾩링, ?뚮뜑 踰꾪띁???묎렐?쒕떎.
 */
class USkeletalMesh : public UObject
{
  public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void Serialize(FArchive &Ar);

    const FString &GetAssetPathFileName() const;
    void           SetSkeletalMeshAsset(FSkeletalMesh *InMesh);
    FSkeletalMesh *GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }

    void                         SetMaterials(TArray<FMeshMaterial> &&InMaterials);
    const TArray<FMeshMaterial> &GetMaterials() const { return Materials; }

    const TArray<FMeshSection> &GetSections() const;

  private:
    void RebuildSectionMaterialIndices();

    FSkeletalMesh        *SkeletalMeshAsset = nullptr;
    TArray<FMeshMaterial> Materials;
};
