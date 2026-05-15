/**
 * 정적 메시 UObject 래퍼와 LOD별 렌더 데이터를 선언한다.
 *
 * FStaticMesh 에셋 데이터를 기반으로 여러 LOD, 섹션, 머티리얼 슬롯, GPU 버퍼를 관리한다. 에디터에서
 * 로드한 메시와 렌더러가 draw command를 만들 때 참조하는 메시 객체가 같은 데이터 모델을 바라보도록
 * 하는 연결 계층이다.
 */

#pragma once

#include "Object/Object.h"
#include "Collision/MeshTriangleBVH.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"
#include "Serialization/Archive.h"

#include <memory>

struct ID3D11Device;

/**
 * StaticMesh의 특정 LOD가 렌더링에 필요한 데이터를 묶은 구조이다.
 *
 * 각 LOD는 별도의 정점/인덱스 버퍼와 섹션 정보를 가진다. 렌더링 단계에서는 거리나 설정에 따라
 * 사용할 LOD를 선택하고 이 구조의 버퍼를 draw call에 바인딩한다.
 */
struct FLODMeshData
{
    TArray<FStaticMeshSection>   Sections;
    std::unique_ptr<FMeshBuffer> RenderBuffer;
};

/**
 * 정적 메시 에셋 데이터를 렌더링 가능한 UObject로 감싼 타입이다.
 *
 * FStaticMesh의 LOD 데이터와 머티리얼 슬롯을 보관하고, D3D 버퍼 생성/해제를 담당한다. OBJ/FBX
 * 임포터가 만든 순수 데이터는 이 타입을 거쳐 실제 렌더 파이프라인에서 사용할 수 있는 리소스가 된다.
 */
class UStaticMesh : public UObject
{
  public:
    DECLARE_CLASS(UStaticMesh, UObject)

    static constexpr uint32 MAX_LOD_COUNT = 4;

    UStaticMesh() = default;
    ~UStaticMesh() override;

    void Serialize(FArchive &Ar);

    const FString                 &GetAssetPathFileName() const;
    void                           SetStaticMeshAsset(FStaticMesh *InMesh);
    FStaticMesh                   *GetStaticMeshAsset() const;
    void                           SetStaticMaterials(TArray<FStaticMaterial> &&InMaterials);
    const TArray<FStaticMaterial> &GetStaticMaterials() const;

    void InitResources(ID3D11Device *InDevice);

    void EnsureMeshTrianglePickingBVHBuilt() const;
    bool RaycastMeshTrianglesWithBVHLocal(const FVector &LocalOrigin, const FVector &LocalDirection,
                                          FHitResult &OutHitResult) const;

    uint32                            GetLODCount() const { return bHasLOD ? MAX_LOD_COUNT : 1; }
    FMeshBuffer                      *GetLODMeshBuffer(uint32 LODLevel) const;
    const TArray<FStaticMeshSection> &GetLODSections(uint32 LODLevel) const;

  private:
    FStaticMesh             *StaticMeshAsset = nullptr;
    TArray<FStaticMaterial>  StaticMaterials;
    mutable FMeshTriangleBVH MeshTrianglePickingBVH;

    FLODMeshData AdditionalLODs[3];
    bool         bHasLOD = false;
};
