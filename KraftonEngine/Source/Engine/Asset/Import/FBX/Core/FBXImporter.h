/**
 * FBX SDK를 사용해 원본 FBX 씬을 엔진 중간 에셋으로 변환하는 임포터를 선언한다.
 *
 * 이 클래스는 SDK 초기화, 씬 로드, 단위/축 보정, 메타 정보 수집, 메시/스켈레톤/애니메이션 파싱을 하나의
 * 임포트 절차로 묶는다. 최종 결과는 렌더 가능한 UObject가 아니라 FFBXAsset 중간 데이터이며, 이후
 * 매니저가 이를 저장 가능한 UAsset 구조로 변환한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Types/FBXImportTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"
#include "Serialization/Archive.h"
#include "Serialization/ArchiveMath.h"

enum class EFBXLightType
{
    Point,
    Directional,
    Spot,
};

/**
 * FBX 씬에서 읽은 라이트 속성을 엔진 내부 표현으로 옮겨 담는 구조이다.
 */
struct FLightAsset
{
    EFBXLightType LightType;
    FMatrix       Transform;
};

using FCameraAsset = FMatrix;

enum class EFBXSceneComponentType
{
    StaticMesh,
    SkeletalMesh
};

/**
 * FBX 노드 하나를 에디터 씬 컴포넌트로 복원하기 위한 설명 구조이다.
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
        Ar << Desc.RelativeTransform;
        return Ar;
    }
};

/**
 * FBX 원본 하나에서 추출된 메시, 스켈레톤, 애니메이션, 컴포넌트 목록을 담는 중간 결과이다.
 */
struct FFBXAsset
{
    FString                        PathFileName;
    TArray<FStaticMesh>            StaticMeshes;
    TArray<FSkeletalMesh>          SkeletalMeshes;
    TArray<FSkeleton>               Skeletons;
    TArray<TArray<UAnimSequence *>> AnimSequences;
    TArray<TArray<FMeshMaterial>>   StaticMeshMaterials;
    TArray<TArray<FMeshMaterial>>   SkeletalMeshMaterials;
    TArray<FMeshMaterial>           SkeletalMaterials;
    TArray<FFBXSceneComponentDesc>  SceneComponents;
    TMap<int32, int32>              MeshIdToStaticMeshAssetIndex;
    TMap<int32, int32>              SkeletonIdToSkeletalMeshAssetIndex;
    TArray<FLightAsset>             LightAssets;
    TArray<FCameraAsset>            CameraAssets;
};

/**
 * FBX SDK 씬을 엔진의 중간 에셋 묶음으로 변환하는 임포트 파이프라인 객체이다.
 *
 * SDK 생명주기, 씬 전처리, 메타 분석, 메시/애니메이션 파서를 순서대로 실행한다. 결과는 FBX 씬 캐시나
 * 개별 UObject 생성 단계에서 다시 사용된다.
 */
class FBXImporter
{
  public:
    /**
     * FBX 파일 하나를 로드하고 씬 내부의 메시/스켈레톤/애니메이션 데이터를 추출한다.
     */
    bool ImportFbxAsset(const FString &InFilePath, FFBXAsset &OutFBXAsset);

  private:
    bool InitializeSdk();
    bool LoadScene(const FString &InFilePath);
    /**
     * 파서들이 만든 중간 데이터를 최종 FBX 임포트 결과에 연결한다.
     */
    bool FinalizeAsset();
    void ShutdownSdk();

  private:
    void ClearState();
    /**
     * FBX 씬의 단위와 좌표계를 엔진 기준에 맞게 변환한다.
     */
    void PreprocessScene();
    void DestroyScene();

  private:
    FFbxImportMeta ImportMeta;

  private:
    FbxManager *Manager = nullptr;
    FbxScene   *Scene = nullptr;
};
