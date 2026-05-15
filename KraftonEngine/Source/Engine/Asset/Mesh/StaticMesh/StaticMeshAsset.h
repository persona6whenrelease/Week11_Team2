/**
 * 정적 메시 에셋의 직렬화 가능한 순수 데이터 구조를 정의한다.
 *
 * 위치, 노말, 탄젠트, UV를 가진 정점 배열과 인덱스, 섹션, 머티리얼 슬롯을 저장한다. FBX/OBJ
 * 임포터는 각자의 원본 포맷을 이 구조로 변환하고, UStaticMesh는 이 데이터를 기반으로 GPU 리소스를
 * 만든다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Engine/Object/Object.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Render/Resource/Buffer.h"
#include "Serialization/Archive.h"
#include <memory>
#include <algorithm>

/**
 * 정적 메시 렌더링에 사용하는 기본 정점 형식이다.
 *
 * 위치, 노말, 탄젠트, UV를 포함하며 StaticMesh와 메시 간소화, FBX/OBJ 임포터가 공통으로 사용하는
 * 정점 버퍼 단위이다.
 */
struct FNormalVertex
{
    FVector  pos;
    FVector  normal;
    FVector4 color;
    FVector2 tex;
    FVector4 tangent;
};

/**
 * 정적 메시 에셋의 저장 단위이다.
 *
 * LOD별 지오메트리, 섹션, 머티리얼 슬롯을 포함하며 UObject나 GPU 버퍼에 의존하지 않는다. 원본
 * 임포트 결과를 저장하거나 다시 로드할 때 사용하는 순수 데이터 모델이다.
 */
struct FStaticMesh
{
    FString               PathFileName;
    TArray<FNormalVertex> Vertices;
    TArray<uint32>        Indices;

    TArray<FStaticMeshSection> Sections;

    std::unique_ptr<FMeshBuffer> RenderBuffer;

    FVector BoundsCenter = FVector(0, 0, 0);
    FVector BoundsExtent = FVector(0, 0, 0);
    bool    bBoundsValid = false;

    void CacheBounds()
    {
        bBoundsValid = false;
        if (Vertices.empty())
            return;

        FVector LocalMin = Vertices[0].pos;
        FVector LocalMax = Vertices[0].pos;
        for (const FNormalVertex &V : Vertices)
        {
            LocalMin.X = (std::min)(LocalMin.X, V.pos.X);
            LocalMin.Y = (std::min)(LocalMin.Y, V.pos.Y);
            LocalMin.Z = (std::min)(LocalMin.Z, V.pos.Z);
            LocalMax.X = (std::max)(LocalMax.X, V.pos.X);
            LocalMax.Y = (std::max)(LocalMax.Y, V.pos.Y);
            LocalMax.Z = (std::max)(LocalMax.Z, V.pos.Z);
        }

        BoundsCenter = (LocalMin + LocalMax) * 0.5f;
        BoundsExtent = (LocalMax - LocalMin) * 0.5f;
        bBoundsValid = true;
    }

    void Serialize(FArchive &Ar)
    {
        Ar << PathFileName;
        Ar << Vertices;
        Ar << Indices;
        Ar << Sections;
    }
};
