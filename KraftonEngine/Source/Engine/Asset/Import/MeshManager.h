/**
 * OBJ와 FBX를 함께 다루는 상위 메시 매니저를 선언한다.
 *
 * FMeshManager는 사용자가 요청한 경로가 OBJ인지, FBX 씬 캐시의 하위 에셋 참조인지 판단하고 적절한
 * 전용 매니저로 위임한다. 에디터 입장에서는 하나의 메시 로드 API를 사용하지만, 내부에서는 원본
 * 포맷과 캐시 방식에 따라 다른 경로를 선택한다.
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
class UAnimSequence;

/**
 * OBJ와 FBX 로드 경로를 하나로 묶어 제공하는 상위 메시 매니저이다.
 */
class FMeshManager
{
  public:
    static constexpr uint32 FbxSceneCacheMagic = 0x4E435346u;
    static constexpr uint32 FbxSceneCacheVersion = 5u;

    static bool IsFbxStaticMeshReference(const FString &PathFileName);
    static bool IsFbxSkeletalMeshReference(const FString &PathFileName);

    static FString GetObjBinaryFilePath(const FString &OriginalPath);
    static FString GetFbxSceneCacheFilePath(const FString &SourcePath);

    static UStaticMesh   *LoadStaticMesh(const FString &PathFileName, ID3D11Device *InDevice);
    static UStaticMesh   *LoadObjStaticMesh(const FString &PathFileName, ID3D11Device *InDevice);
    static UStaticMesh   *LoadObjStaticMesh(const FString        &PathFileName,
                                            const FImportOptions &Options, ID3D11Device *InDevice);
    static USkeletalMesh *LoadSkeletalMesh(const FString &PathFileName);
    static UAnimSequence *ResolveAnimSequenceReference(const FString &PathFileName);

    /**
     * UAnimSequence를 단일 `.asset` 파일로 저장한다. 본문 헤더는 UAnimSequence::Serialize가 자체 작성한다.
     * SkeletonAssetPath가 비어 있어도 저장은 그대로 진행되며, 다시 로드했을 때 PreviewMesh 매칭이 실패할 수 있다.
     */
    static bool SaveAnimSequenceToFile(const UAnimSequence *Sequence, const FString &PathFileName);
    /**
     * `.asset` 단일 파일에서 UAnimSequence를 로드한다. 같은 경로로 반복 호출하면 메모리 캐시 적중분을 반환한다.
     * 캐시는 프로세스 종료 전까지 비우지 않는다 (FBX scene cache와 동일한 영구 보관 패턴).
     */
    static UAnimSequence *LoadAnimSequenceFromFile(const FString &PathFileName);

    static UFBXSceneAsset *LoadFbxScene(const FString &PathFileName);
    static int32          GetAnimSequenceCountForSkeletalMesh(const UFBXSceneAsset *SceneAsset,
                                                              const USkeletalMesh  *SkeletalMesh);
    static UAnimSequence *FindAnimSequenceForSkeletalMesh(UFBXSceneAsset      *SceneAsset,
                                                          const USkeletalMesh *SkeletalMesh,
                                                          int32 SequenceIndex,
                                                          FString             *OutPath = nullptr);
    static USkeletalMesh *FindSkeletalMeshForAnimSequence(UFBXSceneAsset       *SceneAsset,
                                                          const UAnimSequence  *Sequence);

    static UObject *ResolveFbxSceneAssetReference(const FString &PathFileName);

    /**
     * 저장된 메시 에셋과 원본 메시 파일을 스캔해 목록을 갱신한다.
     */
    static void ScanMeshAssets();
    /**
     * 프로젝트 원본 폴더에서 OBJ 파일을 찾아 에디터 목록용 항목으로 수집한다.
     */
    static void ScanObjSourceFiles();
    /**
     * 프로젝트 원본 폴더에서 FBX 파일을 찾아 에디터 목록용 항목으로 수집한다.
     */
    static void ScanFbxSourceFiles();
    /**
     * OBJ와 FBX 양쪽 경로를 모두 스캔해 통합 메시 목록을 구성한다.
     */
    static void ScanAllAssets();

    static const TArray<FMeshAssetListItem> &GetAvailableStaticMeshFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableObjSourceFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableFbxSourceFiles();

    /**
     * 캐시에 남아 있는 모든 텍스처의 GPU 리소스를 해제한다.
     */
    static void ReleaseAllGPU();
};
