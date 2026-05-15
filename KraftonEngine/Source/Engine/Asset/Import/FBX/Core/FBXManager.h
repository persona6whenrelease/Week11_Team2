/**
 * FBX 에셋 로딩, 씬 캐시, 씬 내부 참조 해석을 담당하는 매니저를 선언한다.
 *
 * FBX 원본 파일은 한 번의 로드로 여러 엔진 에셋을 만들 수 있으므로, 이 클래스는 씬 단위 캐시와
 * 개별 메시 참조 해석을 분리한다. Content Browser나 메시 선택 UI는 문자열 참조만 가지고 있어도
 * 이 매니저를 통해 실제 StaticMesh, SkeletalMesh, FBXSceneAsset을 얻을 수 있다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"

class USkeletalMesh;
class UStaticMesh;
class UFBXSceneAsset;
class UObject;

/**
 * FBX 씬 캐시와 FBX 내부 에셋 참조 해석을 담당하는 매니저이다.
 *
 * 원본 FBX를 씬 단위로 임포트하고, #Mesh_ 또는 #Skeleton_ 같은 내부 참조 문자열을 실제 엔진
 * 오브젝트로 변환한다. 반복 로드 시 같은 씬 캐시를 공유해 파싱 비용과 에셋 불일치를 줄인다.
 */
class FFBXManager
{
    static TMap<FString, UFBXSceneAsset *> FbxSceneCache;
    static TArray<FMeshAssetListItem>      AvailableFbxFiles;

  public:
    static USkeletalMesh *LoadSkeletalMesh(const FString &PathFileName);
    /**
     * FBX 원본 또는 캐시 경로를 UFBXSceneAsset으로 로드한다.
     *
     * 이미 캐시된 씬이 있으면 재사용하고, 없으면 원본 FBX를 임포트해 씬 에셋을 구성한다.
     */
    static UFBXSceneAsset *LoadFbxScene(const FString &PathFileName);
    static UStaticMesh    *ResolveStaticMeshReference(const FString &PathFileName);
    static USkeletalMesh  *ResolveSkeletalMeshReference(const FString &PathFileName);
    /**
     * FBX 씬 내부 메시 참조 문자열을 실제 UObject로 변환한다.
     *
     * #Mesh_, #Skeleton_ 같은 참조 규칙을 해석해 해당 씬 캐시 안의 StaticMesh 또는 SkeletalMesh를
     * 찾는다.
     */
    static UObject       *ResolveFbxSceneAssetReference(const FString &PathFileName);
    static UStaticMesh   *LoadStaticMeshFromFbxSceneReference(const FString &PathFileName);
    static USkeletalMesh *LoadSkeletalMeshFromFbxSceneReference(const FString &PathFileName);
    static void           ScanFbxSourceFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableFbxSourceFiles();

    static void ReleaseAllGPU();
};
