/**
 * FBX SDK와 엔진 타입 사이의 변환 유틸리티를 선언한다.
 *
 * FBX 원본 데이터는 SDK 고유 타입과 layer element 규칙을 사용하므로, 각 파서가 직접 읽으면 처리
 * 방식이 쉽게 달라진다. 이 클래스는 기본값, fallback, 통계 수집을 포함한 공통 읽기 경로를 제공한다.
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
} // namespace fbxsdk

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
 * FBX UV 읽기 과정에서 어떤 경로가 사용되었는지 집계하는 디버그 통계이다.
 *
 * preferred UV set, fallback, 기본값 사용 횟수를 기록해 특정 모델의 UV 누락 문제를 추적할 때
 * 사용한다.
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
 * FBX SDK 타입과 엔진 메시 타입 사이의 공통 변환 함수 모음이다.
 *
 * 좌표/행렬 변환, 노말/UV/탄젠트 읽기, 머티리얼 인덱스 해석을 통일한다. FBX layer element의 다양한
 * mapping/reference mode 처리를 이 클래스에 모아 임포터 전체가 같은 fallback 규칙을 사용하게 한다.
 */
class FBXUtil
{
  public:
    static int32    GetNodeDepth(FbxNode *Node);
    static FVector  ConvertFbxVector(const FbxVector4 &V);
    static FVector2 ConvertFbxVector2(const FbxVector2 &V);
    static FMatrix  ConvertFbxMatrix(const FbxAMatrix &M);
    static int32    ReadMaterialIndex(FbxMesh *Mesh, int32 PolyIndex);
    static FVector  ReadPosition(FbxMesh *Mesh, int32 ControlPointIndex);
    static FVector  ReadNormal(FbxMesh *Mesh, int32 PolyIndex, int32 CornerIndex);
    static FVector2 ReadUV(FbxMesh *Mesh, int32 PolyIndex, int32 CornerIndex,
                           int32 ControlPointIndex, int32 PolygonVertexCounter,
                           const char *PreferredUVSetName, FFbxUVReadStats *Stats = nullptr);
    static FVector4 ReadTangent(FbxMesh *Mesh, int32 ControlPointIndex, int32 PolygonVertexIndex);
    static int32    QuantizeFloat(float Value);
    static FString  GetNodeName(FbxNode *Node);
};
