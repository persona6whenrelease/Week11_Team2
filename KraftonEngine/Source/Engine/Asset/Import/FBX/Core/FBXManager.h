/**
 * FBX ?먯뀑 濡쒕뵫, ??罹먯떆, ???대? 李몄“ ?댁꽍???대떦?섎뒗 留ㅻ땲?瑜??좎뼵?쒕떎.
 *
 * FBX ?먮낯 ?뚯씪? ??踰덉쓽 濡쒕뱶濡??щ윭 ?붿쭊 ?먯뀑??留뚮뱾 ???덉쑝誘濡? ???대옒?ㅻ뒗 ???⑥쐞 罹먯떆?
 * 媛쒕퀎 硫붿떆 李몄“ ?댁꽍??遺꾨━?쒕떎. Content Browser??硫붿떆 ?좏깮 UI??臾몄옄??李몄“留?媛吏怨??덉뼱?? * ??留ㅻ땲?瑜??듯빐 ?ㅼ젣 StaticMesh, SkeletalMesh, FBXSceneAsset???살쓣 ???덈떎.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"

class USkeletalMesh;
class UStaticMesh;
class UFBXSceneAsset;
class UObject;

/**
 * FBX ??罹먯떆? FBX ?대? ?먯뀑 李몄“ ?댁꽍???대떦?섎뒗 留ㅻ땲??대떎.
 *
 * ?먮낯 FBX瑜????⑥쐞濡??꾪룷?명븯怨? #Mesh_ ?먮뒗 #Skeleton_ 媛숈? ?대? 李몄“ 臾몄옄?댁쓣 ?ㅼ젣 ?붿쭊
 * ?ㅻ툕?앺듃濡?蹂?섑븳?? 諛섎났 濡쒕뱶 ??媛숈? ??罹먯떆瑜?怨듭쑀???뚯떛 鍮꾩슜怨??먯뀑 遺덉씪移섎? 以꾩씤??
 */
class FFBXManager
{
    static TMap<FString, UFBXSceneAsset *> FbxSceneCache;
    static TArray<FMeshAssetListItem>      AvailableFbxFiles;

  public:
    static USkeletalMesh *LoadSkeletalMesh(const FString &PathFileName);
    /**
     * FBX ?먮낯 ?먮뒗 罹먯떆 寃쎈줈瑜?UFBXSceneAsset?쇰줈 濡쒕뱶?쒕떎.
     *
     * ?대? 罹먯떆???ъ씠 ?덉쑝硫??ъ궗?⑺븯怨? ?놁쑝硫??먮낯 FBX瑜??꾪룷?명빐 ???먯뀑??援ъ꽦?쒕떎.
     */
    static UFBXSceneAsset *LoadFbxScene(const FString &PathFileName);
    static UStaticMesh    *ResolveStaticMeshReference(const FString &PathFileName);
    static USkeletalMesh  *ResolveSkeletalMeshReference(const FString &PathFileName);
    /**
     * FBX ???대? 硫붿떆 李몄“ 臾몄옄?댁쓣 ?ㅼ젣 UObject濡?蹂?섑븳??
     *
     * #Mesh_, #Skeleton_ 媛숈? 李몄“ 洹쒖튃???댁꽍???대떦 ??罹먯떆 ?덉쓽 StaticMesh ?먮뒗 SkeletalMesh瑜?     * 李얜뒗??
     */
    static UObject       *ResolveFbxSceneAssetReference(const FString &PathFileName);
    static UStaticMesh   *LoadStaticMeshFromFbxSceneReference(const FString &PathFileName);
    static USkeletalMesh *LoadSkeletalMeshFromFbxSceneReference(const FString &PathFileName);
    static void           ScanFbxSourceFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableFbxSourceFiles();

    static void ReleaseAllGPU();
};
