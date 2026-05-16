/**
 * FBX SDK 데이터 접근을 엔진 타입으로 감싸는 공통 유틸리티를 선언한다.
 *
 * FBX는 매핑 모드와 참조 모드에 따라 노말, UV, 탄젠트의 실제 인덱싱 방식이 달라진다. 이 유틸리티는
 * 그 차이를 한곳에 모아 각 파서가 동일한 규칙으로 메시 속성을 읽도록 한다.
 */

#pragma once
#include "Core/CoreTypes.h"
#include "Engine/Math/Matrix.h"
#include "Engine/Math/Vector.h"

namespace fbxsdk
{
    class FbxAMatrix;
    class FbxCluster;
    class FbxManager;
    class FbxMesh;
    class FbxNode;
    class FbxScene;
    class FbxSkin;
    class FbxSurfaceMaterial;
    class FbxVector2;
    class FbxVector4;
} 

using FbxAMatrix = fbxsdk::FbxAMatrix;
using FbxCluster = fbxsdk::FbxCluster;
using FbxManager = fbxsdk::FbxManager;
using FbxMesh = fbxsdk::FbxMesh;
using FbxNode = fbxsdk::FbxNode;
using FbxScene = fbxsdk::FbxScene;
using FbxSkin = fbxsdk::FbxSkin;
using FbxSurfaceMaterial = fbxsdk::FbxSurfaceMaterial;
using FbxVector2 = fbxsdk::FbxVector2;
using FbxVector4 = fbxsdk::FbxVector4;

/**
 * FBX UV 읽기 과정에서 누락, 기본값 사용, 미러 보정 같은 상태를 집계한다.
 */
struct FFbxUVReadStats
{
    int32 PreferredSuccessCount = 0;
    int32 UVSetFallbackSuccessCount = 0;
    int32 ManualElementFallbackSuccessCount = 0;
    int32 GetPolygonVertexUVFailedCount = 0;
    int32 UnmappedCount = 0;
    int32 DefaultUVCount = 0;
};

/**
 * FBX SDK의 다양한 데이터 접근 방식을 엔진 타입으로 통일하는 유틸리티 모음이다.
 */
class FBXUtil
{
  public:
    static int32    GetNodeDepth(FbxNode *Node);
    /**
     * 외부 포맷 또는 중간 데이터를 엔진 내부 표현으로 변환한다.
     */
    static FVector  ConvertFbxVector(const FbxVector4 &V);
    /**
     * 외부 포맷 또는 중간 데이터를 엔진 내부 표현으로 변환한다.
     */
    static FVector2 ConvertFbxVector2(const FbxVector2 &V);
    /**
     * 외부 포맷 또는 중간 데이터를 엔진 내부 표현으로 변환한다.
     */
    static FMatrix  ConvertFbxMatrix(const FbxAMatrix &M);
    /**
     * FBX polygon의 material index layer를 읽어 섹션 분리에 사용할 슬롯을 구한다.
     */
    static int32    ReadMaterialIndex(FbxMesh *Mesh, int32 PolyIndex);
    /**
     * 원본 포맷에서 필요한 속성 값을 읽어 엔진 타입으로 변환한다.
     */
    static FVector  ReadPosition(FbxMesh *Mesh, int32 ControlPointIndex);
    /**
     * FBX normal layer element의 매핑/참조 모드를 해석해 폴리곤 정점 노말을 읽는다.
     */
    static FVector  ReadNormal(FbxMesh *Mesh, int32 PolyIndex, int32 CornerIndex);
    static FVector2 ReadUV(FbxMesh *Mesh, int32 PolyIndex, int32 CornerIndex,
                           int32 ControlPointIndex, int32 PolygonVertexCounter,
                           const char *PreferredUVSetName, FFbxUVReadStats *Stats = nullptr);
    /**
     * FBX tangent layer element의 매핑/참조 모드를 해석해 폴리곤 정점 탄젠트를 읽는다.
     */
    static FVector4 ReadTangent(FbxMesh *Mesh, int32 ControlPointIndex, int32 PolygonVertexIndex);
    static int32    QuantizeFloat(float Value);
    static FString  GetNodeName(FbxNode *Node);
};
