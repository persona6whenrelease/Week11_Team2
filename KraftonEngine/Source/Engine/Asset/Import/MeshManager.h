/**
 * 메시 시스템의 공개 Facade를 선언한다.
 *
 * 에디터와 런타임 쪽 코드는 OBJManager, FBXManager의 세부 구현을 직접 알 필요 없이 이 클래스를
 * 통해 메시를 로드하고, 원본 파일 목록을 스캔하고, GPU 리소스를 해제한다. FBX 씬 내부의 특정
 * 메시를 가리키는 문자열 참조 규칙도 여기서 판별하므로 Content Browser나 Asset Editor가 같은
 * 경로 해석 방식을 공유할 수 있다.
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
 * 메시 로딩과 스캔의 최상위 접근점이다.
 *
 * 호출부가 OBJ, FBX 원본, FBX 씬 내부 메시 참조를 직접 구분하지 않아도 되도록 경로 문자열을
 * 해석하고 알맞은 매니저로 위임한다. 메시 시스템 외부에 노출되는 API를 이 클래스로 모아 두어 임포트
 * 방식이나 캐시 정책이 바뀌어도 에디터/렌더러 쪽 변경 범위를 줄인다.
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
     * FBX 원본 또는 캐시 경로를 UFBXSceneAsset으로 로드한다.
     *
     * 이미 캐시된 씬이 있으면 재사용하고, 없으면 원본 FBX를 임포트해 씬 에셋을 구성한다.
     */
    static UFBXSceneAsset *LoadFbxScene(const FString &PathFileName);
    /**
     * FBX 씬 내부 메시 참조 문자열을 실제 UObject로 변환한다.
     *
     * #Mesh_, #Skeleton_ 같은 참조 규칙을 해석해 해당 씬 캐시 안의 StaticMesh 또는 SkeletalMesh를
     * 찾는다.
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
