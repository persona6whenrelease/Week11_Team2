/**
 * OBJ 임포트에 필요한 원본 파싱 결과와 옵션, 임포터 인터페이스를 선언한다.
 *
 * OBJ는 위치/UV/노말 인덱스가 서로 독립적이므로, 이 파일의 구조체들은 원본 파일의 표현을 먼저
 * 보존한 뒤 엔진이 사용하는 단일 정점 배열로 합치는 과정을 지원한다. 축 변환, winding 보정,
 * 머티리얼 자동 생성 같은 정책은 FImportOptions로 분리되어 호출부가 같은 파서에 다른 변환 규칙을
 * 적용할 수 있다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Mesh/MeshCommonTypes.h"

struct FStaticMesh;

/**
 * OBJ 파일을 1차 파싱한 원본 중심 데이터이다.
 *
 * OBJ는 position, uv, normal 인덱스가 서로 독립적이고 face에서 조합되므로, 즉시 엔진 정점으로
 * 합치지 않고 원본 배열과 face 정보를 먼저 보관한다. 이후 빌드 단계에서 중복 제거와 인덱스 재구성이
 * 수행된다.
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
 * MTL 파일에서 읽은 머티리얼 속성이다.
 *
 * OBJ 임포트 단계에서 엔진 머티리얼을 자동 생성하거나 슬롯 정보를 구성할 때 사용한다. 텍스처 경로는
 * 원본 파일 기준 문자열일 수 있으므로, 실제 에셋 경로로 확정되기 전의 중간 정보로 취급된다.
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

/**
 * OBJ 원본이 어떤 축을 전방으로 사용하는지 나타내는 옵션이다.
 *
 * 외부 툴마다 forward axis가 다르기 때문에 임포트 시 엔진 좌표계에 맞춰 회전 보정이 필요하다.
 */
enum class EForwardAxis : uint8
{
    X,
    NegX,
    Y,
    NegY,
    Z,
    NegZ
};

/**
 * 삼각형 정점 순서 보정 정책이다.
 *
 * 좌표계 변환이나 원본 툴 차이로 front face 판정이 뒤집힐 수 있으므로, 임포트 단계에서 winding을
 * 유지하거나 반전하는 선택지를 제공한다.
 */
enum class EWindingOrder : uint8
{
    CCW_to_CW,
    Keep
};

/**
 * OBJ를 엔진 메시 데이터로 변환할 때 적용할 정책 묶음이다.
 *
 * 스케일, 축 보정, winding, 머티리얼 생성 여부처럼 파일 내용 자체가 아니라 프로젝트가 선택하는
 * 임포트 규칙을 담는다. 같은 OBJ라도 이 값에 따라 최종 메시 방향과 슬롯 구성이 달라질 수 있다.
 */
struct FImportOptions
{
    float                 Scale = 1.0f;
    EForwardAxis          ForwardAxis = EForwardAxis::NegY;
    EWindingOrder         WindingOrder = EWindingOrder::CCW_to_CW;
    static FImportOptions Default() { return {}; }
};

/**
 * OBJ/MTL 텍스트 파일을 FStaticMesh 데이터로 변환하는 임포터이다.
 *
 * 원본 파일 파싱, face triangulation, 정점 조합 중복 제거, 머티리얼 슬롯 구성까지 수행한다. 결과는
 * GPU 리소스를 아직 갖지 않는 에셋 데이터이며, ObjManager가 캐시 저장이나 UStaticMesh 생성으로
 * 이어간다.
 */
struct FObjImporter
{
    static bool Import(const FString &ObjFilePath, FStaticMesh &OutMesh,
                       TArray<FStaticMaterial> &OutMaterials);
    static bool Import(const FString &ObjFilePath, const FImportOptions &Options,
                       FStaticMesh &OutMesh, TArray<FStaticMaterial> &OutMaterials);

  private:
    static bool ParseObj(const FString &ObjFilePath, FObjInfo &OutObjInfo);
    static bool ParseMtl(const FString &MtlFilePath, TArray<FObjMaterialInfo> &OutMaterials);
    static bool Convert(const FObjInfo &ObjInfo, const TArray<FObjMaterialInfo> &MtlInfos,
                        const FImportOptions &Options, FStaticMesh &OutMesh,
                        TArray<FStaticMaterial> &OutMaterials);

    static FString ConvertMtlInfoToJson(const FObjMaterialInfo *MtlInfo);
    static FString ConvertMtlInfoToMat(const FObjMaterialInfo *MtlInfo);
    static FVector RemapPosition(const FVector &ObjPos, EForwardAxis Axis);
};
