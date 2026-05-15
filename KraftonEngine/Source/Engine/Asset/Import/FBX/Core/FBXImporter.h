/**
 * FBX 임포터의 최상위 결과 타입과 공개 임포트 인터페이스를 선언한다.
 *
 * FBX 하나는 단일 메시가 아니라 씬 계층, 여러 메시, 스켈레톤, 머티리얼, 라이트를 동시에 포함할 수
 * 있으므로 이 파일은 그 결과를 FFBXAsset으로 묶어 표현한다. ImportFBX는 원본 파일을 엔진이 저장하고
 * 참조할 수 있는 씬 에셋 구조로 바꾸는 진입점이다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Types/FBXImportTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"
#include "Serialization/Archive.h"

/**
 * FBX 라이트 타입을 엔진에서 구분하기 위한 열거형이다.
 *
 * 원본 SDK의 타입을 그대로 노출하지 않고, 씬 에셋 직렬화와 프리뷰 렌더링에서 사용할 수 있는
 * 최소한의 라이트 종류로 정리한다.
 */
enum class EFBXLightType
{
    Point,
    Directional,
    Spot,
};

/**
 * FBX 씬에서 가져온 라이트 정보를 저장하는 데이터이다.
 *
 * 위치, 방향, 색상, 강도, 타입 같은 렌더링 프리뷰에 필요한 값을 담는다. FBXSceneAsset이 원본 씬의
 * 조명 배치를 보존할 때 사용한다.
 */
struct FLightAsset
{
    EFBXLightType LightType;
    FMatrix       Transform;
};

using FCameraAsset = FMatrix;

/**
 * FBX 씬 계층의 컴포넌트 역할을 구분하는 타입이다.
 *
 * 노드가 단순 transform인지, 정적 메시인지, 스켈레탈 메시인지, 라이트인지에 따라 후속 생성 로직이
 * 달라지므로 씬 저장 단계에서 명시적으로 분류한다.
 */
enum class EFBXSceneComponentType
{
    StaticMesh,
    SkeletalMesh
};

/**
 * FBX 씬의 노드 하나를 엔진 컴포넌트 설명으로 바꾼 데이터이다.
 *
 * 부모/자식 관계, 로컬 transform, 연결된 메시나 라이트 인덱스를 담아 원본 FBX 계층을 재구성할 수
 * 있게 한다. 실제 컴포넌트를 생성하기 전의 직렬화 가능한 설명서 역할을 한다.
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
 * FBX 원본 하나를 임포트했을 때 만들어지는 전체 결과 묶음이다.
 *
 * 정적 메시, 스켈레탈 메시, 라이트, 씬 컴포넌트 계층을 함께 보관한다. FBX가 단일 메시 파일이 아니라
 * 씬 컨테이너일 수 있다는 점을 반영한 상위 에셋 데이터이다.
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
 * FBX SDK를 사용해 원본 FBX 파일을 엔진 에셋 데이터로 변환하는 최상위 임포터이다.
 *
 * 메타 파싱, 메시 파싱, 스켈레탈 메시 조립, 애니메이션 추출, 씬 컴포넌트 구성을 순서대로 수행한다.
 * 호출부는 파일 경로만 넘기고, 결과로 FBXSceneAsset 생성에 필요한 FFBXAsset을 받는다.
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
