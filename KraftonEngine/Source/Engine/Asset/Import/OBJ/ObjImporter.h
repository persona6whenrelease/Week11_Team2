/**
 * OBJ/MTL 원본 파일을 정적 메시와 머티리얼 데이터로 변환하는 임포터를 선언한다.
 *
 * OBJ는 위치, UV, 노말 인덱스가 서로 독립적으로 저장되는 텍스트 포맷이다. 이 임포터는 face token을
 * 해석해 엔진 정점 배열로 재구성하고, MTL의 색상/텍스처 정보를 엔진 머티리얼 파일로 변환한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"

struct FStaticMesh;

/**
 * OBJ 원본을 파싱한 뒤 변환 전까지 보관하는 중간 데이터이다.
 */
struct FObjInfo
{
    TArray<FVector>  Positions;
    TArray<FVector2> UVs;
    TArray<FVector>  Normals;
    TArray<uint32>   PosIndices;
    TArray<uint32>   UVIndices;
    TArray<uint32>   NormalIndices;

    FString ObjectName;

    FString                    MaterialLibraryFilePath;
    TArray<FStaticMeshSection> Sections;
};

/**
 * MTL 파일에서 읽은 머티리얼 색상과 텍스처 참조 정보를 저장한다.
 */
struct FObjMaterialInfo
{
    FString MaterialSlotName = "None";
    FVector Kd;
    FString map_Kd;

    FVector Ka;
    FVector Ks;
    float   Ns;
    float   Ni;
    int32   illum;
};

enum class EForwardAxis : uint8
{
    X,
    NegX,
    Y,
    NegY,
    Z,
    NegZ
};

enum class EWindingOrder : uint8
{
    CCW_to_CW,
    Keep
};

/**
 * 메시 임포트 시 축 방향, winding, 스케일 같은 변환 옵션을 전달하는 구조이다.
 */
struct FImportOptions
{
    float                 Scale = 1.0f;
    EForwardAxis          ForwardAxis = EForwardAxis::NegY;
    EWindingOrder         WindingOrder = EWindingOrder::CCW_to_CW;
    static FImportOptions Default() { return {}; }
};

/**
 * OBJ/MTL 텍스트 원본을 엔진 StaticMesh와 머티리얼 파일로 변환하는 임포터이다.
 */
struct FObjImporter
{
    static bool Import(const FString &ObjFilePath, FStaticMesh &OutMesh,
                       TArray<FStaticMaterial> &OutMaterials);
    static bool Import(const FString &ObjFilePath, const FImportOptions &Options,
                       FStaticMesh &OutMesh, TArray<FStaticMaterial> &OutMaterials);

  private:
    /**
     * OBJ 텍스트를 정점, 인덱스, 섹션, 머티리얼 참조를 가진 중간 데이터로 분해한다.
     */
    static bool ParseObj(const FString &ObjFilePath, FObjInfo &OutObjInfo);
    /**
     * MTL 파일의 색상과 텍스처 참조를 머티리얼 중간 데이터로 읽어 온다.
     */
    static bool ParseMtl(const FString &MtlFilePath, TArray<FObjMaterialInfo> &OutMaterials);
    static bool Convert(const FObjInfo &ObjInfo, const TArray<FObjMaterialInfo> &MtlInfos,
                        const FImportOptions &Options, FStaticMesh &OutMesh,
                        TArray<FStaticMaterial> &OutMaterials);

    /**
     * MTL에서 읽은 머티리얼 값을 엔진 머티리얼 JSON 형식으로 저장한다.
     */
    static FString ConvertMtlInfoToJson(const FObjMaterialInfo *MtlInfo);
    /**
     * MTL에서 읽은 머티리얼 값을 런타임 머티리얼 파일로 변환한다.
     */
    static FString ConvertMtlInfoToMat(const FObjMaterialInfo *MtlInfo);
    /**
     * 임포트 옵션의 축 방향과 스케일을 적용해 원본 좌표를 엔진 좌표로 변환한다.
     */
    static FVector RemapPosition(const FVector &ObjPos, EForwardAxis Axis);
};
