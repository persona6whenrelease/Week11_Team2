/**
 * ?뺤쟻 硫붿떆 ?먯뀑??吏곷젹??媛?ν븳 ?쒖닔 ?곗씠??援ъ“瑜??뺤쓽?쒕떎.
 *
 * ?꾩튂, ?몃쭚, ?꾩젨?? UV瑜?媛吏??뺤젏 諛곗뿴怨??몃뜳?? ?뱀뀡, 癒명떚由ъ뼹 ?щ’????ν븳?? FBX/OBJ
 * ?꾪룷?곕뒗 媛곸옄???먮낯 ?щ㎎????援ъ“濡?蹂?섑븯怨? UStaticMesh?????곗씠?곕? 湲곕컲?쇰줈 GPU 由ъ냼?ㅻ?
 * 留뚮뱺??
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Engine/Object/Object.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Render/Resource/Buffer.h"
#include "Serialization/Archive.h"
#include <memory>
#include <algorithm>

/**
 * ?뺤쟻 硫붿떆 ?뚮뜑留곸뿉 ?ъ슜?섎뒗 湲곕낯 ?뺤젏 ?뺤떇?대떎.
 *
 * ?꾩튂, ?몃쭚, ?꾩젨?? UV瑜??ы븿?섎ŉ StaticMesh? 硫붿떆 媛꾩냼?? FBX/OBJ ?꾪룷?곌? 怨듯넻?쇰줈 ?ъ슜?섎뒗
 * ?뺤젏 踰꾪띁 ?⑥쐞?대떎.
 */
struct FNormalVertex
{
    FVector  pos;
    FVector  normal;
    FVector4 color;
    FVector2 tex;
    FVector4 tangent;
};

/**
 * ?뺤쟻 硫붿떆 ?먯뀑??????⑥쐞?대떎.
 *
 * LOD蹂?吏?ㅻ찓?몃━, ?뱀뀡, 癒명떚由ъ뼹 ?щ’???ы븿?섎ŉ UObject??GPU 踰꾪띁???섏〈?섏? ?딅뒗?? ?먮낯
 * ?꾪룷??寃곌낵瑜???ν븯嫄곕굹 ?ㅼ떆 濡쒕뱶?????ъ슜?섎뒗 ?쒖닔 ?곗씠??紐⑤뜽?대떎.
 */
struct FStaticMesh
{
    FString               PathFileName;
    TArray<FNormalVertex> Vertices;
    TArray<uint32>        Indices;

    TArray<FStaticMeshSection> Sections;

    std::unique_ptr<FMeshBuffer> RenderBuffer;

    FVector BoundsCenter = FVector(0, 0, 0);
    FVector BoundsExtent = FVector(0, 0, 0);
    bool    bBoundsValid = false;

    void CacheBounds()
    {
        bBoundsValid = false;
        if (Vertices.empty())
            return;

        FVector LocalMin = Vertices[0].pos;
        FVector LocalMax = Vertices[0].pos;
        for (const FNormalVertex &V : Vertices)
        {
            LocalMin.X = (std::min)(LocalMin.X, V.pos.X);
            LocalMin.Y = (std::min)(LocalMin.Y, V.pos.Y);
            LocalMin.Z = (std::min)(LocalMin.Z, V.pos.Z);
            LocalMax.X = (std::max)(LocalMax.X, V.pos.X);
            LocalMax.Y = (std::max)(LocalMax.Y, V.pos.Y);
            LocalMax.Z = (std::max)(LocalMax.Z, V.pos.Z);
        }

        BoundsCenter = (LocalMin + LocalMax) * 0.5f;
        BoundsExtent = (LocalMax - LocalMin) * 0.5f;
        bBoundsValid = true;
    }

    void Serialize(FArchive &Ar)
    {
        Ar << PathFileName;
        Ar << Vertices;
        Ar << Indices;
        Ar << Sections;
    }
};
