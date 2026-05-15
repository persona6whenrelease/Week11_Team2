/**
 * USkeletalMesh???고???媛앹껜 ?숈옉??援ы쁽?쒕떎.
 *
 * ?ㅼ펷?덊깉 硫붿떆 ?먯뀑 ?곗씠?곕줈遺???뚮뜑留곸뿉 ?꾩슂??踰꾪띁瑜?留뚮뱾怨? 癒명떚由ъ뼹 ?щ’怨?蹂??좊땲硫붿씠?? * ?뺣낫瑜?媛앹껜 ?덈꺼?먯꽌 ?묎렐?????덇쾶 ?쒕떎. ?꾩옱 援ъ“?먯꽌??CPU skinning?대굹 ?꾨━酉??ъ쫰 ?몄쭛 履쎌씠
 * ???곗씠?곕? 李몄“?섎?濡? ?먯뀑 ?곗씠?곗? ?뚮뜑 由ъ냼?ㅼ쓽 ?뚯쑀 寃쎄퀎瑜?紐낇솗???좎??섎뒗 ??븷???쒕떎.
 */

#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"

#include "Object/ObjectFactory.h"

#include <utility>

IMPLEMENT_CLASS(USkeletalMesh, UObject)

static const FString              EmptySkeletalPath;
static const TArray<FMeshSection> EmptySkeletalSections;

USkeletalMesh::~USkeletalMesh()
{
    delete SkeletalMeshAsset;
    SkeletalMeshAsset = nullptr;
}

void USkeletalMesh::Serialize(FArchive &Ar)
{
    if (Ar.IsLoading() && !SkeletalMeshAsset)
    {
        SkeletalMeshAsset = new FSkeletalMesh();
    }

    if (SkeletalMeshAsset)
    {
        SkeletalMeshAsset->Serialize(Ar);
    }
    Ar << Materials;

    if (Ar.IsLoading())
    {
        RebuildSectionMaterialIndices();
    }
}

const FString &USkeletalMesh::GetAssetPathFileName() const
{
    return SkeletalMeshAsset ? SkeletalMeshAsset->PathFileName : EmptySkeletalPath;
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh *InMesh)
{
    if (SkeletalMeshAsset != InMesh)
    {
        delete SkeletalMeshAsset;
        SkeletalMeshAsset = InMesh;
    }

    if (SkeletalMeshAsset && !SkeletalMeshAsset->bBoundsValid)
    {
        SkeletalMeshAsset->CacheBounds();
    }
    RebuildSectionMaterialIndices();
}

void USkeletalMesh::SetMaterials(TArray<FMeshMaterial> &&InMaterials)
{
    Materials = std::move(InMaterials);
    RebuildSectionMaterialIndices();
}

const TArray<FMeshSection> &USkeletalMesh::GetSections() const
{
    return SkeletalMeshAsset ? SkeletalMeshAsset->Sections : EmptySkeletalSections;
}

void USkeletalMesh::RebuildSectionMaterialIndices()
{
    if (!SkeletalMeshAsset)
    {
        return;
    }

    for (FMeshSection &Section : SkeletalMeshAsset->Sections)
    {
        Section.MaterialIndex = -1;
        for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
        {
            if (Materials[i].MaterialSlotName == Section.MaterialSlotName)
            {
                Section.MaterialIndex = i;
                break;
            }
        }
    }
}
