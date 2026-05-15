/**
 * 硫붿떆 ?쒖뒪?쒖쓽 怨듦컻 Facade瑜??좎뼵?쒕떎.
 *
 * ?먮뵒?곗? ?고???履?肄붾뱶??OBJManager, FBXManager???몃? 援ы쁽??吏곸젒 ???꾩슂 ?놁씠 ???대옒?ㅻ?
 * ?듯빐 硫붿떆瑜?濡쒕뱶?섍퀬, ?먮낯 ?뚯씪 紐⑸줉???ㅼ틪?섍퀬, GPU 由ъ냼?ㅻ? ?댁젣?쒕떎. FBX ???대????뱀젙
 * 硫붿떆瑜?媛由ы궎??臾몄옄??李몄“ 洹쒖튃???ш린???먮퀎?섎?濡?Content Browser??Asset Editor媛 媛숈?
 * 寃쎈줈 ?댁꽍 諛⑹떇??怨듭쑀?????덈떎.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"

struct FImportOptions;
struct ID3D11Device;
class UFBXSceneAsset;
class UObject;
class USkeletalMesh;
class UStaticMesh;

/**
 * 硫붿떆 濡쒕뵫怨??ㅼ틪??理쒖긽???묎렐?먯씠??
 *
 * ?몄텧遺媛 OBJ, FBX ?먮낯, FBX ???대? 硫붿떆 李몄“瑜?吏곸젒 援щ텇?섏? ?딆븘???섎룄濡?寃쎈줈 臾몄옄?댁쓣
 * ?댁꽍?섍퀬 ?뚮쭪? 留ㅻ땲?濡??꾩엫?쒕떎. 硫붿떆 ?쒖뒪???몃????몄텧?섎뒗 API瑜????대옒?ㅻ줈 紐⑥븘 ?먯뼱 ?꾪룷?? * 諛⑹떇?대굹 罹먯떆 ?뺤콉??諛붾뚯뼱???먮뵒???뚮뜑??履?蹂寃?踰붿쐞瑜?以꾩씤??
 */
class FMeshManager
{
  public:
    static constexpr uint32 FbxSceneCacheMagic = 0x4E435346u;
    static constexpr uint32 FbxSceneCacheVersion = 2u;

    static bool IsFbxStaticMeshReference(const FString &PathFileName);
    static bool IsFbxSkeletalMeshReference(const FString &PathFileName);

    static FString GetObjBinaryFilePath(const FString &OriginalPath);
    static FString GetFbxSceneCacheFilePath(const FString &SourcePath);

    static UStaticMesh   *LoadStaticMesh(const FString &PathFileName, ID3D11Device *InDevice);
    static UStaticMesh   *LoadObjStaticMesh(const FString &PathFileName, ID3D11Device *InDevice);
    static UStaticMesh   *LoadObjStaticMesh(const FString        &PathFileName,
                                            const FImportOptions &Options, ID3D11Device *InDevice);
    static USkeletalMesh *LoadSkeletalMesh(const FString &PathFileName);
    /**
     * FBX ?먮낯 ?먮뒗 罹먯떆 寃쎈줈瑜?UFBXSceneAsset?쇰줈 濡쒕뱶?쒕떎.
     *
     * ?대? 罹먯떆???ъ씠 ?덉쑝硫??ъ궗?⑺븯怨? ?놁쑝硫??먮낯 FBX瑜??꾪룷?명빐 ???먯뀑??援ъ꽦?쒕떎.
     */
    static UFBXSceneAsset *LoadFbxScene(const FString &PathFileName);
    /**
     * FBX ???대? 硫붿떆 李몄“ 臾몄옄?댁쓣 ?ㅼ젣 UObject濡?蹂?섑븳??
     *
     * #Mesh_, #Skeleton_ 媛숈? 李몄“ 洹쒖튃???댁꽍???대떦 ??罹먯떆 ?덉쓽 StaticMesh ?먮뒗 SkeletalMesh瑜?     * 李얜뒗??
     */
    static UObject *ResolveFbxSceneAssetReference(const FString &PathFileName);

    static void ScanMeshAssets();
    static void ScanObjSourceFiles();
    static void ScanFbxSourceFiles();
    static void ScanAllAssets();

    static const TArray<FMeshAssetListItem> &GetAvailableStaticMeshFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableObjSourceFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableFbxSourceFiles();

    static void ReleaseAllGPU();
};
