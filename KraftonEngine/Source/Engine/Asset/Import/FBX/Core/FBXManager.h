/**
 * FBX 원본 파일과 캐시된 씬 에셋을 관리하는 매니저를 선언한다.
 *
 * FFBXManager는 Asset/Source의 FBX를 스캔하고, 필요한 경우 임포트를 수행해 UFBXSceneAsset 캐시를
 * 만든다. 외부에서는 FBX 하위 에셋 참조 문자열을 통해 static/skeletal mesh를 요청할 수 있으며, 매니저는
 * 참조 해석과 GPU 리소스 생성을 함께 담당한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"

class USkeletalMesh;
class UStaticMesh;
class UAnimSequence;
class UFBXSceneAsset;
class UObject;

/**
 * FBX 원본 스캔, 씬 캐시, 하위 에셋 참조 해석을 담당하는 매니저이다.
 *
 * 외부 API는 특정 FBX 내부의 static/skeletal mesh 참조를 요청하고, 이 매니저는 캐시 유효성 검사와 UObject
 * 복원을 수행한다.
 */
class FFBXManager
{
    static TMap<FString, UFBXSceneAsset *> FbxSceneCache;
    static TArray<FMeshAssetListItem>      AvailableFbxFiles;

  public:
    static USkeletalMesh *LoadSkeletalMesh(const FString &PathFileName);
    

    static UFBXSceneAsset *LoadFbxScene(const FString &PathFileName);
    static UStaticMesh    *ResolveStaticMeshReference(const FString &PathFileName);
    static USkeletalMesh  *ResolveSkeletalMeshReference(const FString &PathFileName);
    static UAnimSequence  *ResolveAnimSequenceReference(const FString &PathFileName);
    

    static UObject       *ResolveFbxSceneAssetReference(const FString &PathFileName);
    static UStaticMesh   *LoadStaticMeshFromFbxSceneReference(const FString &PathFileName);
    static USkeletalMesh *LoadSkeletalMeshFromFbxSceneReference(const FString &PathFileName);
    /**
     * 프로젝트 원본 폴더에서 FBX 파일을 찾아 에디터 목록용 항목으로 수집한다.
     */
    static void           ScanFbxSourceFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableFbxSourceFiles();

    /**
     * 캐시에 남아 있는 모든 텍스처의 GPU 리소스를 해제한다.
     */
    static void ReleaseAllGPU();
};
