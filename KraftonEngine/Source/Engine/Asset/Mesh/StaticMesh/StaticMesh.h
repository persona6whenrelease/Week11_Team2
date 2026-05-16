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
#include "Asset/AssetFileHeader.h"

#include <memory>

struct ID3D11Device;

/**
 * 정적 메시의 추가 LOD가 사용하는 섹션과 GPU 버퍼를 묶어 보관한다.
 */
struct FLODMeshData
{
    TArray<FStaticMeshSection>   Sections;
    std::unique_ptr<FMeshBuffer> RenderBuffer;
};

/**
 * FStaticMesh 데이터를 렌더링 가능한 UObject로 감싸고 LOD/GPU 리소스를 관리하는 에셋 타입이다.
 */
class UStaticMesh : public UObject
{
  public:
    DECLARE_CLASS(UStaticMesh, UObject)

    static constexpr uint32 MAX_LOD_COUNT = 4;
    static constexpr uint32 AssetVersion = 1;

    UStaticMesh() = default;
    ~UStaticMesh() override;

    /**
     * 에셋 헤더 검증과 본문 데이터 저장/로드를 함께 처리한다.
     */
    void Serialize(FArchive &Ar);

    const FString                 &GetAssetPathFileName() const;
    void                           SetStaticMeshAsset(FStaticMesh *InMesh);
    FStaticMesh                   *GetStaticMeshAsset() const;
    void                           SetStaticMaterials(TArray<FStaticMaterial> &&InMaterials);
    const TArray<FStaticMaterial> &GetStaticMaterials() const;

    /**
     * 로드된 메시 데이터를 기반으로 렌더링에 필요한 GPU 버퍼를 생성한다.
     */
    void InitResources(ID3D11Device *InDevice);

    /**
     * 에디터 피킹을 위해 메시 삼각형 BVH가 없으면 지연 생성한다.
     */
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
