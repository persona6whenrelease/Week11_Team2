/**
 * ?뺤쟻 硫붿떆 UObject ?섑띁? LOD蹂??뚮뜑 ?곗씠?곕? ?좎뼵?쒕떎.
 *
 * FStaticMesh ?먯뀑 ?곗씠?곕? 湲곕컲?쇰줈 ?щ윭 LOD, ?뱀뀡, 癒명떚由ъ뼹 ?щ’, GPU 踰꾪띁瑜?愿由ы븳?? ?먮뵒?곗뿉?? * 濡쒕뱶??硫붿떆? ?뚮뜑?ш? draw command瑜?留뚮뱾 ??李몄“?섎뒗 硫붿떆 媛앹껜媛 媛숈? ?곗씠??紐⑤뜽??諛붾씪蹂대룄濡? * ?섎뒗 ?곌껐 怨꾩링?대떎.
 */

#pragma once

#include "Object/Object.h"
#include "Collision/MeshTriangleBVH.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"
#include "Serialization/Archive.h"

#include <memory>

struct ID3D11Device;

/**
 * StaticMesh???뱀젙 LOD媛 ?뚮뜑留곸뿉 ?꾩슂???곗씠?곕? 臾띠? 援ъ“?대떎.
 *
 * 媛?LOD??蹂꾨룄???뺤젏/?몃뜳??踰꾪띁? ?뱀뀡 ?뺣낫瑜?媛吏꾨떎. ?뚮뜑留??④퀎?먯꽌??嫄곕━???ㅼ젙???곕씪
 * ?ъ슜??LOD瑜??좏깮?섍퀬 ??援ъ“??踰꾪띁瑜?draw call??諛붿씤?⑺븳??
 */
struct FLODMeshData
{
    TArray<FStaticMeshSection>   Sections;
    std::unique_ptr<FMeshBuffer> RenderBuffer;
};

/**
 * ?뺤쟻 硫붿떆 ?먯뀑 ?곗씠?곕? ?뚮뜑留?媛?ν븳 UObject濡?媛먯떬 ??낆씠??
 *
 * FStaticMesh??LOD ?곗씠?곗? 癒명떚由ъ뼹 ?щ’??蹂닿??섍퀬, D3D 踰꾪띁 ?앹꽦/?댁젣瑜??대떦?쒕떎. OBJ/FBX
 * ?꾪룷?곌? 留뚮뱺 ?쒖닔 ?곗씠?곕뒗 ????낆쓣 嫄곗퀜 ?ㅼ젣 ?뚮뜑 ?뚯씠?꾨씪?몄뿉???ъ슜?????덈뒗 由ъ냼?ㅺ? ?쒕떎.
 */
class UStaticMesh : public UObject
{
  public:
    DECLARE_CLASS(UStaticMesh, UObject)

    static constexpr uint32 MAX_LOD_COUNT = 4;

    UStaticMesh() = default;
    ~UStaticMesh() override;

    void Serialize(FArchive &Ar);

    const FString                 &GetAssetPathFileName() const;
    void                           SetStaticMeshAsset(FStaticMesh *InMesh);
    FStaticMesh                   *GetStaticMeshAsset() const;
    void                           SetStaticMaterials(TArray<FStaticMaterial> &&InMaterials);
    const TArray<FStaticMaterial> &GetStaticMaterials() const;

    void InitResources(ID3D11Device *InDevice);

    void EnsureMeshTrianglePickingBVHBuilt() const;
    bool RaycastMeshTrianglesWithBVHLocal(const FVector &LocalOrigin, const FVector &LocalDirection,
                                          FHitResult &OutHitResult) const;

    uint32                            GetLODCount() const { return bHasLOD ? MAX_LOD_COUNT : 1; }
    FMeshBuffer                      *GetLODMeshBuffer(uint32 LODLevel) const;
    const TArray<FStaticMeshSection> &GetLODSections(uint32 LODLevel) const;

  private:
    FStaticMesh             *StaticMeshAsset = nullptr;
    TArray<FStaticMaterial>  StaticMaterials;
    mutable FMeshTriangleBVH MeshTrianglePickingBVH;

    FLODMeshData AdditionalLODs[3];
    bool         bHasLOD = false;
};
