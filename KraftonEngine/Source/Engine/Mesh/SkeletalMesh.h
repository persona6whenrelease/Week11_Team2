/**
 * 스켈레탈 메시 UObject 래퍼를 선언한다.
 *
 * FSkeletalMesh 에셋 데이터와 GPU 버퍼, 머티리얼 슬롯을 연결하여 에디터와 렌더러가 같은 객체를 통해
 * 스켈레탈 메시를 다룰 수 있게 한다. 파일 로드 결과로 만들어진 순수 데이터가 실제 렌더링 가능한
 * 엔진 오브젝트로 올라오는 지점이다.
 */

#pragma once

#include "Mesh/SkeletalMeshAsset.h"
#include "Object/Object.h"

struct ID3D11Device;

/**
 * 스켈레탈 메시 에셋 데이터를 렌더링 가능한 UObject로 감싼 타입이다.
 *
 * FSkeletalMesh의 정점, 인덱스, 본, 애니메이션 정보를 보관하고 필요한 GPU 리소스를 생성한다.
 * 컴포넌트나 에셋 에디터는 이 객체를 통해 스켈레탈 메시의 슬롯, 본 계층, 렌더 버퍼에 접근한다.
 */
class USkeletalMesh : public UObject
{
  public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void Serialize(FArchive &Ar);

    const FString &GetAssetPathFileName() const;
    void           SetSkeletalMeshAsset(FSkeletalMesh *InMesh);
    FSkeletalMesh *GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }

    void                         SetMaterials(TArray<FMeshMaterial> &&InMaterials);
    const TArray<FMeshMaterial> &GetMaterials() const { return Materials; }

    const TArray<FMeshSection> &GetSections() const;

  private:
    void RebuildSectionMaterialIndices();

    FSkeletalMesh        *SkeletalMeshAsset = nullptr;
    TArray<FMeshMaterial> Materials;
};
