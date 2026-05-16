/**
 * 스켈레탈 메시 데이터를 UObject로 감싼 렌더링 에셋 타입을 선언한다.
 *
 * USkeletalMesh는 FSkeletalMesh 순수 데이터와 머티리얼 슬롯을 소유하고, 로드된 에셋을 컴포넌트와 렌더러가
 * 공유할 수 있는 형태로 제공한다. 애니메이션 포즈 적용은 컴포넌트 쪽에서 처리하지만, 본 가중치와 섹션
 * 정보의 기준은 이 객체가 가진다.
 */

#pragma once

#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Asset/AssetFileHeader.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Object/Object.h"

struct ID3D11Device;

/**
 * FSkeletalMesh 데이터를 UObject로 감싸 에디터와 런타임 컴포넌트가 참조할 수 있게 하는 에셋 타입이다.
 */
class USkeletalMesh : public UObject
{
  public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    static constexpr uint32 AssetVersion = 1;

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    /**
     * 에셋 헤더 검증과 본문 데이터 저장/로드를 함께 처리한다.
     */
    void Serialize(FArchive &Ar);

    const FString &GetAssetPathFileName() const;
    void           SetSkeletalMeshAsset(FSkeletalMesh *InMesh);
    FSkeletalMesh *GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }

    void                         SetMaterials(TArray<FMeshMaterial> &&InMaterials);
    const TArray<FMeshMaterial> &GetMaterials() const { return Materials; }

    const TArray<FMeshSection> &GetSections() const;

  private:
    /**
     * 섹션의 머티리얼 슬롯 이름을 현재 머티리얼 배열의 인덱스와 다시 맞춘다.
     */
    void RebuildSectionMaterialIndices();

    FSkeletalMesh        *SkeletalMeshAsset = nullptr;
    TArray<FMeshMaterial> Materials;
};
