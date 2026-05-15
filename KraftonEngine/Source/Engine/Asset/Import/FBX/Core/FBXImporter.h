/**
 * FBX ?꾪룷?곗쓽 理쒖긽??寃곌낵 ??낃낵 怨듦컻 ?꾪룷???명꽣?섏씠?ㅻ? ?좎뼵?쒕떎.
 *
 * FBX ?섎굹???⑥씪 硫붿떆媛 ?꾨땲????怨꾩링, ?щ윭 硫붿떆, ?ㅼ펷?덊넠, 癒명떚由ъ뼹, ?쇱씠?몃? ?숈떆???ы븿???? * ?덉쑝誘濡????뚯씪? 洹?寃곌낵瑜?FFBXAsset?쇰줈 臾띠뼱 ?쒗쁽?쒕떎. ImportFBX???먮낯 ?뚯씪???붿쭊????ν븯怨? * 李몄“?????덈뒗 ???먯뀑 援ъ“濡?諛붽씀??吏꾩엯?먯씠??
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Types/FBXImportTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"
#include "Serialization/Archive.h"

/**
 * FBX ?쇱씠????낆쓣 ?붿쭊?먯꽌 援щ텇?섍린 ?꾪븳 ?닿굅?뺤씠??
 *
 * ?먮낯 SDK????낆쓣 洹몃?濡??몄텧?섏? ?딄퀬, ???먯뀑 吏곷젹?붿? ?꾨━酉??뚮뜑留곸뿉???ъ슜?????덈뒗
 * 理쒖냼?쒖쓽 ?쇱씠??醫낅쪟濡??뺣━?쒕떎.
 */
enum class EFBXLightType
{
    Point,
    Directional,
    Spot,
};

/**
 * FBX ?ъ뿉??媛?몄삩 ?쇱씠???뺣낫瑜???ν븯???곗씠?곗씠??
 *
 * ?꾩튂, 諛⑺뼢, ?됱긽, 媛뺣룄, ???媛숈? ?뚮뜑留??꾨━酉곗뿉 ?꾩슂??媛믪쓣 ?대뒗?? FBXSceneAsset???먮낯 ?ъ쓽
 * 議곕챸 諛곗튂瑜?蹂댁〈?????ъ슜?쒕떎.
 */
struct FLightAsset
{
    EFBXLightType LightType;
    FMatrix       Transform;
};

using FCameraAsset = FMatrix;

/**
 * FBX ??怨꾩링??而댄룷?뚰듃 ??븷??援щ텇?섎뒗 ??낆씠??
 *
 * ?몃뱶媛 ?⑥닚 transform?몄?, ?뺤쟻 硫붿떆?몄?, ?ㅼ펷?덊깉 硫붿떆?몄?, ?쇱씠?몄씤吏???곕씪 ?꾩냽 ?앹꽦 濡쒖쭅?? * ?щ씪吏誘濡???????④퀎?먯꽌 紐낆떆?곸쑝濡?遺꾨쪟?쒕떎.
 */
enum class EFBXSceneComponentType
{
    StaticMesh,
    SkeletalMesh
};

/**
 * FBX ?ъ쓽 ?몃뱶 ?섎굹瑜??붿쭊 而댄룷?뚰듃 ?ㅻ챸?쇰줈 諛붽씔 ?곗씠?곗씠??
 *
 * 遺紐??먯떇 愿怨? 濡쒖뺄 transform, ?곌껐??硫붿떆???쇱씠???몃뜳?ㅻ? ?댁븘 ?먮낯 FBX 怨꾩링???ш뎄?깊븷 ?? * ?덇쾶 ?쒕떎. ?ㅼ젣 而댄룷?뚰듃瑜??앹꽦?섍린 ?꾩쓽 吏곷젹??媛?ν븳 ?ㅻ챸????븷???쒕떎.
 */
struct FFBXSceneComponentDesc
{
    EFBXSceneComponentType Type = EFBXSceneComponentType::StaticMesh;
    FString                Name;
    int32                  SourceNodeId = -1;
    int32                  SourceMeshId = -1;
    int32                  SourceSkeletonId = -1;
    int32                  StaticMeshAssetIndex = -1;
    int32                  SkeletalMeshAssetIndex = -1;
    FMatrix                RelativeTransform = FMatrix::Identity;

    friend FArchive &operator<<(FArchive &Ar, FFBXSceneComponentDesc &Desc)
    {
        Ar << Desc.Type;
        Ar << Desc.Name;
        Ar << Desc.SourceNodeId;
        Ar << Desc.SourceMeshId;
        Ar << Desc.SourceSkeletonId;
        Ar << Desc.StaticMeshAssetIndex;
        Ar << Desc.SkeletalMeshAssetIndex;
        Ar.Serialize(&Desc.RelativeTransform, sizeof(FMatrix));
        return Ar;
    }
};

/**
 * FBX ?먮낯 ?섎굹瑜??꾪룷?명뻽????留뚮뱾?댁????꾩껜 寃곌낵 臾띠쓬?대떎.
 *
 * ?뺤쟻 硫붿떆, ?ㅼ펷?덊깉 硫붿떆, ?쇱씠?? ??而댄룷?뚰듃 怨꾩링???④퍡 蹂닿??쒕떎. FBX媛 ?⑥씪 硫붿떆 ?뚯씪???꾨땲?? * ??而⑦뀒?대꼫?????덈떎???먯쓣 諛섏쁺???곸쐞 ?먯뀑 ?곗씠?곗씠??
 */
struct FFBXAsset
{
    FString                        PathFileName;
    TArray<FStaticMesh>            StaticMeshes;
    TArray<FSkeletalMesh>          SkeletalMeshes;
    TArray<TArray<FMeshMaterial>>  StaticMeshMaterials;
    TArray<TArray<FMeshMaterial>>  SkeletalMeshMaterials;
    TArray<FMeshMaterial>          SkeletalMaterials;
    TArray<FFBXSceneComponentDesc> SceneComponents;
    TMap<int32, int32>             MeshIdToStaticMeshAssetIndex;
    TMap<int32, int32>             SkeletonIdToSkeletalMeshAssetIndex;
    TArray<FLightAsset>            LightAssets;
    TArray<FCameraAsset>           CameraAssets;
};

/**
 * FBX SDK瑜??ъ슜???먮낯 FBX ?뚯씪???붿쭊 ?먯뀑 ?곗씠?곕줈 蹂?섑븯??理쒖긽???꾪룷?곗씠??
 *
 * 硫뷀? ?뚯떛, 硫붿떆 ?뚯떛, ?ㅼ펷?덊깉 硫붿떆 議곕┰, ?좊땲硫붿씠??異붿텧, ??而댄룷?뚰듃 援ъ꽦???쒖꽌?濡??섑뻾?쒕떎.
 * ?몄텧遺???뚯씪 寃쎈줈留??섍린怨? 寃곌낵濡?FBXSceneAsset ?앹꽦???꾩슂??FFBXAsset??諛쏅뒗??
 */
class FBXImporter
{
  public:
    bool ImportFbxAsset(const FString &InFilePath, FFBXAsset &OutFBXAsset);

  private:
    bool InitializeSdk();
    bool LoadScene(const FString &InFilePath);
    bool FinalizeAsset();
    void ShutdownSdk();

  private:
    void ClearState();
    void PreprocessScene();
    void DestroyScene();

  private:
    FFbxImportMeta ImportMeta;

  private:
    FbxManager *Manager = nullptr;
    FbxScene   *Scene = nullptr;
};
